// Flex Compiler - Native Code Generator Declaration Visitors
// Handles: functions, records, enums, traits, impl, extern, macros, program

#include "codegen_base.h"

namespace flex {

// Check if an expression contains any function calls
bool NativeCodeGen::expressionHasCall(Expression* expr) {
    if (!expr) return false;
    
    if (dynamic_cast<CallExpr*>(expr)) return true;
    
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        return expressionHasCall(binary->left.get()) || expressionHasCall(binary->right.get());
    }
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        return expressionHasCall(unary->operand.get());
    }
    if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        return expressionHasCall(ternary->condition.get()) ||
               expressionHasCall(ternary->thenExpr.get()) ||
               expressionHasCall(ternary->elseExpr.get());
    }
    if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        return expressionHasCall(index->object.get()) || expressionHasCall(index->index.get());
    }
    if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        return expressionHasCall(member->object.get());
    }
    
    return false;
}

// Check if a statement contains any function calls
bool NativeCodeGen::statementHasCall(Statement* stmt) {
    if (!stmt) return false;
    
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            if (statementHasCall(s.get())) return true;
        }
        return false;
    }
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        return expressionHasCall(exprStmt->expr.get());
    }
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        return expressionHasCall(varDecl->initializer.get());
    }
    if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        return expressionHasCall(assignStmt->value.get());
    }
    if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        if (expressionHasCall(ifStmt->condition.get())) return true;
        if (statementHasCall(ifStmt->thenBranch.get())) return true;
        for (auto& elif : ifStmt->elifBranches) {
            if (expressionHasCall(elif.first.get())) return true;
            if (statementHasCall(elif.second.get())) return true;
        }
        if (statementHasCall(ifStmt->elseBranch.get())) return true;
        return false;
    }
    if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        return expressionHasCall(whileStmt->condition.get()) || statementHasCall(whileStmt->body.get());
    }
    if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
        return expressionHasCall(forStmt->iterable.get()) || statementHasCall(forStmt->body.get());
    }
    if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        return expressionHasCall(returnStmt->value.get());
    }
    
    return false;
}

// Check if a function is a leaf function (makes no calls)
bool NativeCodeGen::checkIsLeafFunction(Statement* body) {
    return !statementHasCall(body);
}

// Emit code to save callee-saved registers that we're using
void NativeCodeGen::emitSaveCalleeSavedRegs() {
    // RDI is used for stdout caching and must be saved/restored
    if (useStdoutCaching_) {
        asm_.push_rdi();
    }
    
    auto usedRegs = regAlloc_.getUsedRegisters();
    for (auto reg : usedRegs) {
        switch (reg) {
            case VarRegister::RBX: asm_.push_rbx(); break;
            case VarRegister::R12: asm_.push_r12(); break;
            case VarRegister::R13: asm_.push_r13(); break;
            case VarRegister::R14: asm_.push_r14(); break;
            case VarRegister::R15: asm_.push_r15(); break;
            default: break;
        }
    }
}

// Emit code to restore callee-saved registers (in reverse order)
void NativeCodeGen::emitRestoreCalleeSavedRegs() {
    auto usedRegs = regAlloc_.getUsedRegisters();
    for (auto it = usedRegs.rbegin(); it != usedRegs.rend(); ++it) {
        switch (*it) {
            case VarRegister::RBX: asm_.pop_rbx(); break;
            case VarRegister::R12: asm_.pop_r12(); break;
            case VarRegister::R13: asm_.pop_r13(); break;
            case VarRegister::R14: asm_.pop_r14(); break;
            case VarRegister::R15: asm_.pop_r15(); break;
            default: break;
        }
    }
    
    // Restore RDI (stdout handle cache)
    if (useStdoutCaching_) {
        asm_.pop_rdi();
    }
}

