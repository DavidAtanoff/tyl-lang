// Tyl Compiler - Native Code Generator Miscellaneous Statements
// Handles: Block, Return, Break, Continue, Try, Delete

#include "backend/codegen/codegen_base.h"
#include "semantic/ownership/ownership.h"
#include "semantic/ctfe/ctfe_interpreter.h"

namespace tyl {

void NativeCodeGen::visit(Block& node) {
    // Track variables declared in this block for drop at scope exit
    std::vector<std::string> blockVars;
    // Track which variables have been moved (should not be dropped)
    std::unordered_set<std::string> movedVars;
    
    for (auto& stmt : node.statements) {
        if (dynamic_cast<FnDecl*>(stmt.get())) {
            continue;
        }
        
        // Track variable declarations for drop
        if (auto* varDecl = dynamic_cast<VarDecl*>(stmt.get())) {
            // Check if this type has a Drop implementation
            std::string typeName = varDecl->typeName;
            if (typeName.empty() && varDecl->initializer) {
                // Try to infer type from initializer
                if (auto* recExpr = dynamic_cast<RecordExpr*>(varDecl->initializer.get())) {
                    typeName = recExpr->typeName;
                }
                // Also check if initializer is an identifier with known type
                else if (auto* srcId = dynamic_cast<Identifier*>(varDecl->initializer.get())) {
                    // First check blockVars for local variables in this block
                    auto typeIt = varRecordTypes_.find(srcId->name);
                    if (typeIt != varRecordTypes_.end()) {
                        typeName = typeIt->second;
                    }
                }
            }
            
            // Check if this type has Drop trait implemented
            std::string dropKey = "Drop:" + typeName;
            bool hasDrop = !typeName.empty() && impls_.count(dropKey);
            
            if (hasDrop) {
                blockVars.push_back(varDecl->name);
                varRecordTypes_[varDecl->name] = typeName;
            }
            
            // Also check ownership system for custom drop
            if (!typeName.empty() && OwnershipTracker::hasCustomDrop(typeName)) {
                if (std::find(blockVars.begin(), blockVars.end(), varDecl->name) == blockVars.end()) {
                    blockVars.push_back(varDecl->name);
                    varRecordTypes_[varDecl->name] = typeName;
                }
            }
            
            // Check if initializer is a move from another variable
            if (varDecl->initializer) {
                if (auto* srcId = dynamic_cast<Identifier*>(varDecl->initializer.get())) {
                    // This is a move: let b = a
                    // Mark the source as moved (don't drop it)
                    if (std::find(blockVars.begin(), blockVars.end(), srcId->name) != blockVars.end()) {
                        movedVars.insert(srcId->name);
                    }
                }
            }
        }
        
        // Track ExprStmt containing AssignExpr (Flex syntax: x = value)
        if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt.get())) {
            if (auto* assignExpr = dynamic_cast<AssignExpr*>(exprStmt->expr.get())) {
                if (auto* targetId = dynamic_cast<Identifier*>(assignExpr->target.get())) {
                    std::string typeName;
                    
                    // Check if value is a RecordExpr
                    if (auto* recExpr = dynamic_cast<RecordExpr*>(assignExpr->value.get())) {
                        typeName = recExpr->typeName;
                    }
                    // Check if value is an identifier with known type (move)
                    else if (auto* srcId = dynamic_cast<Identifier*>(assignExpr->value.get())) {
                        auto typeIt = varRecordTypes_.find(srcId->name);
                        if (typeIt != varRecordTypes_.end()) {
                            typeName = typeIt->second;
                            // This is a move - mark source as moved
                            if (std::find(blockVars.begin(), blockVars.end(), srcId->name) != blockVars.end()) {
                                movedVars.insert(srcId->name);
                            }
                        }
                    }
                    
                    if (!typeName.empty()) {
                        // Check if this type has Drop trait implemented
                        std::string dropKey = "Drop:" + typeName;
                        bool hasDrop = impls_.count(dropKey) > 0;
                        
                        if (hasDrop) {
                            // Only add if not already tracked
                            if (std::find(blockVars.begin(), blockVars.end(), targetId->name) == blockVars.end()) {
                                blockVars.push_back(targetId->name);
                            }
                            varRecordTypes_[targetId->name] = typeName;
                        }
                        
                        // Also check ownership system for custom drop
                        if (OwnershipTracker::hasCustomDrop(typeName)) {
                            if (std::find(blockVars.begin(), blockVars.end(), targetId->name) == blockVars.end()) {
                                blockVars.push_back(targetId->name);
                            }
                            varRecordTypes_[targetId->name] = typeName;
                        }
                    }
                }
            }
        }
        
        // Track assignments that are moves
        if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt.get())) {
            if (auto* srcId = dynamic_cast<Identifier*>(assignStmt->value.get())) {
                // Check if source is a droppable variable being moved
                if (std::find(blockVars.begin(), blockVars.end(), srcId->name) != blockVars.end()) {
                    movedVars.insert(srcId->name);
                }
            }
        }
        
        stmt->accept(*this);
    }
    
    // Emit drop calls for variables in reverse declaration order
    // Skip variables that have been moved
    for (auto it = blockVars.rbegin(); it != blockVars.rend(); ++it) {
        const std::string& varName = *it;
        
        // Skip moved variables - they don't own the value anymore
        if (movedVars.count(varName)) {
            continue;
        }
        
        auto typeIt = varRecordTypes_.find(varName);
        if (typeIt == varRecordTypes_.end()) {
            continue;
        }
        
        std::string typeName = typeIt->second;
        std::string dropLabel;
        
        // First try to get drop label from impls_
        std::string dropKey = "Drop:" + typeName;
        auto implIt = impls_.find(dropKey);
        if (implIt != impls_.end()) {
            auto methodIt = implIt->second.methodLabels.find("drop");
            if (methodIt != implIt->second.methodLabels.end()) {
                dropLabel = methodIt->second;
            }
        }
        
        // If not found in impls_, try ownership system
        if (dropLabel.empty()) {
            const DropInfo* dropInfo = OwnershipTracker::getDropInfo(typeName);
            if (dropInfo && dropInfo->hasCustomDrop) {
                dropLabel = dropInfo->dropFunctionName;
            }
        }
        
        if (dropLabel.empty()) {
            continue;
        }
        
        // Load variable value into rcx (self parameter)
        // For records, the variable contains a pointer to the heap-allocated record
        auto localIt = locals.find(varName);
        auto regIt = varRegisters_.find(varName);
        
        if (regIt != varRegisters_.end() && regIt->second != VarRegister::NONE) {
            // Variable is in a register - move to RAX first, then to RCX
            switch (regIt->second) {
                case VarRegister::RBX: asm_.mov_rax_rbx(); break;
                case VarRegister::R12: asm_.mov_rax_r12(); break;
                case VarRegister::R13: asm_.mov_rax_r13(); break;
                case VarRegister::R14: asm_.mov_rax_r14(); break;
                case VarRegister::R15: asm_.mov_rax_r15(); break;
                default: break;
            }
            asm_.mov_rcx_rax();
            
            // Call the drop function
            if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
            asm_.call_rel32(dropLabel);
            if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
        } else if (localIt != locals.end()) {
            // Variable is on stack - load its value (the pointer to the record)
            asm_.mov_rcx_mem_rbp(localIt->second);
            
            // Call the drop function
            if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
            asm_.call_rel32(dropLabel);
            if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
        }
    }
}

