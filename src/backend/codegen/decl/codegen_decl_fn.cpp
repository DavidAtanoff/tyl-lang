// Tyl Compiler - Native Code Generator Function Declarations
// Handles: FnDecl, register allocation helpers

#include "backend/codegen/codegen_base.h"

namespace tyl {

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

bool NativeCodeGen::checkIsLeafFunction(Statement* body) {
    return !statementHasCall(body);
}

void NativeCodeGen::emitSaveCalleeSavedRegs() {
    if (useStdoutCaching_) {
        asm_.push_rdi();
        stackOffset -= 8;
    }
    
    auto usedRegs = regAlloc_.getUsedRegisters();
    for (auto reg : usedRegs) {
        switch (reg) {
            case VarRegister::RBX: asm_.push_rbx(); stackOffset -= 8; break;
            case VarRegister::R12: asm_.push_r12(); stackOffset -= 8; break;
            case VarRegister::R13: asm_.push_r13(); stackOffset -= 8; break;
            case VarRegister::R14: asm_.push_r14(); stackOffset -= 8; break;
            case VarRegister::R15: asm_.push_r15(); stackOffset -= 8; break;
            default: break;
        }
    }
}

void NativeCodeGen::emitRestoreCalleeSavedRegs() {
    auto usedRegs = regAlloc_.getUsedRegisters();
    for (auto it = usedRegs.rbegin(); it != usedRegs.rend(); ++it) {
        switch (*it) {
            case VarRegister::RBX: asm_.pop_rbx(); stackOffset += 8; break;
            case VarRegister::R12: asm_.pop_r12(); stackOffset += 8; break;
            case VarRegister::R13: asm_.pop_r13(); stackOffset += 8; break;
            case VarRegister::R14: asm_.pop_r14(); stackOffset += 8; break;
            case VarRegister::R15: asm_.pop_r15(); stackOffset += 8; break;
            default: break;
        }
    }
    
    if (useStdoutCaching_) {
        asm_.pop_rdi();
        stackOffset += 8;
    }
}

void NativeCodeGen::emitLoadVarToRax(const std::string& name) {
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
    
    auto stackIt = locals.find(name);
    if (stackIt != locals.end()) {
        asm_.mov_rax_mem_rbp(stackIt->second);
    }
}

void NativeCodeGen::emitStoreRaxToVar(const std::string& name) {
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
    
    auto stackIt = locals.find(name);
    if (stackIt != locals.end()) {
        asm_.mov_mem_rbp_rax(stackIt->second);
    } else {
        allocLocal(name);
        asm_.mov_mem_rbp_rax(locals[name]);
    }
}

void NativeCodeGen::emitMoveParamToVar(int paramIndex, const std::string& name, const std::string& type) {
    auto it = varRegisters_.find(name);
    VarRegister targetReg = (it != varRegisters_.end()) ? it->second : VarRegister::NONE;
    
    bool isFloat = isFloatTypeName(type);
    
    if (targetReg != VarRegister::NONE) {
        if (isFloat) {
            // Move from XMM to generic register (as integer bits)
            // Or if target is allocated to XMM... but we only track GP registers in varRegisters_ generally.
            // For now, we assume varRegisters_ maps to GP regs (RBX, R12-R15).
            // We need to move XMM -> GP.
            switch (paramIndex) {
                case 0: asm_.movq_rax_xmm0(); break;
                case 1: 
                    asm_.code.push_back(0x66); asm_.code.push_back(0x48);
                    asm_.code.push_back(0x0F); asm_.code.push_back(0x7E); asm_.code.push_back(0xC8); // movq rax, xmm1
                    break;
                case 2:
                    asm_.code.push_back(0x66); asm_.code.push_back(0x48);
                    asm_.code.push_back(0x0F); asm_.code.push_back(0x7E); asm_.code.push_back(0xD0); // movq rax, xmm2
                    break;
                case 3:
                    asm_.code.push_back(0x66); asm_.code.push_back(0x48);
                    asm_.code.push_back(0x0F); asm_.code.push_back(0x7E); asm_.code.push_back(0xD8); // movq rax, xmm3
                    break;
            }
            
            // Move RAX to target GP register
            switch (targetReg) {
                case VarRegister::RBX: asm_.mov_rbx_rax(); break;
                case VarRegister::R12: asm_.mov_r12_rax(); break;
                case VarRegister::R13: asm_.mov_r13_rax(); break;
                case VarRegister::R14: asm_.mov_r14_rax(); break;
                case VarRegister::R15: asm_.mov_r15_rax(); break;
                default: break;
            }
        } else {
            // Integer/Pointer parameters (RCX, RDX, R8, R9)
            switch (paramIndex) {
                case 0:
                    switch (targetReg) {
                        case VarRegister::RBX: asm_.mov_rbx_rcx(); break;
                        case VarRegister::R12: asm_.mov_r12_rcx(); break;
                        case VarRegister::R13: asm_.mov_r13_rcx(); break;
                        case VarRegister::R14: asm_.mov_r14_rcx(); break;
                        case VarRegister::R15: asm_.mov_r15_rcx(); break;
                        default: break;
                    }
                    break;
                case 1:
                    switch (targetReg) {
                        case VarRegister::RBX: asm_.mov_rbx_rdx(); break;
                        case VarRegister::R12: asm_.mov_r12_rdx(); break;
                        case VarRegister::R13: asm_.mov_r13_rdx(); break;
                        case VarRegister::R14: asm_.mov_r14_rdx(); break;
                        case VarRegister::R15: asm_.mov_r15_rdx(); break;
                        default: break;
                    }
                    break;
                case 2:
                    switch (targetReg) {
                        case VarRegister::RBX: asm_.mov_rbx_r8(); break;
                        case VarRegister::R12: asm_.mov_r12_r8(); break;
                        case VarRegister::R13: asm_.mov_r13_r8(); break;
                        case VarRegister::R14: asm_.mov_r14_r8(); break;
                        case VarRegister::R15: asm_.mov_r15_r8(); break;
                        default: break;
                    }
                    break;
                case 3:
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
        }
    } else {
        // Stack allocation
        auto it = locals.find(name);
        if (it == locals.end()) {
            allocLocal(name);
        }
        int32_t off = locals[name];
        
        if (isFloat) {
            // Store XMM register to stack
            switch (paramIndex) {
                case 0: asm_.movsd_mem_rbp_xmm0(off); break;
                case 1: 
                    // movsd [rbp+off], xmm1
                    asm_.code.push_back(0xF2); asm_.code.push_back(0x0F);
                    asm_.code.push_back(0x11); asm_.code.push_back(0x8D);
                    asm_.code.push_back(off & 0xFF);
                    asm_.code.push_back((off >> 8) & 0xFF);
                    asm_.code.push_back((off >> 16) & 0xFF);
                    asm_.code.push_back((off >> 24) & 0xFF);
                    break;
                case 2:
                    // movsd [rbp+off], xmm2
                    asm_.code.push_back(0xF2); asm_.code.push_back(0x0F);
                    asm_.code.push_back(0x11); asm_.code.push_back(0x95);
                    asm_.code.push_back(off & 0xFF);
                    asm_.code.push_back((off >> 8) & 0xFF);
                    asm_.code.push_back((off >> 16) & 0xFF);
                    asm_.code.push_back((off >> 24) & 0xFF);
                    break;
                case 3:
                     // movsd [rbp+off], xmm3
                    asm_.code.push_back(0xF2); asm_.code.push_back(0x0F);
                    asm_.code.push_back(0x11); asm_.code.push_back(0x9D);
                    asm_.code.push_back(off & 0xFF);
                    asm_.code.push_back((off >> 8) & 0xFF);
                    asm_.code.push_back((off >> 16) & 0xFF);
                    asm_.code.push_back((off >> 24) & 0xFF);
                    break;
            }
        } else {
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
}

// Helper to collect nested functions from a statement
static void collectNestedFunctions(Statement* stmt, std::vector<FnDecl*>& nested) {
    if (!stmt) return;
    
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            if (auto* fn = dynamic_cast<FnDecl*>(s.get())) {
                nested.push_back(fn);
            }
        }
    }
}

void NativeCodeGen::visit(FnDecl& node) {
    // Skip comptime functions - they are evaluated at compile time, not emitted as code
    if (node.isComptime) {
        // Register with CTFE interpreter for compile-time evaluation
        ctfe_.registerComptimeFunction(&node);
        comptimeFunctions_.insert(node.name);
        return;
    }
    
    std::map<std::string, int32_t> savedLocals = locals;
    std::map<std::string, std::string> savedConstStrVars = constStrVars;
    std::map<std::string, std::string> savedVarRecordTypes = varRecordTypes_;
    int32_t savedStackOffset = stackOffset;
    bool savedInFunction = inFunction;
    int32_t savedFunctionStackSize = functionStackSize_;
    bool savedStackAllocated = stackAllocated_;
    std::map<std::string, VarRegister> savedVarRegisters = varRegisters_;
    bool savedIsLeaf = isLeafFunction_;
    bool savedStdoutCached = stdoutHandleCached_;
    std::map<std::string, std::string> savedBorrowParams = borrowParams_;
    std::string savedReturnType = currentFnReturnType_;
    std::set<std::string> savedFnPtrVars = fnPtrVars_;
    std::set<std::string> savedClosureVars = closureVars_;
    
    std::vector<FnDecl*> nestedFunctions;
    if (auto* block = dynamic_cast<Block*>(node.body.get())) {
        collectNestedFunctions(block, nestedFunctions);
    }
    
    for (auto* nested : nestedFunctions) {
        if (asm_.labels.find(nested->name) == asm_.labels.end()) {
            asm_.labels[nested->name] = 0xFFFFFFFF;
        }
    }
    
    inFunction = true;
    locals.clear();
    varRecordTypes_.clear();
    borrowParams_.clear();
    fnPtrVars_.clear();
    closureVars_.clear();
    currentFnReturnType_ = node.returnType;
    stackOffset = 0;
    stackAllocated_ = false;
    varRegisters_.clear();
    
    isLeafFunction_ = useLeafOptimization_ && checkIsLeafFunction(node.body.get());
    
    // Track calling convention for this function
    fnCallingConvs_[node.name] = node.callingConv;
    
    // Track export/visibility attributes
    FnAttributes attrs;
    attrs.isExport = node.isExport;
    attrs.isHidden = node.isHidden;
    attrs.isWeak = node.isWeak;
    fnAttributes_[node.name] = attrs;
    
    if (useRegisterAllocation_) {
        // Pass function names to register allocator so it skips them
        // Verify that allFunctionNames_ is populated
        if (allFunctionNames_.empty()) {
            // This should never happen - allFunctionNames_ should be populated in visit(Program&)
            // before any functions are compiled
        }
        regAlloc_.setFunctionNames(&allFunctionNames_);
        regAlloc_.analyze(node);
        
        for (const auto& range : regAlloc_.getLiveRanges()) {
            if (range.reg != VarRegister::NONE) {
                varRegisters_[range.name] = range.reg;
            }
        }
    }
    
    // Increased base stack for builtin internal locals
    int32_t baseStack = 0x200;
    int32_t callStack = calculateFunctionStackSize(node.body.get());
    
    if (isLeafFunction_) {
        int spillCount = 0;
        for (size_t i = 0; i < node.params.size(); i++) {
            if (varRegisters_.find(node.params[i].first) == varRegisters_.end() ||
                varRegisters_[node.params[i].first] == VarRegister::NONE) {
                spillCount++;
            }
        }
        baseStack = std::max(0x20, spillCount * 8 + 0x10);
        callStack = 0;
    }
    
    functionStackSize_ = ((baseStack + callStack + 0x28 + 15) / 16) * 16;
    
    asm_.label(node.name);
    
    // Handle naked functions - no prologue/epilogue
    if (node.isNaked) {
        // For naked functions, just emit the body directly
        // The user is responsible for all stack management
        node.body->accept(*this);
        
        // Restore state
        locals = savedLocals;
        constStrVars = savedConstStrVars;
        varRecordTypes_ = savedVarRecordTypes;
        stackOffset = savedStackOffset;
        inFunction = savedInFunction;
        functionStackSize_ = savedFunctionStackSize;
        stackAllocated_ = savedStackAllocated;
        varRegisters_ = savedVarRegisters;
        isLeafFunction_ = savedIsLeaf;
        stdoutHandleCached_ = savedStdoutCached;
        
        for (auto* nested : nestedFunctions) {
            nested->accept(*this);
        }
        return;
    }
    
    if (isLeafFunction_ && varRegisters_.size() == node.params.size() && node.params.size() <= 4) {
        emitSaveCalleeSavedRegs();
        
        for (size_t i = 0; i < node.params.size() && i < 4; i++) {
            // Only mark string parameters in constStrVars
            const std::string& paramType = node.params[i].second;
            if (paramType == "str" || paramType == "string" || paramType == "String") {
                constStrVars[node.params[i].first] = "";  // Empty means runtime string
            }
            emitMoveParamToVar((int)i, node.params[i].first, node.params[i].second);
            if (isFloatTypeName(node.params[i].second)) {
                floatVars.insert(node.params[i].first);
            }
            // Track function pointer parameters
            if (paramType.find("fn(") != std::string::npos || 
                paramType.find("fn (") != std::string::npos ||
                (paramType.size() > 3 && paramType.substr(0, 3) == "*fn")) {
                fnPtrVars_.insert(node.params[i].first);
            }
            // Track record type for parameters
            // Handle generic types like Container[int] -> Container
            std::string paramTypeName = node.params[i].second;
            size_t bracketPos = paramTypeName.find('[');
            if (bracketPos != std::string::npos) {
                paramTypeName = paramTypeName.substr(0, bracketPos);
            }
            if (recordTypes_.find(paramTypeName) != recordTypes_.end()) {
                varRecordTypes_[node.params[i].first] = paramTypeName;
            }
            // Track borrow parameters for auto-dereference on return
            if (!paramType.empty() && paramType[0] == '&') {
                // Extract base type: "&int" -> "int", "&mut int" -> "int"
                std::string baseType = paramType.substr(1);
                if (baseType.substr(0, 4) == "mut ") {
                    baseType = baseType.substr(4);
                }
                // Trim leading whitespace
                while (!baseType.empty() && (baseType[0] == ' ' || baseType[0] == '\t')) {
                    baseType = baseType.substr(1);
                }
                borrowParams_[node.params[i].first] = baseType;
            }
        }
        
        stackAllocated_ = false;
    } else {
        asm_.push_rbp();
        asm_.mov_rbp_rsp();
        
        emitSaveCalleeSavedRegs();
        
        asm_.sub_rsp_imm32(functionStackSize_);
        stackAllocated_ = true;
        
        for (size_t i = 0; i < node.params.size() && i < 4; i++) {
            // Only mark string parameters in constStrVars
            const std::string& paramType = node.params[i].second;
            if (paramType == "str" || paramType == "string" || paramType == "String") {
                constStrVars[node.params[i].first] = "";  // Empty means runtime string
            }
            emitMoveParamToVar((int)i, node.params[i].first, node.params[i].second);
            if (isFloatTypeName(node.params[i].second)) {
                floatVars.insert(node.params[i].first);
            }
            // Track function pointer parameters
            if (paramType.find("fn(") != std::string::npos || 
                paramType.find("fn (") != std::string::npos ||
                (paramType.size() > 3 && paramType.substr(0, 3) == "*fn")) {
                fnPtrVars_.insert(node.params[i].first);
            }
            // Track record type for parameters
            // Handle generic types like Container[int] -> Container
            std::string paramTypeName = node.params[i].second;
            size_t bracketPos = paramTypeName.find('[');
            if (bracketPos != std::string::npos) {
                paramTypeName = paramTypeName.substr(0, bracketPos);
            }
            if (recordTypes_.find(paramTypeName) != recordTypes_.end()) {
                varRecordTypes_[node.params[i].first] = paramTypeName;
            }
            // Track borrow parameters for auto-dereference on return
            if (!paramType.empty() && paramType[0] == '&') {
                // Extract base type: "&int" -> "int", "&mut int" -> "int"
                std::string baseType = paramType.substr(1);
                if (baseType.substr(0, 4) == "mut ") {
                    baseType = baseType.substr(4);
                }
                // Trim leading whitespace
                while (!baseType.empty() && (baseType[0] == ' ' || baseType[0] == '\t')) {
                    baseType = baseType.substr(1);
                }
                borrowParams_[node.params[i].first] = baseType;
            }
        }
    }
    
    node.body->accept(*this);
    
    if (!endsWithTerminator(node.body.get())) {
        asm_.xor_rax_rax();
        
        if (isLeafFunction_ && varRegisters_.size() == node.params.size() && node.params.size() <= 4) {
            emitRestoreCalleeSavedRegs();
        } else {
            asm_.add_rsp_imm32(functionStackSize_);
            emitRestoreCalleeSavedRegs();
            asm_.pop_rbp();
        }
        
        asm_.ret();
    }
    
    locals = savedLocals;
    constStrVars = savedConstStrVars;
    varRecordTypes_ = savedVarRecordTypes;
    stackOffset = savedStackOffset;
    inFunction = savedInFunction;
    functionStackSize_ = savedFunctionStackSize;
    stackAllocated_ = savedStackAllocated;
    varRegisters_ = savedVarRegisters;
    isLeafFunction_ = savedIsLeaf;
    stdoutHandleCached_ = savedStdoutCached;
    borrowParams_ = savedBorrowParams;
    currentFnReturnType_ = savedReturnType;
    fnPtrVars_ = savedFnPtrVars;
    closureVars_ = savedClosureVars;
    
    for (auto* nested : nestedFunctions) {
        nested->accept(*this);
    }
}

} // namespace tyl