// Load a variable into RAX (from register or stack)
void NativeCodeGen::emitLoadVarToRax(const std::string& name) {
    // Check if variable is in a register
    auto it = varRegisters_.find(name);
    if (it != varRegisters_.end() && it->second != VarRegister::NONE) {
        switch (it->second) {
            case VarRegister::RBX: asm_.mov_rax_rbx(); break;
            case VarRegister::R12: asm_.mov_rax_r12(); break;
            case VarRegister::R13: asm_.mov_rax_r13(); break;
            case VarRegister::R14: asm_.mov_rax_r14(); break;
            case VarRegister::R15: asm_.mov_rax_r15(); break;
            default: break;
        }
        return;
    }
    
    // Fall back to stack
    auto stackIt = locals.find(name);
    if (stackIt != locals.end()) {
        asm_.mov_rax_mem_rbp(stackIt->second);
    }
}

// Store RAX to a variable (to register or stack)
void NativeCodeGen::emitStoreRaxToVar(const std::string& name) {
    // Check if variable is in a register
    auto it = varRegisters_.find(name);
    if (it != varRegisters_.end() && it->second != VarRegister::NONE) {
        switch (it->second) {
            case VarRegister::RBX: asm_.mov_rbx_rax(); break;
            case VarRegister::R12: asm_.mov_r12_rax(); break;
            case VarRegister::R13: asm_.mov_r13_rax(); break;
            case VarRegister::R14: asm_.mov_r14_rax(); break;
            case VarRegister::R15: asm_.mov_r15_rax(); break;
            default: break;
        }
        return;
    }
    
    // Fall back to stack
    auto stackIt = locals.find(name);
    if (stackIt != locals.end()) {
        asm_.mov_mem_rbp_rax(stackIt->second);
    } else {
        // Allocate new stack slot
        allocLocal(name);
        asm_.mov_mem_rbp_rax(locals[name]);
    }
}

// Move a parameter register to a variable location
void NativeCodeGen::emitMoveParamToVar(int paramIndex, const std::string& name) {
    auto it = varRegisters_.find(name);
    VarRegister targetReg = (it != varRegisters_.end()) ? it->second : VarRegister::NONE;
    
    if (targetReg != VarRegister::NONE) {
        // Move param register directly to allocated register
        switch (paramIndex) {
            case 0: // RCX
                switch (targetReg) {
                    case VarRegister::RBX: asm_.mov_rbx_rcx(); break;
                    case VarRegister::R12: asm_.mov_r12_rcx(); break;
                    case VarRegister::R13: asm_.mov_r13_rcx(); break;
                    case VarRegister::R14: asm_.mov_r14_rcx(); break;
                    case VarRegister::R15: asm_.mov_r15_rcx(); break;
                    default: break;
                }
                break;
            case 1: // RDX
                switch (targetReg) {
                    case VarRegister::RBX: asm_.mov_rbx_rdx(); break;
                    case VarRegister::R12: asm_.mov_r12_rdx(); break;
                    case VarRegister::R13: asm_.mov_r13_rdx(); break;
                    case VarRegister::R14: asm_.mov_r14_rdx(); break;
                    case VarRegister::R15: asm_.mov_r15_rdx(); break;
                    default: break;
                }
                break;
            case 2: // R8
                switch (targetReg) {
                    case VarRegister::RBX: asm_.mov_rbx_r8(); break;
                    case VarRegister::R12: asm_.mov_r12_r8(); break;
                    case VarRegister::R13: asm_.mov_r13_r8(); break;
                    case VarRegister::R14: asm_.mov_r14_r8(); break;
                    case VarRegister::R15: asm_.mov_r15_r8(); break;
                    default: break;
                }
                break;
            case 3: // R9
                switch (targetReg) {
                    case VarRegister::RBX: asm_.mov_rbx_r9(); break;
                    case VarRegister::R12: asm_.mov_r12_r9(); break;
                    case VarRegister::R13: asm_.mov_r13_r9(); break;
                    case VarRegister::R14: asm_.mov_r14_r9(); break;
                    case VarRegister::R15: asm_.mov_r15_r9(); break;
                    default: break;
                }
                break;
        }
    } else {
        // Store to stack
        allocLocal(name);
        int32_t off = locals[name];
        switch (paramIndex) {
            case 0: asm_.mov_mem_rbp_rcx(off); break;
            case 1:
                asm_.code.push_back(0x48); asm_.code.push_back(0x89);
                asm_.code.push_back(0x95);
                asm_.code.push_back(off & 0xFF);
                asm_.code.push_back((off >> 8) & 0xFF);
                asm_.code.push_back((off >> 16) & 0xFF);
                asm_.code.push_back((off >> 24) & 0xFF);
                break;
            case 2:
                asm_.code.push_back(0x4C); asm_.code.push_back(0x89);
                asm_.code.push_back(0x85);
                asm_.code.push_back(off & 0xFF);
                asm_.code.push_back((off >> 8) & 0xFF);
                asm_.code.push_back((off >> 16) & 0xFF);
                asm_.code.push_back((off >> 24) & 0xFF);
                break;
            case 3:
                asm_.code.push_back(0x4C); asm_.code.push_back(0x89);
                asm_.code.push_back(0x8D);
                asm_.code.push_back(off & 0xFF);
                asm_.code.push_back((off >> 8) & 0xFF);
                asm_.code.push_back((off >> 16) & 0xFF);
                asm_.code.push_back((off >> 24) & 0xFF);
                break;
        }
    }
}