void NativeCodeGen::visit(ReturnStmt& node) {
    if (node.value) {
        // Check if we're returning a borrow parameter and need to auto-dereference
        if (auto* id = dynamic_cast<Identifier*>(node.value.get())) {
            auto borrowIt = borrowParams_.find(id->name);
            if (borrowIt != borrowParams_.end()) {
                // This is a borrow parameter - check if return type is non-reference
                bool returnTypeIsRef = !currentFnReturnType_.empty() && currentFnReturnType_[0] == '&';
                if (!returnTypeIsRef) {
                    // Return type is not a reference, so auto-dereference the borrow
                    // First load the address (the borrow parameter value)
                    node.value->accept(*this);
                    // Then dereference it to get the actual value
                    asm_.mov_rax_mem_rax();
                } else {
                    // Return type is a reference, return the address as-is
                    node.value->accept(*this);
                }
            } else {
                node.value->accept(*this);
            }
        } else {
            node.value->accept(*this);
        }
    } else {
        asm_.xor_rax_rax();
    }
    
    // Function epilogue - must match the prologue condition in FnDecl::visit
    // The prologue uses: isLeafFunction_ && varRegisters_.size() == params.size() && params.size() <= 4
    // Since we don't have access to params here, we use stackAllocated_ which tracks if sub rsp was done
    if (!stackAllocated_) {
        // Simplified epilogue for leaf functions without stack allocation
        emitRestoreCalleeSavedRegs();
    } else {
        // Full epilogue with stack cleanup
        asm_.add_rsp_imm32(functionStackSize_);
        emitRestoreCalleeSavedRegs();
        asm_.pop_rbp();
    }
    
    asm_.ret();
}

void NativeCodeGen::visit(BreakStmt& node) {
    if (!loopStack.empty()) {
        // If label specified, find the matching loop
        if (!node.label.empty()) {
            for (auto it = loopStack.rbegin(); it != loopStack.rend(); ++it) {
                if (it->label == node.label) {
                    asm_.jmp_rel32(it->breakLabel);
                    return;
                }
            }
            // Label not found - fall through to innermost loop
        }
        asm_.jmp_rel32(loopStack.back().breakLabel);
    }
}

void NativeCodeGen::visit(ContinueStmt& node) {
    if (!loopStack.empty()) {
        // If label specified, find the matching loop
        if (!node.label.empty()) {
            for (auto it = loopStack.rbegin(); it != loopStack.rend(); ++it) {
                if (it->label == node.label) {
                    asm_.jmp_rel32(it->continueLabel);
                    return;
                }
            }
            // Label not found - fall through to innermost loop
        }
        asm_.jmp_rel32(loopStack.back().continueLabel);
    }
}

void NativeCodeGen::visit(TryStmt& node) {
    // TryStmt in Flex is a try-else expression (like Rust's ? operator)
    // tryExpr is evaluated, and if it fails, elseExpr is used
    // For now, just evaluate the try expression
    node.tryExpr->accept(*this);
    
    // If we had proper error handling, we'd check for error and use elseExpr
    // For now, this is a simplified implementation
    if (node.elseExpr) {
        // The else expression would be used if tryExpr fails
        // This would require proper Result/Option type support
        (void)node.elseExpr;
    }
}

void NativeCodeGen::visit(DeleteStmt& node) {
    // Delete: free the memory pointed to by the expression
    node.expr->accept(*this);
    asm_.mov_r8_rax();
    
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("GetProcessHeap"));
    asm_.mov_rcx_rax();
    asm_.xor_rax_rax();
    asm_.mov_rdx_rax();
    asm_.call_mem_rip(pe_.getImportRVA("HeapFree"));
    if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
}

// Helper to parse a register name and return its encoding
static int parseRegister(const std::string& reg) {
    if (reg == "rax" || reg == "eax" || reg == "ax" || reg == "al") return 0;
    if (reg == "rcx" || reg == "ecx" || reg == "cx" || reg == "cl") return 1;
    if (reg == "rdx" || reg == "edx" || reg == "dx" || reg == "dl") return 2;
    if (reg == "rbx" || reg == "ebx" || reg == "bx" || reg == "bl") return 3;
    if (reg == "rsp" || reg == "esp" || reg == "sp" || reg == "spl") return 4;
    if (reg == "rbp" || reg == "ebp" || reg == "bp" || reg == "bpl") return 5;
    if (reg == "rsi" || reg == "esi" || reg == "si" || reg == "sil") return 6;
    if (reg == "rdi" || reg == "edi" || reg == "di" || reg == "dil") return 7;
    if (reg == "r8" || reg == "r8d" || reg == "r8w" || reg == "r8b") return 8;
    if (reg == "r9" || reg == "r9d" || reg == "r9w" || reg == "r9b") return 9;
    if (reg == "r10" || reg == "r10d" || reg == "r10w" || reg == "r10b") return 10;
    if (reg == "r11" || reg == "r11d" || reg == "r11w" || reg == "r11b") return 11;
    if (reg == "r12" || reg == "r12d" || reg == "r12w" || reg == "r12b") return 12;
    if (reg == "r13" || reg == "r13d" || reg == "r13w" || reg == "r13b") return 13;
    if (reg == "r14" || reg == "r14d" || reg == "r14w" || reg == "r14b") return 14;
    if (reg == "r15" || reg == "r15d" || reg == "r15w" || reg == "r15b") return 15;
    return -1;
}

static bool is64BitReg(const std::string& reg) {
    return reg[0] == 'r' && reg != "rsp" && reg != "rbp" && reg != "rsi" && reg != "rdi" && 
           (reg.size() == 3 || (reg.size() > 1 && reg[1] >= '0' && reg[1] <= '9'));
}

static bool isExtReg(const std::string& reg) {
    return reg.size() >= 2 && reg[0] == 'r' && reg[1] >= '8' && reg[1] <= '9';
}

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t");
    return s.substr(start, end - start + 1);
}