// Helper to collect nested functions from a statement
void collectNestedFunctions(Statement* stmt, std::vector<FnDecl*>& nested) {
    if (!stmt) return;
    
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            if (auto* fn = dynamic_cast<FnDecl*>(s.get())) {
                nested.push_back(fn);
            }
            // Don't recurse into nested functions - they handle their own nesting
        }
    }
}

void NativeCodeGen::visit(FnDecl& node) {
    std::map<std::string, int32_t> savedLocals = locals;
    std::map<std::string, std::string> savedConstStrVars = constStrVars;
    int32_t savedStackOffset = stackOffset;
    bool savedInFunction = inFunction;
    int32_t savedFunctionStackSize = functionStackSize_;
    bool savedStackAllocated = stackAllocated_;
    std::map<std::string, VarRegister> savedVarRegisters = varRegisters_;
    bool savedIsLeaf = isLeafFunction_;
    
    bool savedStdoutCached = stdoutHandleCached_;
    
    // Collect nested functions from the body block
    std::vector<FnDecl*> nestedFunctions;
    if (auto* block = dynamic_cast<Block*>(node.body.get())) {
        collectNestedFunctions(block, nestedFunctions);
    }
    
    // Register nested function labels BEFORE we process the body
    for (auto* nested : nestedFunctions) {
        // Don't set to 0 - just ensure the label exists for fixup
        if (asm_.labels.find(nested->name) == asm_.labels.end()) {
            asm_.labels[nested->name] = 0xFFFFFFFF;  // Placeholder, will be set when function is emitted
        }
    }
    
    inFunction = true;
    locals.clear();
    stackOffset = 0;
    stackAllocated_ = false;
    varRegisters_.clear();
    // Don't reset stdoutHandleCached_ - we want to preserve RDI across function calls
    
    // Check if this is a leaf function (excluding nested function calls which are now separate)
    isLeafFunction_ = useLeafOptimization_ && checkIsLeafFunction(node.body.get());
    
    // Perform register allocation for this function
    if (useRegisterAllocation_) {
        regAlloc_.analyze(node);
        // Copy register assignments
        for (const auto& range : regAlloc_.getLiveRanges()) {
            if (range.reg != VarRegister::NONE) {
                varRegisters_[range.name] = range.reg;
            }
        }
    }
    
    // Calculate total stack size needed for this function
    int32_t baseStack = 0x80;
    int32_t callStack = calculateFunctionStackSize(node.body.get());
    
    // For leaf functions, we need much less stack space
    if (isLeafFunction_) {
        // Only need space for spilled variables (if any)
        // Count variables that didn't get registers
        int spillCount = 0;
        for (size_t i = 0; i < node.params.size(); i++) {
            if (varRegisters_.find(node.params[i].first) == varRegisters_.end() ||
                varRegisters_[node.params[i].first] == VarRegister::NONE) {
                spillCount++;
            }
        }
        // Minimal stack: just for spills + alignment
        baseStack = std::max(0x20, spillCount * 8 + 0x10);
        callStack = 0;  // No calls in leaf function
    }
    
    functionStackSize_ = ((baseStack + callStack + 0x28 + 15) / 16) * 16;
    
    asm_.label(node.name);
    
    // Function prologue
    if (isLeafFunction_ && varRegisters_.size() == node.params.size() && node.params.size() <= 4) {
        // Ultra-minimal leaf function: parameters stay in registers, no stack frame needed
        // Save callee-saved registers (includes RDI for stdout caching)
        emitSaveCalleeSavedRegs();
        
        // Move parameters to their allocated registers
        for (size_t i = 0; i < node.params.size() && i < 4; i++) {
            constStrVars[node.params[i].first] = "";
            emitMoveParamToVar((int)i, node.params[i].first);
        }
        
        stackAllocated_ = false;  // No stack frame
    } else {
        // Standard prologue
        asm_.push_rbp();
        asm_.mov_rbp_rsp();
        
        // Save callee-saved registers (includes RDI for stdout caching)
        emitSaveCalleeSavedRegs();
        
        // Allocate stack space
        asm_.sub_rsp_imm32(functionStackSize_);
        stackAllocated_ = true;
        
        // Store parameters
        for (size_t i = 0; i < node.params.size() && i < 4; i++) {
            constStrVars[node.params[i].first] = "";
            emitMoveParamToVar((int)i, node.params[i].first);
        }
    }
    
    node.body->accept(*this);
    
    // Function epilogue - only emit if body doesn't end with a return
    // (ReturnStmt handles its own epilogue)
    if (!endsWithTerminator(node.body.get())) {
        asm_.xor_rax_rax();
        
        if (isLeafFunction_ && varRegisters_.size() == node.params.size() && node.params.size() <= 4) {
            // Minimal epilogue for ultra-minimal leaf
            emitRestoreCalleeSavedRegs();
        } else {
            // Standard epilogue
            // 1. Deallocate stack space
            asm_.add_rsp_imm32(functionStackSize_);
            
            // 2. Restore callee-saved registers (includes RDI)
            emitRestoreCalleeSavedRegs();
            
            // 3. Restore rbp
            asm_.pop_rbp();
        }
        
        asm_.ret();
    }
    
    // Restore state
    locals = savedLocals;
    constStrVars = savedConstStrVars;
    stackOffset = savedStackOffset;
    inFunction = savedInFunction;
    functionStackSize_ = savedFunctionStackSize;
    stackAllocated_ = savedStackAllocated;
    varRegisters_ = savedVarRegisters;
    isLeafFunction_ = savedIsLeaf;
    stdoutHandleCached_ = savedStdoutCached;
    
    // Now emit nested functions AFTER the parent function
    for (auto* nested : nestedFunctions) {
        nested->accept(*this);
    }
}

void NativeCodeGen::visit(RecordDecl& node) { (void)node; }
void NativeCodeGen::visit(UseStmt& node) { (void)node; }

void NativeCodeGen::visit(ModuleDecl& node) {
    // Save current module context
    std::string savedModule = currentModule_;
    currentModule_ = node.name;
    
    // Collect all functions in this module
    std::vector<FnDecl*> moduleFns;
    for (auto& stmt : node.body) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            // Rename function to module.function format
            std::string mangledName = node.name + "." + fn->name;
            moduleFunctions_[node.name].push_back(fn->name);
            
            // Store original name and set mangled name
            std::string originalName = fn->name;
            fn->name = mangledName;
            
            // Register the label
            asm_.labels[mangledName] = 0;
            
            moduleFns.push_back(fn);
        }
    }
    
    // Compile all functions in the module (they will be emitted later in Program)
    // Don't emit here - just register them
    
    // Restore module context
    currentModule_ = savedModule;
}

void NativeCodeGen::visit(EnumDecl& node) { (void)node; }
void NativeCodeGen::visit(TypeAlias& node) { (void)node; }

void NativeCodeGen::visit(TraitDecl& node) {
    // Register the trait and its methods
    TraitInfo info;
    info.name = node.name;
    for (auto& method : node.methods) {
        info.methodNames.push_back(method->name);
    }
    traits_[node.name] = info;
}