static std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> result;
    std::string current;
    for (char c : s) {
        if (c == delim) {
            if (!current.empty()) result.push_back(trim(current));
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) result.push_back(trim(current));
    return result;
}

void NativeCodeGen::visit(AsmStmt& node) {
    // Parse and emit inline assembly
    // Split by newlines and process each instruction
    std::vector<std::string> lines = split(node.code, '\n');
    
    for (const std::string& line : lines) {
        std::string instr = trim(line);
        if (instr.empty() || instr[0] == ';') continue;  // Skip empty lines and comments
        
        // Convert to lowercase for comparison
        std::string instrLower;
        for (char c : instr) instrLower += (c >= 'A' && c <= 'Z') ? (c + 32) : c;
        
        // Parse instruction and operands
        size_t spacePos = instrLower.find(' ');
        std::string mnemonic = (spacePos != std::string::npos) ? instrLower.substr(0, spacePos) : instrLower;
        std::string operands = (spacePos != std::string::npos) ? trim(instrLower.substr(spacePos + 1)) : "";
        
        // Handle common instructions
        if (mnemonic == "ret") {
            asm_.ret();
        }
        else if (mnemonic == "nop") {
            asm_.code.push_back(0x90);
        }
        else if (mnemonic == "push") {
            int reg = parseRegister(operands);
            if (reg >= 0) {
                if (reg >= 8) {
                    asm_.code.push_back(0x41);
                    asm_.code.push_back(0x50 + (reg - 8));
                } else {
                    asm_.code.push_back(0x50 + reg);
                }
            }
        }
        else if (mnemonic == "pop") {
            int reg = parseRegister(operands);
            if (reg >= 0) {
                if (reg >= 8) {
                    asm_.code.push_back(0x41);
                    asm_.code.push_back(0x58 + (reg - 8));
                } else {
                    asm_.code.push_back(0x58 + reg);
                }
            }
        }
        else if (mnemonic == "mov") {
            auto parts = split(operands, ',');
            if (parts.size() == 2) {
                int dst = parseRegister(parts[0]);
                int src = parseRegister(parts[1]);
                
                if (dst >= 0 && src >= 0) {
                    // mov reg, reg
                    uint8_t rex = 0x48;
                    if (dst >= 8) rex |= 0x01;  // REX.B
                    if (src >= 8) rex |= 0x04;  // REX.R
                    asm_.code.push_back(rex);
                    asm_.code.push_back(0x89);
                    asm_.code.push_back(0xC0 | ((src & 7) << 3) | (dst & 7));
                }
                else if (dst >= 0) {
                    // mov reg, imm
                    int64_t imm = 0;
                    try {
                        if (parts[1].size() > 2 && parts[1][0] == '0' && parts[1][1] == 'x') {
                            imm = std::stoll(parts[1], nullptr, 16);
                        } else {
                            imm = std::stoll(parts[1]);
                        }
                    } catch (...) {
                        continue;  // Skip invalid immediate
                    }
                    
                    // mov r64, imm64
                    uint8_t rex = 0x48;
                    if (dst >= 8) rex |= 0x01;
                    asm_.code.push_back(rex);
                    asm_.code.push_back(0xB8 + (dst & 7));
                    for (int i = 0; i < 8; i++) {
                        asm_.code.push_back((imm >> (i * 8)) & 0xFF);
                    }
                }
            }
        }
        else if (mnemonic == "xor") {
            auto parts = split(operands, ',');
            if (parts.size() == 2) {
                int dst = parseRegister(parts[0]);
                int src = parseRegister(parts[1]);
                if (dst >= 0 && src >= 0) {
                    uint8_t rex = 0x48;
                    if (dst >= 8) rex |= 0x01;
                    if (src >= 8) rex |= 0x04;
                    asm_.code.push_back(rex);
                    asm_.code.push_back(0x31);
                    asm_.code.push_back(0xC0 | ((src & 7) << 3) | (dst & 7));
                }
            }
        }
        else if (mnemonic == "add") {
            auto parts = split(operands, ',');
            if (parts.size() == 2) {
                int dst = parseRegister(parts[0]);
                int src = parseRegister(parts[1]);
                if (dst >= 0 && src >= 0) {
                    uint8_t rex = 0x48;
                    if (dst >= 8) rex |= 0x01;
                    if (src >= 8) rex |= 0x04;
                    asm_.code.push_back(rex);
                    asm_.code.push_back(0x01);
                    asm_.code.push_back(0xC0 | ((src & 7) << 3) | (dst & 7));
                }
                else if (dst >= 0) {
                    // add reg, imm
                    int64_t imm = 0;
                    try {
                        if (parts[1].size() > 2 && parts[1][0] == '0' && parts[1][1] == 'x') {
                            imm = std::stoll(parts[1], nullptr, 16);
                        } else {
                            imm = std::stoll(parts[1]);
                        }
                    } catch (...) { continue; }
                    
                    uint8_t rex = 0x48;
                    if (dst >= 8) rex |= 0x01;
                    asm_.code.push_back(rex);
                    if (imm >= -128 && imm <= 127) {
                        asm_.code.push_back(0x83);
                        asm_.code.push_back(0xC0 | (dst & 7));
                        asm_.code.push_back((int8_t)imm);
                    } else {
                        asm_.code.push_back(0x81);
                        asm_.code.push_back(0xC0 | (dst & 7));
                        for (int i = 0; i < 4; i++) {
                            asm_.code.push_back((imm >> (i * 8)) & 0xFF);
                        }
                    }
                }
            }
        }
        else if (mnemonic == "sub") {
            auto parts = split(operands, ',');
            if (parts.size() == 2) {
                int dst = parseRegister(parts[0]);
                int src = parseRegister(parts[1]);
                if (dst >= 0 && src >= 0) {
                    uint8_t rex = 0x48;
                    if (dst >= 8) rex |= 0x01;
                    if (src >= 8) rex |= 0x04;
                    asm_.code.push_back(rex);
                    asm_.code.push_back(0x29);
                    asm_.code.push_back(0xC0 | ((src & 7) << 3) | (dst & 7));
                }
                else if (dst >= 0) {
                    int64_t imm = 0;
                    try {
                        if (parts[1].size() > 2 && parts[1][0] == '0' && parts[1][1] == 'x') {
                            imm = std::stoll(parts[1], nullptr, 16);
                        } else {
                            imm = std::stoll(parts[1]);
                        }
                    } catch (...) { continue; }
                    
                    uint8_t rex = 0x48;
                    if (dst >= 8) rex |= 0x01;
                    asm_.code.push_back(rex);
                    if (imm >= -128 && imm <= 127) {
                        asm_.code.push_back(0x83);
                        asm_.code.push_back(0xE8 | (dst & 7));
                        asm_.code.push_back((int8_t)imm);
                    } else {
                        asm_.code.push_back(0x81);
                        asm_.code.push_back(0xE8 | (dst & 7));
                        for (int i = 0; i < 4; i++) {
                            asm_.code.push_back((imm >> (i * 8)) & 0xFF);
                        }
                    }
                }
            }
        }
        else if (mnemonic == "inc") {
            int reg = parseRegister(operands);
            if (reg >= 0) {
                uint8_t rex = 0x48;
                if (reg >= 8) rex |= 0x01;
                asm_.code.push_back(rex);
                asm_.code.push_back(0xFF);
                asm_.code.push_back(0xC0 | (reg & 7));
            }
        }
        else if (mnemonic == "dec") {
            int reg = parseRegister(operands);
            if (reg >= 0) {
                uint8_t rex = 0x48;
                if (reg >= 8) rex |= 0x01;
                asm_.code.push_back(rex);
                asm_.code.push_back(0xFF);
                asm_.code.push_back(0xC8 | (reg & 7));
            }
        }
        else if (mnemonic == "imul") {
            auto parts = split(operands, ',');
            if (parts.size() == 2) {
                int dst = parseRegister(parts[0]);
                int src = parseRegister(parts[1]);
                if (dst >= 0 && src >= 0) {
                    uint8_t rex = 0x48;
                    if (src >= 8) rex |= 0x01;
                    if (dst >= 8) rex |= 0x04;
                    asm_.code.push_back(rex);
                    asm_.code.push_back(0x0F);
                    asm_.code.push_back(0xAF);
                    asm_.code.push_back(0xC0 | ((dst & 7) << 3) | (src & 7));
                }
            }
        }
        else if (mnemonic == "syscall") {
            asm_.code.push_back(0x0F);
            asm_.code.push_back(0x05);
        }
        else if (mnemonic == "int3") {
            asm_.code.push_back(0xCC);
        }
        // Add more instructions as needed
    }
}