void NativeCodeGen::visit(ImplBlock& node) {
    // Generate method implementations with mangled names
    std::string implKey = node.traitName + ":" + node.typeName;
    ImplInfo info;
    info.traitName = node.traitName;
    info.typeName = node.typeName;
    
    for (auto& method : node.methods) {
        // Mangle method name: Type_Trait_method or Type_method
        std::string mangledName;
        if (!node.traitName.empty()) {
            mangledName = node.typeName + "_" + node.traitName + "_" + method->name;
        } else {
            mangledName = node.typeName + "_" + method->name;
        }
        
        // Store original name, generate with mangled name
        std::string originalName = method->name;
        method->name = mangledName;
        method->accept(*this);
        method->name = originalName;  // Restore
        
        info.methodLabels[originalName] = mangledName;
    }
    
    impls_[implKey] = info;
    
    // If this is a trait impl, generate vtable
    if (!node.traitName.empty() && traits_.count(node.traitName)) {
        auto& trait = traits_[node.traitName];
        
        // Build vtable: array of function pointers
        std::vector<uint8_t> vtableData;
        for (auto& methodName : trait.methodNames) {
            // We'll need to patch these addresses later
            // For now, store placeholder (will be resolved by linker)
            for (int i = 0; i < 8; i++) {
                vtableData.push_back(0);
            }
        }
        
        // Add vtable to data section
        if (!vtableData.empty()) {
            uint32_t vtableRVA = pe_.addData(vtableData.data(), vtableData.size());
            vtables_[implKey] = vtableRVA;
        }
    }
}

void NativeCodeGen::visit(UnsafeBlock& node) {
    node.body->accept(*this);
}

void NativeCodeGen::visit(ImportStmt& node) { (void)node; }

void NativeCodeGen::visit(ExternDecl& node) {
    // Add imports for external functions
    for (auto& fn : node.functions) {
        // Add the import to the PE file
        pe_.addImport(node.library, fn->name);
        // Store the function name - we'll get the RVA later via getImportRVA
        externFunctions_[fn->name] = 0;  // Placeholder, will use getImportRVA at call time
    }
}
void NativeCodeGen::visit(MacroDecl& node) { (void)node; }
void NativeCodeGen::visit(SyntaxMacroDecl& node) { (void)node; }
void NativeCodeGen::visit(LayerDecl& node) { (void)node; }

void NativeCodeGen::visit(Program& node) {
    std::vector<FnDecl*> functions;
    std::vector<Statement*> topLevelStmts;
    std::vector<ModuleDecl*> modules;
    std::vector<ExternDecl*> externs;
    FnDecl* mainFn = nullptr;
    
    for (auto& stmt : node.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            functions.push_back(fn);
            if (fn->name == "main") {
                mainFn = fn;
            }
        } else if (auto* mod = dynamic_cast<ModuleDecl*>(stmt.get())) {
            modules.push_back(mod);
        } else if (auto* ext = dynamic_cast<ExternDecl*>(stmt.get())) {
            externs.push_back(ext);
        } else {
            bool isMainCall = false;
            if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt.get())) {
                if (auto* call = dynamic_cast<CallExpr*>(exprStmt->expr.get())) {
                    if (auto* id = dynamic_cast<Identifier*>(call->callee.get())) {
                        if (id->name == "main") {
                            isMainCall = true;
                        }
                    }
                }
            }
            if (!isMainCall) {
                topLevelStmts.push_back(stmt.get());
            }
        }
    }
    
    // Process extern declarations first (to register imports)
    for (auto* ext : externs) {
        ext->accept(*this);
    }
    
    // Process modules to collect and rename functions
    for (auto* mod : modules) {
        mod->accept(*this);
    }
    
    // Perform global register allocation for top-level variables
    if (useGlobalRegisterAllocation_) {
        globalRegAlloc_.analyze(node);
        
        // Copy global register assignments
        for (const auto& [name, info] : globalRegAlloc_.getGlobalVars()) {
            if (info.assignedReg != VarRegister::NONE) {
                globalVarRegisters_[name] = info.assignedReg;
            }
        }
    }
    
    // Register all function labels
    for (auto* fn : functions) {
        asm_.labels[fn->name] = 0;
    }
    
    // Register module function labels
    for (auto* mod : modules) {
        for (auto& stmt : mod->body) {
            if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
                // The name was already mangled in visit(ModuleDecl)
                asm_.labels[fn->name] = 0;
            }
        }
    }
    
    asm_.label("_start");
    asm_.push_rbp();
    asm_.mov_rbp_rsp();
    
    // Save callee-saved registers used for global variables
    int numPushedRegs = 0;  // Count pushed registers for stack alignment
    if (useGlobalRegisterAllocation_) {
        auto usedGlobalRegs = globalRegAlloc_.getUsedGlobalRegisters();
        for (auto reg : usedGlobalRegs) {
            switch (reg) {
                case VarRegister::RBX: asm_.push_rbx(); numPushedRegs++; break;
                case VarRegister::R12: asm_.push_r12(); numPushedRegs++; break;
                case VarRegister::R13: asm_.push_r13(); numPushedRegs++; break;
                case VarRegister::R14: asm_.push_r14(); numPushedRegs++; break;
                case VarRegister::R15: asm_.push_r15(); numPushedRegs++; break;
                default: break;
            }
        }
    }
    
    // Calculate total stack needed for _start (top-level code)
    // This includes space for locals + shadow space for all calls
    // We allocate once here and set stackAllocated_ = true to prevent
    // per-call sub rsp/add rsp pairs (the "stack pump" problem)
    int32_t topLevelStackSize = 0x100;  // Base allocation for locals
    
    // Scan top-level statements to find max call stack requirement
    for (auto* stmt : topLevelStmts) {
        topLevelStackSize = std::max(topLevelStackSize, 
            0x100 + calculateFunctionStackSize(stmt));
    }
    
    // Round up to 16-byte alignment and ensure we have enough for shadow space
    // Account for: return address (1) + rbp (1) + callee-saved regs (numPushedRegs)
    // Total pushes = 2 + numPushedRegs
    // If total pushes is odd, we need stack allocation to be 8 mod 16 for alignment
    // If total pushes is even, we need stack allocation to be 0 mod 16 for alignment
    topLevelStackSize = ((topLevelStackSize + 0x38 + 15) / 16) * 16;
    
    // Adjust for stack alignment based on number of pushed registers
    // After pushes, RSP is at: original - 8*(2 + numPushedRegs)
    // For 16-byte alignment before calls, we need RSP to be 16-byte aligned
    // If (2 + numPushedRegs) is odd, RSP is 8-byte aligned, need odd multiple of 8 for stack
    // If (2 + numPushedRegs) is even, RSP is 16-byte aligned, need even multiple of 8 for stack
    if ((2 + numPushedRegs) % 2 == 1) {
        // Odd number of pushes, RSP is 8-byte aligned
        // Make sure stack allocation is 8 mod 16 to get 16-byte alignment
        if (topLevelStackSize % 16 == 0) {
            topLevelStackSize += 8;
        }
    } else {
        // Even number of pushes, RSP is 16-byte aligned
        // Make sure stack allocation is 0 mod 16 to keep 16-byte alignment
        // (already done by the rounding above)
    }
    
    asm_.sub_rsp_imm32(topLevelStackSize);
    stackAllocated_ = true;  // FIX: Mark stack as allocated to prevent per-call adjustments
    functionStackSize_ = topLevelStackSize;
    
    // Copy global register assignments to varRegisters_ for use in codegen
    varRegisters_ = globalVarRegisters_;
    
    for (auto* stmt : topLevelStmts) {
        stmt->accept(*this);
    }
    
    if (mainFn) {
        // Stack is already allocated, just call
        asm_.call_rel32("main");
    } else {
        asm_.xor_rax_rax();
    }
    
    asm_.mov_rcx_rax();
    // Stack is already allocated, just call ExitProcess
    // ExitProcess never returns, so no ret needed
    asm_.call_mem_rip(pe_.getImportRVA("ExitProcess"));
    // No ret - ExitProcess terminates the process
    
    // Reset for function compilation
    stackAllocated_ = false;
    functionStackSize_ = 0;
    varRegisters_.clear();
    
    // Emit top-level functions
    for (auto* fn : functions) {
        fn->accept(*this);
    }
    
    // Emit module functions
    for (auto* mod : modules) {
        for (auto& stmt : mod->body) {
            if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
                fn->accept(*this);
            }
        }
    }
}

} // namespace flex