// Syntax Redesign - New Statement Visitors
void NativeCodeGen::visit(LoopStmt& node) {
    // Infinite loop - same as while true
    std::string loopLabel = newLabel("loop");
    std::string endLabel = newLabel("loop_end");
    std::string continueLabel = newLabel("loop_continue");
    
    // Push loop context for break/continue
    loopStack.push_back({node.label, continueLabel, endLabel});
    
    // Loop start
    asm_.label(loopLabel);
    asm_.label(continueLabel);
    
    // Execute body
    node.body->accept(*this);
    
    // Jump back to start
    asm_.jmp_rel32(loopLabel);
    
    // End label for break
    asm_.label(endLabel);
    
    loopStack.pop_back();
}

void NativeCodeGen::visit(WithStmt& node) {
    // Resource management: with resource as alias: body
    // Evaluate resource
    node.resource->accept(*this);
    
    // Store resource pointer for cleanup
    std::string resourceVar = node.alias.empty() ? newLabel("with_resource") : node.alias;
    allocLocal(resourceVar);
    int32_t resourceOffset = locals[resourceVar];
    asm_.mov_mem_rbp_rax(resourceOffset);
    
    // Track the type of the resource for cleanup
    std::string resourceType;
    if (auto* call = dynamic_cast<CallExpr*>(node.resource.get())) {
        if (auto* id = dynamic_cast<Identifier*>(call->callee.get())) {
            // Check if this is a record constructor
            if (recordTypes_.count(id->name)) {
                resourceType = id->name;
                varRecordTypes_[resourceVar] = resourceType;
            }
        }
    }
    if (auto* recExpr = dynamic_cast<RecordExpr*>(node.resource.get())) {
        if (!recExpr->typeName.empty()) {
            resourceType = recExpr->typeName;
            varRecordTypes_[resourceVar] = resourceType;
        }
    }
    
    // Execute body
    node.body->accept(*this);
    
    // Cleanup: call .close() or .__del__() method on the resource
    // Load resource pointer
    asm_.mov_rax_mem_rbp(resourceOffset);
    
    // Check if resource is not null before calling cleanup
    asm_.test_rax_rax();
    std::string skipCloseLabel = newLabel("skip_close");
    asm_.jz_rel32(skipCloseLabel);
    
    bool cleanupEmitted = false;
    
    // Try to call close() or __del__() method
    // First check if this is a known type with a close or __del__ method
    auto recIt = varRecordTypes_.find(resourceVar);
    if (recIt != varRecordTypes_.end()) {
        std::string typeName = recIt->second;
        
        // Try close() first
        std::string closeMethod = typeName + "_close";
        if (asm_.labels.count(closeMethod)) {
            // Call TypeName_close(resource)
            asm_.mov_rax_mem_rbp(resourceOffset);
            asm_.mov_rcx_rax();  // First arg = self
            if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
            asm_.call_rel32(closeMethod);
            if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
            cleanupEmitted = true;
        }
        
        // Also try __del__() destructor
        std::string delMethod = typeName + "___del__";
        if (asm_.labels.count(delMethod)) {
            asm_.mov_rax_mem_rbp(resourceOffset);
            asm_.mov_rcx_rax();  // First arg = self
            if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
            asm_.call_rel32(delMethod);
            if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
            cleanupEmitted = true;
        }
        
        // Try drop() method (Rust-style)
        std::string dropMethod = typeName + "_drop";
        if (asm_.labels.count(dropMethod)) {
            asm_.mov_rax_mem_rbp(resourceOffset);
            asm_.mov_rcx_rax();  // First arg = self
            if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
            asm_.call_rel32(dropMethod);
            if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
            cleanupEmitted = true;
        }
        
        // Try dispose() method (.NET-style)
        std::string disposeMethod = typeName + "_dispose";
        if (asm_.labels.count(disposeMethod)) {
            asm_.mov_rax_mem_rbp(resourceOffset);
            asm_.mov_rcx_rax();  // First arg = self
            if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
            asm_.call_rel32(disposeMethod);
            if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
            cleanupEmitted = true;
        }
    }
    
    // For file handles, call CloseHandle
    // Check if this looks like a file handle (from open() call)
    if (!cleanupEmitted) {
        if (auto* call = dynamic_cast<CallExpr*>(node.resource.get())) {
            if (auto* id = dynamic_cast<Identifier*>(call->callee.get())) {
                if (id->name == "open" || id->name == "fopen") {
                    // This is a file handle, call CloseHandle
                    asm_.mov_rax_mem_rbp(resourceOffset);
                    asm_.mov_rcx_rax();  // Handle
                    if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
                    asm_.call_mem_rip(pe_.getImportRVA("CloseHandle"));
                    if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
                }
            }
        }
    }
    
    asm_.label(skipCloseLabel);
}

void NativeCodeGen::visit(ScopeStmt& node) {
    // Structured concurrency scope
    // For now, just execute the body
    // TODO: Add proper scope management for concurrent tasks
    node.body->accept(*this);
}

void NativeCodeGen::visit(RequireStmt& node) {
    // Precondition check - panic if condition is false
    std::string okLabel = newLabel("require_ok");
    
    // Evaluate condition
    node.condition->accept(*this);
    
    // Test result
    asm_.test_rax_rax();
    asm_.jnz_rel32(okLabel);
    
    // Condition failed - call panic/exit
    // For now, just exit with error code 1
    asm_.mov_rcx_imm64(1);  // Exit code
    asm_.call_mem_rip(pe_.getImportRVA("ExitProcess"));
    
    asm_.label(okLabel);
}

void NativeCodeGen::visit(EnsureStmt& node) {
    // Postcondition check - same as require for now
    std::string okLabel = newLabel("ensure_ok");
    
    node.condition->accept(*this);
    asm_.test_rax_rax();
    asm_.jnz_rel32(okLabel);
    
    asm_.mov_rcx_imm64(1);
    asm_.call_mem_rip(pe_.getImportRVA("ExitProcess"));
    
    asm_.label(okLabel);
}

void NativeCodeGen::visit(InvariantStmt& node) {
    // Invariant check - same as require for now
    std::string okLabel = newLabel("invariant_ok");
    
    node.condition->accept(*this);
    asm_.test_rax_rax();
    asm_.jnz_rel32(okLabel);
    
    asm_.mov_rcx_imm64(1);
    asm_.call_mem_rip(pe_.getImportRVA("ExitProcess"));
    
    asm_.label(okLabel);
}

void NativeCodeGen::visit(ComptimeBlock& node) {
    // Compile-time execution block
    // Try to evaluate constant expressions at compile time
    // If successful, store results in constVars/constFloatVars
    // If not fully evaluable, fall back to runtime execution
    
    // We handle:
    // 1. Variable declarations with constant initializers
    // 2. Simple arithmetic on constants
    // 3. Pure function calls with constant arguments (len, sizeof, etc.)
    // 4. String operations on constant strings
    
    if (auto* block = dynamic_cast<Block*>(node.body.get())) {
        bool allConstant = true;
        
        for (auto& stmt : block->statements) {
            if (auto* varDecl = dynamic_cast<VarDecl*>(stmt.get())) {
                // Try to evaluate the initializer at compile time
                if (varDecl->initializer) {
                    int64_t intVal;
                    double floatVal;
                    std::string strVal;
                    
                    if (tryEvalConstant(varDecl->initializer.get(), intVal)) {
                        // Store as compile-time constant
                        constVars[varDecl->name] = intVal;
                        varTypes_[varDecl->name] = "int";
                    } else if (tryEvalConstantFloat(varDecl->initializer.get(), floatVal)) {
                        constFloatVars[varDecl->name] = floatVal;
                        varTypes_[varDecl->name] = "float";
                    } else if (tryEvalConstantString(varDecl->initializer.get(), strVal)) {
                        constStrVars[varDecl->name] = strVal;
                        varTypes_[varDecl->name] = "str";
                    } else if (tryEvalComptimeCall(varDecl->initializer.get(), intVal)) {
                        // Compile-time function call evaluation
                        constVars[varDecl->name] = intVal;
                        varTypes_[varDecl->name] = "int";
                    } else {
                        // Can't evaluate at compile time - emit runtime code
                        allConstant = false;
                        stmt->accept(*this);
                    }
                } else {
                    // No initializer - must be runtime
                    allConstant = false;
                    stmt->accept(*this);
                }
            } else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt.get())) {
                // Expression statements in comptime - try to evaluate
                int64_t intVal;
                if (tryEvalConstant(exprStmt->expr.get(), intVal)) {
                    // Constant expression - no code needed
                    // Result is discarded anyway
                } else if (tryEvalComptimeCall(exprStmt->expr.get(), intVal)) {
                    // Compile-time function call - no code needed
                } else {
                    // Can't evaluate - emit runtime code
                    allConstant = false;
                    stmt->accept(*this);
                }
            } else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
                // Try to evaluate if condition at compile time
                int64_t condVal;
                if (tryEvalConstant(ifStmt->condition.get(), condVal)) {
                    // Condition is constant - only emit the taken branch
                    if (condVal) {
                        ifStmt->thenBranch->accept(*this);
                    } else if (ifStmt->elseBranch) {
                        ifStmt->elseBranch->accept(*this);
                    }
                    // Don't mark as non-constant since we handled it
                } else {
                    // Can't evaluate condition - emit full if statement
                    allConstant = false;
                    stmt->accept(*this);
                }
            } else {
                // Other statement types - fall back to runtime
                allConstant = false;
                stmt->accept(*this);
            }
        }
        
        // If all statements were constant, we're done
        // Otherwise, we've already emitted the necessary runtime code
    } else {
        // Single statement body - just execute it
        // Try to evaluate if it's a simple expression
        if (auto* exprStmt = dynamic_cast<ExprStmt*>(node.body.get())) {
            int64_t intVal;
            if (!tryEvalConstant(exprStmt->expr.get(), intVal) && 
                !tryEvalComptimeCall(exprStmt->expr.get(), intVal)) {
                node.body->accept(*this);
            }
        } else {
            node.body->accept(*this);
        }
    }
}

// Helper function to evaluate compile-time function calls
bool NativeCodeGen::tryEvalComptimeCall(Expression* expr, int64_t& outValue) {
    auto* call = dynamic_cast<CallExpr*>(expr);
    if (!call) return false;
    
    auto* id = dynamic_cast<Identifier*>(call->callee.get());
    if (!id) return false;
    
    // First, try to evaluate user-defined comptime functions via CTFE interpreter
    if (ctfe_.isComptimeFunction(id->name)) {
        // Evaluate arguments
        std::vector<CTFEInterpValue> args;
        bool allArgsConst = true;
        
        for (auto& arg : call->args) {
            auto val = ctfe_.evaluateExpr(arg.get());
            if (val) {
                args.push_back(*val);
            } else {
                allArgsConst = false;
                break;
            }
        }
        
        if (allArgsConst) {
            try {
                auto result = ctfe_.evaluateCall(id->name, args);
                if (result) {
                    auto intVal = CTFEInterpreter::toInt(*result);
                    if (intVal) {
                        outValue = *intVal;
                        return true;
                    }
                }
            } catch (const CTFEInterpError& e) {
                // CTFE evaluation failed - fall through to runtime
                (void)e;
            }
        }
    }
    
    // len() on constant strings or lists
    if (id->name == "len" && call->args.size() == 1) {
        std::string strVal;
        if (tryEvalConstantString(call->args[0].get(), strVal)) {
            outValue = static_cast<int64_t>(strVal.length());
            return true;
        }
        // Check for constant list
        if (auto* listId = dynamic_cast<Identifier*>(call->args[0].get())) {
            auto it = constListVars.find(listId->name);
            if (it != constListVars.end()) {
                outValue = static_cast<int64_t>(it->second.size());
                return true;
            }
            auto sizeIt = listSizes.find(listId->name);
            if (sizeIt != listSizes.end()) {
                outValue = static_cast<int64_t>(sizeIt->second);
                return true;
            }
        }
        if (auto* listExpr = dynamic_cast<ListExpr*>(call->args[0].get())) {
            outValue = static_cast<int64_t>(listExpr->elements.size());
            return true;
        }
    }
    
    // abs() on constant integers
    if (id->name == "abs" && call->args.size() == 1) {
        int64_t val;
        if (tryEvalConstant(call->args[0].get(), val)) {
            outValue = val < 0 ? -val : val;
            return true;
        }
    }
    
    // min/max on constant integers
    if (id->name == "min" && call->args.size() == 2) {
        int64_t a, b;
        if (tryEvalConstant(call->args[0].get(), a) && tryEvalConstant(call->args[1].get(), b)) {
            outValue = a < b ? a : b;
            return true;
        }
    }
    if (id->name == "max" && call->args.size() == 2) {
        int64_t a, b;
        if (tryEvalConstant(call->args[0].get(), a) && tryEvalConstant(call->args[1].get(), b)) {
            outValue = a > b ? a : b;
            return true;
        }
    }
    
    // pow on constant integers (small exponents only)
    if (id->name == "pow" && call->args.size() == 2) {
        int64_t base, exp;
        if (tryEvalConstant(call->args[0].get(), base) && tryEvalConstant(call->args[1].get(), exp)) {
            if (exp >= 0 && exp <= 20) {  // Limit to prevent overflow
                int64_t result = 1;
                for (int64_t i = 0; i < exp; i++) {
                    result *= base;
                }
                outValue = result;
                return true;
            }
        }
    }
    
    return false;
}

// Algebraic Effects - Effect Declaration
void NativeCodeGen::visit(EffectDecl& node) {
    // Effect declarations are compile-time only - they define the effect interface
    // No runtime code is generated for the declaration itself
    // The effect operations are implemented via the handler mechanism
    (void)node;
}

// ============================================================================
// Algebraic Effects Runtime Implementation
// ============================================================================
//
// The effect system uses a handler stack approach:
// 1. Each handle block pushes handler entries onto a global stack
// 2. When perform is called, we search the stack for a matching handler
// 3. The handler can either return a value or resume the continuation
//
// Handler Stack Entry Layout (48 bytes):
//   [0-7]   effect_name_hash (uint64_t) - hash of effect name
//   [8-15]  op_name_hash (uint64_t) - hash of operation name  
//   [16-23] handler_addr (ptr) - address of handler code
//   [24-31] resume_addr (ptr) - address to resume after handler (0 if no resume)
//   [32-39] saved_rsp (ptr) - stack pointer when handler was installed
//   [40-47] prev_entry (ptr) - pointer to previous handler entry (linked list)
//
// Global State:
//   effect_handler_top: pointer to top of handler stack (linked list head)
//   effect_result: storage for handler result value
//   effect_resume_value: storage for resume value
// ============================================================================

void NativeCodeGen::emitEffectRuntimeInit() {
    if (effectRuntimeInitialized_) return;
    effectRuntimeInitialized_ = true;
    
    // Allocate space in data section for effect runtime globals
    // We need: handler_top (8 bytes), result (8 bytes), resume_value (8 bytes)
    // Total: 24 bytes
    uint8_t zeros[24] = {0};
    effectHandlerStackRVA_ = pe_.addData(zeros, 24);
}

void NativeCodeGen::emitPushEffectHandler(const std::string& effectName, const std::string& opName,
                                          const std::string& handlerLabel, bool hasResume) {
    // Allocate handler entry on stack (48 bytes)
    asm_.sub_rsp_imm32(48);
    
    // Compute effect name hash
    uint64_t effectHash = 5381;
    for (char c : effectName) {
        effectHash = ((effectHash << 5) + effectHash) + static_cast<uint8_t>(c);
    }
    
    // Compute operation name hash
    uint64_t opHash = 5381;
    for (char c : opName) {
        opHash = ((opHash << 5) + opHash) + static_cast<uint8_t>(c);
    }
    
    // Store effect_name_hash at [rsp+0]
    asm_.mov_rax_imm64(static_cast<int64_t>(effectHash));
    // mov [rsp], rax
    asm_.code.push_back(0x48); asm_.code.push_back(0x89); asm_.code.push_back(0x04); asm_.code.push_back(0x24);
    
    // Store op_name_hash at [rsp+8]
    asm_.mov_rax_imm64(static_cast<int64_t>(opHash));
    // mov [rsp+8], rax
    asm_.code.push_back(0x48); asm_.code.push_back(0x89); asm_.code.push_back(0x44); asm_.code.push_back(0x24); asm_.code.push_back(0x08);
    
    // Store handler_addr at [rsp+16] - use label fixup
    // lea rax, [rip + handler_label]
    asm_.code.push_back(0x48); asm_.code.push_back(0x8D); asm_.code.push_back(0x05);
    asm_.labelFixups.push_back({asm_.code.size(), handlerLabel});
    asm_.code.push_back(0x00); asm_.code.push_back(0x00); asm_.code.push_back(0x00); asm_.code.push_back(0x00);
    // mov [rsp+16], rax
    asm_.code.push_back(0x48); asm_.code.push_back(0x89); asm_.code.push_back(0x44); asm_.code.push_back(0x24); asm_.code.push_back(0x10);
    
    // Store resume_addr at [rsp+24] (0 if no resume)
    if (hasResume && !currentResumeLabel_.empty()) {
        // lea rax, [rip + resume_label]
        asm_.code.push_back(0x48); asm_.code.push_back(0x8D); asm_.code.push_back(0x05);
        asm_.labelFixups.push_back({asm_.code.size(), currentResumeLabel_});
        asm_.code.push_back(0x00); asm_.code.push_back(0x00); asm_.code.push_back(0x00); asm_.code.push_back(0x00);
    } else {
        asm_.xor_rax_rax();
    }
    // mov [rsp+24], rax
    asm_.code.push_back(0x48); asm_.code.push_back(0x89); asm_.code.push_back(0x44); asm_.code.push_back(0x24); asm_.code.push_back(0x18);
    
    // Store saved_rsp at [rsp+32]
    // lea rax, [rsp+48] (original rsp before we allocated)
    asm_.code.push_back(0x48); asm_.code.push_back(0x8D); asm_.code.push_back(0x44); asm_.code.push_back(0x24); asm_.code.push_back(0x30);
    // mov [rsp+32], rax
    asm_.code.push_back(0x48); asm_.code.push_back(0x89); asm_.code.push_back(0x44); asm_.code.push_back(0x24); asm_.code.push_back(0x20);
    
    // Store prev_entry at [rsp+40] - load current top and store
    asm_.lea_rcx_rip_fixup(effectHandlerStackRVA_);
    asm_.mov_rax_mem_rcx();  // rax = current top
    // mov [rsp+40], rax
    asm_.code.push_back(0x48); asm_.code.push_back(0x89); asm_.code.push_back(0x44); asm_.code.push_back(0x24); asm_.code.push_back(0x28);
    
    // Update top to point to this entry
    // mov rax, rsp
    asm_.code.push_back(0x48); asm_.code.push_back(0x89); asm_.code.push_back(0xE0);
    // mov [rcx], rax (rcx still points to effect_handler_top)
    asm_.mov_mem_rcx_rax();
}

void NativeCodeGen::emitPopEffectHandler() {
    // Load prev_entry from [rsp+40]
    // mov rax, [rsp+40]
    asm_.code.push_back(0x48); asm_.code.push_back(0x8B); asm_.code.push_back(0x44); asm_.code.push_back(0x24); asm_.code.push_back(0x28);
    
    // Store to effect_handler_top
    asm_.lea_rcx_rip_fixup(effectHandlerStackRVA_);
    asm_.mov_mem_rcx_rax();
    
    // Deallocate handler entry
    asm_.add_rsp_imm32(48);
}

void NativeCodeGen::emitLookupEffectHandler(const std::string& effectName, const std::string& opName) {
    // Compute hashes
    uint64_t effectHash = 5381;
    for (char c : effectName) {
        effectHash = ((effectHash << 5) + effectHash) + static_cast<uint8_t>(c);
    }
    uint64_t opHash = 5381;
    for (char c : opName) {
        opHash = ((opHash << 5) + opHash) + static_cast<uint8_t>(c);
    }
    
    std::string searchLoop = newLabel("effect_search");
    std::string foundLabel = newLabel("effect_found");
    std::string notFoundLabel = newLabel("effect_not_found");
    std::string nextEntry = newLabel("effect_next");
    
    // Load handler stack top into rax
    asm_.lea_rcx_rip_fixup(effectHandlerStackRVA_);
    asm_.mov_rax_mem_rcx();  // rax = top of handler stack
    
    asm_.label(searchLoop);
    // Check if we've reached the end (null)
    asm_.test_rax_rax();
    asm_.jz_rel32(notFoundLabel);
    
    // Check effect_name_hash at [rax+0]
    asm_.mov_rcx_mem_rax();  // rcx = [rax] = effect_name_hash
    asm_.mov_rdx_imm64(static_cast<int64_t>(effectHash));
    // cmp rcx, rdx
    asm_.code.push_back(0x48); asm_.code.push_back(0x39); asm_.code.push_back(0xD1);
    asm_.jnz_rel32(nextEntry);
    
    // Check op_name_hash at [rax+8]
    // mov rcx, [rax+8]
    asm_.code.push_back(0x48); asm_.code.push_back(0x8B); asm_.code.push_back(0x48); asm_.code.push_back(0x08);
    asm_.mov_rdx_imm64(static_cast<int64_t>(opHash));
    // cmp rcx, rdx
    asm_.code.push_back(0x48); asm_.code.push_back(0x39); asm_.code.push_back(0xD1);
    asm_.jnz_rel32(nextEntry);
    
    // Found! rax points to the handler entry
    asm_.jmp_rel32(foundLabel);
    
    asm_.label(nextEntry);
    // Move to prev_entry at [rax+40]
    // mov rax, [rax+40]
    asm_.code.push_back(0x48); asm_.code.push_back(0x8B); asm_.code.push_back(0x40); asm_.code.push_back(0x28);
    asm_.jmp_rel32(searchLoop);
    
    asm_.label(notFoundLabel);
    // No handler found - return 0 (will use default behavior)
    asm_.xor_rax_rax();
    
    asm_.label(foundLabel);
    // rax = pointer to handler entry (or 0 if not found)
}

void NativeCodeGen::emitEffectDispatch(const std::string& effectName, const std::string& opName, size_t numArgs) {
    // Look up the handler
    emitLookupEffectHandler(effectName, opName);
    
    std::string noHandlerLabel = newLabel("no_handler");
    std::string dispatchDone = newLabel("dispatch_done");
    
    // Check if handler was found
    asm_.test_rax_rax();
    asm_.jz_rel32(noHandlerLabel);
    
    // Handler found - rax points to handler entry
    // Load handler_addr from [rax+16] into rax
    // mov rax, [rax+16]
    asm_.code.push_back(0x48); asm_.code.push_back(0x8B); asm_.code.push_back(0x40); asm_.code.push_back(0x10);
    
    // Call the handler (arguments are already set up by caller)
    // The handler will return its result in RAX
    asm_.call_rax();
    
    asm_.jmp_rel32(dispatchDone);
    
    asm_.label(noHandlerLabel);
    // No handler - return default value (0)
    asm_.xor_rax_rax();
    
    asm_.label(dispatchDone);
}

// Algebraic Effects - Perform Effect Operation
void NativeCodeGen::visit(PerformEffectExpr& node) {
    // Ensure effect runtime is initialized
    emitEffectRuntimeInit();
    
    // Evaluate arguments and save them
    std::vector<std::string> argLocals;
    for (size_t i = 0; i < node.args.size(); i++) {
        node.args[i]->accept(*this);
        std::string argLocal = "$effect_arg_" + std::to_string(i);
        allocLocal(argLocal);
        asm_.mov_mem_rbp_rax(locals[argLocal]);
        argLocals.push_back(argLocal);
    }
    
    // Set up arguments in registers (Windows x64 ABI)
    // First argument (rcx) will be the first effect argument
    if (argLocals.size() >= 1) asm_.mov_rcx_mem_rbp(locals[argLocals[0]]);
    if (argLocals.size() >= 2) asm_.mov_rdx_mem_rbp(locals[argLocals[1]]);
    if (argLocals.size() >= 3) {
        // mov r8, [rbp + offset]
        int32_t offset = locals[argLocals[2]];
        asm_.code.push_back(0x4C); asm_.code.push_back(0x8B);
        if (offset >= -128 && offset <= 127) {
            asm_.code.push_back(0x45); asm_.code.push_back(static_cast<uint8_t>(offset));
        } else {
            asm_.code.push_back(0x85);
            asm_.code.push_back(static_cast<uint8_t>(offset & 0xFF));
            asm_.code.push_back(static_cast<uint8_t>((offset >> 8) & 0xFF));
            asm_.code.push_back(static_cast<uint8_t>((offset >> 16) & 0xFF));
            asm_.code.push_back(static_cast<uint8_t>((offset >> 24) & 0xFF));
        }
    }
    if (argLocals.size() >= 4) {
        // mov r9, [rbp + offset]
        int32_t offset = locals[argLocals[3]];
        asm_.code.push_back(0x4C); asm_.code.push_back(0x8B);
        if (offset >= -128 && offset <= 127) {
            asm_.code.push_back(0x4D); asm_.code.push_back(static_cast<uint8_t>(offset));
        } else {
            asm_.code.push_back(0x8D);
            asm_.code.push_back(static_cast<uint8_t>(offset & 0xFF));
            asm_.code.push_back(static_cast<uint8_t>((offset >> 8) & 0xFF));
            asm_.code.push_back(static_cast<uint8_t>((offset >> 16) & 0xFF));
            asm_.code.push_back(static_cast<uint8_t>((offset >> 24) & 0xFF));
        }
    }
    
    // Dispatch to the effect handler
    emitEffectDispatch(node.effectName, node.opName, node.args.size());
    
    lastExprWasFloat_ = false;
}

// Algebraic Effects - Handle Expression
void NativeCodeGen::visit(HandleExpr& node) {
    // Ensure effect runtime is initialized
    emitEffectRuntimeInit();
    
    // Generate labels for this handle block
    std::string handleEnd = newLabel("handle_end");
    std::string exprDone = newLabel("handle_expr_done");
    currentHandlerEndLabel_ = handleEnd;
    
    // Save current handler depth
    int savedDepth = effectHandlerDepth_;
    
    // For each handler, generate labels
    std::vector<std::string> handlerLabels;
    std::vector<std::string> resumeLabels;
    
    for (size_t i = 0; i < node.handlers.size(); i++) {
        handlerLabels.push_back(newLabel("handler_" + std::to_string(i)));
        resumeLabels.push_back(newLabel("resume_" + std::to_string(i)));
    }
    
    // Push handlers onto the stack (in reverse order so first handler is on top)
    for (int i = static_cast<int>(node.handlers.size()) - 1; i >= 0; i--) {
        const auto& handler = node.handlers[i];
        currentResumeLabel_ = resumeLabels[i];
        emitPushEffectHandler(handler.effectName, handler.opName, 
                              handlerLabels[i], !handler.resumeParam.empty());
        effectHandlerDepth_++;
    }
    
    // Allocate space for the result
    allocLocal("$handle_result");
    
    // Evaluate the main expression - this is where perform calls will dispatch to handlers
    node.expr->accept(*this);
    
    // Store the result
    asm_.mov_mem_rbp_rax(locals["$handle_result"]);
    
    // Jump past the handler code to cleanup
    asm_.jmp_rel32(exprDone);
    
    // Generate handler code for each handler
    // Note: Handlers are called as functions and return their value in RAX
    // They don't have access to the handle block's stack frame
    for (size_t i = 0; i < node.handlers.size(); i++) {
        const auto& handler = node.handlers[i];
        
        asm_.label(handlerLabels[i]);
        
        // Handler is called as a function - set up a minimal stack frame
        // We don't use the handle block's locals, we create our own
        asm_.push_rbp();
        asm_.mov_rbp_rsp();
        asm_.sub_rsp_imm32(0x40);  // Space for local variables
        
        // Save arguments to local stack frame
        // Arguments come in rcx, rdx, r8, r9 (Windows x64 ABI)
        // Store them at [rbp-8], [rbp-16], [rbp-24], [rbp-32]
        if (handler.paramNames.size() >= 1) {
            asm_.mov_mem_rbp_rcx(-8);
        }
        if (handler.paramNames.size() >= 2) {
            asm_.mov_mem_rbp_rdx(-16);
        }
        if (handler.paramNames.size() >= 3) {
            // mov [rbp-24], r8
            asm_.code.push_back(0x4C); asm_.code.push_back(0x89);
            asm_.code.push_back(0x45); asm_.code.push_back(0xE8);  // -24
        }
        if (handler.paramNames.size() >= 4) {
            // mov [rbp-32], r9
            asm_.code.push_back(0x4C); asm_.code.push_back(0x89);
            asm_.code.push_back(0x4D); asm_.code.push_back(0xE0);  // -32
        }
        
        // Execute handler body
        // For simple handlers like "=> 100", the body is an ExprStmt
        // The expression result will be in RAX
        if (handler.body) {
            // Save current locals and create handler-local scope
            auto savedLocals = locals;
            auto savedStackOffset = stackOffset;
            
            // Set up parameter names in the handler's local scope
            stackOffset = -8;
            for (size_t j = 0; j < handler.paramNames.size(); j++) {
                locals[handler.paramNames[j]] = -8 - static_cast<int32_t>(j * 8);
            }
            
            handler.body->accept(*this);
            
            // Restore locals
            locals = savedLocals;
            stackOffset = savedStackOffset;
        }
        
        // Result is in RAX - clean up and return
        asm_.add_rsp_imm32(0x40);
        asm_.pop_rbp();
        asm_.ret();
        
        // Resume point - this is where resume() jumps to
        asm_.label(resumeLabels[i]);
        // For now, resume just continues with the value in RAX
    }
    
    asm_.label(exprDone);
    
    // Pop all handlers from the stack (cleanup)
    for (size_t i = 0; i < node.handlers.size(); i++) {
        emitPopEffectHandler();
        effectHandlerDepth_--;
    }
    
    // Load the final result
    asm_.mov_rax_mem_rbp(locals["$handle_result"]);
    
    asm_.label(handleEnd);
    
    // Restore handler depth
    effectHandlerDepth_ = savedDepth;
    currentHandlerEndLabel_.clear();
    
    lastExprWasFloat_ = false;
}

// Algebraic Effects - Resume Expression
void NativeCodeGen::visit(ResumeExpr& node) {
    // Evaluate the resume value
    if (node.value) {
        node.value->accept(*this);
    } else {
        asm_.xor_rax_rax();  // Resume with 0/nil
    }
    
    // The resume value is now in RAX
    // In a full implementation, we would:
    // 1. Look up the saved continuation from the handler context
    // 2. Restore the stack to the saved state
    // 3. Jump to the resume point with the value in RAX
    
    // For our implementation, resume is called within a handler body
    // and the value in RAX will be used as the handler's return value
    // The handler will then return, and execution continues after the perform
    
    lastExprWasFloat_ = false;
}


} // namespace tyl