// Tyl Compiler - Native Code Generator Program Visitor
// Handles: Program entry point, _start generation, module/function orchestration

#include "backend/codegen/codegen_base.h"

namespace tyl {

void NativeCodeGen::collectGenericInstantiations(Program& program) {
    // Clear previous state
    monomorphizer_.clear();
    genericFunctions_.clear();
    genericRecords_.clear();
    specializedFunctions_.clear();
    specializedRecords_.clear();
    
    // Use the GenericCollector to find all instantiations
    GenericCollector collector(monomorphizer_, genericFunctions_, genericRecords_);
    collector.collect(program);
    
    // Create specialized copies of generic functions
    for (const auto& [inst, originalFn] : monomorphizer_.getFunctionInstantiations()) {
        auto specialized = monomorphizer_.specializeFunction(originalFn, inst.typeArgs);
        if (specialized) {
            specializedFunctions_.push_back(std::move(specialized));
        }
    }
    
    // Create specialized copies of generic records
    for (const auto& [inst, originalRec] : monomorphizer_.getRecordInstantiations()) {
        auto specialized = monomorphizer_.specializeRecord(originalRec, inst.typeArgs);
        if (specialized) {
            specializedRecords_.push_back(std::move(specialized));
        }
    }
}

void NativeCodeGen::emitSpecializedFunctions() {
    // Emit code for all specialized generic functions
    for (const auto& [inst, originalFn] : monomorphizer_.getFunctionInstantiations()) {
        if (!originalFn || !originalFn->body) continue;
        
        std::string mangledName = inst.mangledName;
        
        // Register the label if not already registered
        if (asm_.labels.find(mangledName) == asm_.labels.end()) {
            asm_.labels[mangledName] = 0;
        }
        
        // Save current state
        std::map<std::string, int32_t> savedLocals = locals;
        std::map<std::string, std::string> savedConstStrVars = constStrVars;
        std::set<std::string> savedFloatVars = floatVars;
        int32_t savedStackOffset = stackOffset;
        bool savedInFunction = inFunction;
        int32_t savedFunctionStackSize = functionStackSize_;
        bool savedStackAllocated = stackAllocated_;
        std::map<std::string, VarRegister> savedVarRegisters = varRegisters_;
        bool savedIsLeaf = isLeafFunction_;
        RegisterAllocator savedRegAlloc = regAlloc_;
        bool savedStdoutCached = stdoutHandleCached_;
        bool savedLastExprWasFloat = lastExprWasFloat_;
        bool savedRuntimeRoutinesEmitted = runtimeRoutinesEmitted_;

        // Reset state for this function
        inFunction = true;
        locals.clear();
        stackOffset = 0;
        stackAllocated_ = false;
        varRegisters_.clear();
        floatVars.clear();  // Clear float vars for this function
        
        // precise state reset
        stdoutHandleCached_ = false;
        lastExprWasFloat_ = false;
        runtimeRoutinesEmitted_ = false; // Allow specialized functions to emit their own runtime routines if needed, though they usually share the main one. Wait, shared routines are at module level. Let's keep runtimeRoutinesEmitted_ as is? No, it's a member of NativeCodeGen, so it's global for the codegen instance. 
        // Actually, runtimeRoutinesEmitted_ tracks if we've emitted the shared helpers at the END of the file. 
        // specialized functions are emitted BEFORE the end. 
        // We should NOT reset runtimeRoutinesEmitted_ because we only want one copy of runtime routines at the very end.
        // But we DO need to reset stdoutHandleCached_ because that tracks if RDI has the handle *currently* in this function.
        
        // Reset register allocator for this function
        regAlloc_ = RegisterAllocator();
        
        // Disable leaf optimization for specialized functions to ensure proper prologue/epilogue
        isLeafFunction_ = false;
        
        // Build type parameter to concrete type mapping
        std::unordered_map<std::string, std::string> typeSubst;
        for (size_t i = 0; i < originalFn->typeParams.size() && i < inst.typeArgs.size(); i++) {
            typeSubst[originalFn->typeParams[i]] = inst.typeArgs[i]->toString();
        }
        
        // Mark parameters with their concrete types
        for (size_t i = 0; i < originalFn->params.size(); i++) {
            const std::string& paramName = originalFn->params[i].first;
            std::string paramType = originalFn->params[i].second;
            
            // Substitute type parameter with concrete type
            auto it = typeSubst.find(paramType);
            if (it != typeSubst.end()) {
                paramType = it->second;
            }
            
            // Track float parameters
            if (isFloatTypeName(paramType)) {
                floatVars.insert(paramName);
            }
            
            // Track string parameters
            constStrVars[paramName] = "";  // Mark as potentially string
        }
        
        // Calculate stack size - increased base for builtin internal locals
        int32_t baseStack = 0x200;
        int32_t callStack = calculateFunctionStackSize(originalFn->body.get());
        functionStackSize_ = ((baseStack + callStack + 0x28 + 15) / 16) * 16;
        
        // Emit function label
        asm_.label(mangledName);
        
        // Standard function prologue
        asm_.push_rbp();
        asm_.mov_rbp_rsp();
        
        emitSaveCalleeSavedRegs();
        
        asm_.sub_rsp_imm32(functionStackSize_);
        stackAllocated_ = true;

        // Handle parameters - for float parameters, they come in XMM registers
        for (size_t i = 0; i < originalFn->params.size() && i < 4; i++) {
            const std::string& paramName = originalFn->params[i].first;
            std::string paramType = originalFn->params[i].second;
            
            // Substitute type parameter
            auto it = typeSubst.find(paramType);
            if (it != typeSubst.end()) {
                paramType = it->second;
            }
            
            allocLocal(paramName);
            int32_t off = locals[paramName];
            
            if (isFloatTypeName(paramType)) {
                // Float parameters come in XMM0-XMM3
                // movsd [rbp+off], xmmN
                switch (i) {
                    case 0:
                        asm_.code.push_back(0xF2); asm_.code.push_back(0x0F);
                        asm_.code.push_back(0x11); asm_.code.push_back(0x85);
                        break;
                    case 1:
                        asm_.code.push_back(0xF2); asm_.code.push_back(0x0F);
                        asm_.code.push_back(0x11); asm_.code.push_back(0x8D);
                        break;
                    case 2:
                        asm_.code.push_back(0xF2); asm_.code.push_back(0x44);
                        asm_.code.push_back(0x0F); asm_.code.push_back(0x11);
                        asm_.code.push_back(0x85);
                        break;
                    case 3:
                        asm_.code.push_back(0xF2); asm_.code.push_back(0x44);
                        asm_.code.push_back(0x0F); asm_.code.push_back(0x11);
                        asm_.code.push_back(0x8D);
                        break;
                }
                if (i < 2) {
                    asm_.code.push_back(off & 0xFF);
                    asm_.code.push_back((off >> 8) & 0xFF);
                    asm_.code.push_back((off >> 16) & 0xFF);
                    asm_.code.push_back((off >> 24) & 0xFF);
                } else {
                    asm_.code.push_back(off & 0xFF);
                    asm_.code.push_back((off >> 8) & 0xFF);
                    asm_.code.push_back((off >> 16) & 0xFF);
                    asm_.code.push_back((off >> 24) & 0xFF);
                }
            } else {
                // Integer/pointer parameters come in RCX, RDX, R8, R9
                switch (i) {
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
        
        // Compile the function body
        originalFn->body->accept(*this);
        
        // Emit epilogue if body doesn't end with return
        if (!endsWithTerminator(originalFn->body.get())) {
            asm_.xor_rax_rax();
            asm_.add_rsp_imm32(functionStackSize_);
            emitRestoreCalleeSavedRegs();
            asm_.pop_rbp();
            asm_.ret();
        }
        
        // Restore state
        locals = savedLocals;
        constStrVars = savedConstStrVars;
        floatVars = savedFloatVars;
        stackOffset = savedStackOffset;
        inFunction = savedInFunction;
        functionStackSize_ = savedFunctionStackSize;
        stackAllocated_ = savedStackAllocated;
        varRegisters_ = savedVarRegisters;
        isLeafFunction_ = savedIsLeaf;
        stdoutHandleCached_ = savedStdoutCached;
        regAlloc_ = savedRegAlloc;
    }
}

std::string NativeCodeGen::resolveGenericCall(const std::string& fnName, 
                                               const std::vector<TypePtr>& typeArgs) {
    if (typeArgs.empty()) return fnName;
    
    // Check if this is a generic function
    auto it = genericFunctions_.find(fnName);
    if (it == genericFunctions_.end()) return fnName;
    
    // Get the mangled name for this instantiation
    return monomorphizer_.getMangledName(fnName, typeArgs);
}


void NativeCodeGen::visit(Program& node) {
    std::vector<FnDecl*> functions;
    std::vector<Statement*> topLevelStmts;
    std::vector<ModuleDecl*> modules;
    std::vector<ExternDecl*> externs;
    FnDecl* mainFn = nullptr;
    
    // Note: collectGenericInstantiations is now called in compile() before the pre-scan
    // to ensure float variable tracking works correctly for generic function calls
    
    for (auto& stmt : node.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            // Skip generic functions - they will be compiled as specialized versions
            if (!fn->typeParams.empty()) {
                continue;
            }
            functions.push_back(fn);
            if (fn->name == "main") {
                mainFn = fn;
            }
        } else if (auto* mod = dynamic_cast<ModuleDecl*>(stmt.get())) {
            modules.push_back(mod);
        } else if (auto* ext = dynamic_cast<ExternDecl*>(stmt.get())) {
            externs.push_back(ext);
        } else if (dynamic_cast<TraitDecl*>(stmt.get())) {
            // Process trait declarations separately (register metadata only, no code)
            stmt->accept(*this);
        } else if (auto* impl = dynamic_cast<ImplBlock*>(stmt.get())) {
            // Register impl info but DON'T compile methods yet
            // We'll compile impl methods after _start, along with other functions
            std::string implKey = impl->traitName + ":" + impl->typeName;
            ImplInfo info;
            info.traitName = impl->traitName;
            info.typeName = impl->typeName;
            
            for (auto& method : impl->methods) {
                std::string mangledName;
                if (!impl->traitName.empty()) {
                    mangledName = impl->typeName + "_" + impl->traitName + "_" + method->name;
                } else {
                    mangledName = impl->typeName + "_" + method->name;
                }
                info.methodLabels[method->name] = mangledName;
                info.methodReturnTypes[method->name] = method->returnType;
                // Register the label for later
                asm_.labels[mangledName] = 0;
                
                // Track string-returning methods
                if (method->returnType == "str" || method->returnType == "string" ||
                    method->returnType == "*str" || method->returnType == "*u8") {
                    stringReturningFunctions_.insert(mangledName);
                }
            }
            impls_[implKey] = info;
            
            // Also register with just the type name for method lookup
            if (!impl->traitName.empty()) {
                std::string typeOnlyKey = ":" + impl->typeName;
                auto& typeInfo = impls_[typeOnlyKey];
                typeInfo.typeName = impl->typeName;
                for (const auto& [methodName, label] : info.methodLabels) {
                    typeInfo.methodLabels[methodName] = label;
                }
                for (const auto& [methodName, retType] : info.methodReturnTypes) {
                    typeInfo.methodReturnTypes[methodName] = retType;
                }
            }
        } else if (dynamic_cast<RecordDecl*>(stmt.get())) {
            // Process record declarations to register type information
            stmt->accept(*this);
        } else if (dynamic_cast<EnumDecl*>(stmt.get())) {
            // Process enum declarations to register constant values
            stmt->accept(*this);
        } else if (dynamic_cast<TypeAlias*>(stmt.get())) {
            // Process type aliases to register refinement type information
            stmt->accept(*this);
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
        
        for (const auto& [name, info] : globalRegAlloc_.getGlobalVars()) {
            if (info.assignedReg != VarRegister::NONE) {
                globalVarRegisters_[name] = info.assignedReg;
            }
        }
    }
    
    // Infer parameter types from call sites (for functions without explicit type annotations)
    inferParamTypesFromCallSites(node, functions);
    
    // Register all function labels and track all function names for UFCS
    for (auto* fn : functions) {
        asm_.labels[fn->name] = 0;
        allFunctionNames_.insert(fn->name);
        functionDecls_[fn->name] = fn;  // Store declaration for default parameter lookup
        
        // Track functions that return strings
        if (fn->returnType == "str" || fn->returnType == "string" || 
            fn->returnType == "*str" || fn->returnType == "*u8") {
            stringReturningFunctions_.insert(fn->name);
        }
    }
    
    // Register specialized function labels
    for (auto& fn : specializedFunctions_) {
        asm_.labels[fn->name] = 0;
        allFunctionNames_.insert(fn->name);
        functionDecls_[fn->name] = fn.get();  // Store declaration for default parameter lookup
    }
    
    // Also register labels from monomorphizer directly
    for (const auto& [inst, _] : monomorphizer_.getFunctionInstantiations()) {
        asm_.labels[inst.mangledName] = 0;
    }
    
    // Register module function labels
    for (auto* mod : modules) {
        for (auto& stmt : mod->body) {
            if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
                asm_.labels[fn->name] = 0;
            }
        }
    }
    
    asm_.label("_start");
    asm_.push_rbp();
    asm_.mov_rbp_rsp();
    
    // Save callee-saved registers used for global variables
    int numPushedRegs = 0;
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
    // Base size increased to handle builtins that allocate local storage
    int32_t topLevelStackSize = 0x400;  // 1KB base for builtin allocations
    
    for (auto* stmt : topLevelStmts) {
        topLevelStackSize = std::max(topLevelStackSize, 
            0x400 + calculateFunctionStackSize(stmt));
    }
    
    topLevelStackSize = ((topLevelStackSize + 0x38 + 15) / 16) * 16;
    
    // Adjust for stack alignment based on number of pushed registers
    if ((2 + numPushedRegs) % 2 == 1) {
        if (topLevelStackSize % 16 == 0) {
            topLevelStackSize += 8;
        }
    }
    
    asm_.sub_rsp_imm32(topLevelStackSize);
    stackAllocated_ = true;
    functionStackSize_ = topLevelStackSize;
    
    // Initialize GC
    if (useGC_) {
        emitGCInit();
    }
    
    // Copy global register assignments to varRegisters_ for use in codegen
    varRegisters_ = globalVarRegisters_;
    
    for (auto* stmt : topLevelStmts) {
        stmt->accept(*this);
    }
    
    if (mainFn) {
        asm_.call_rel32("main");
    } else {
        asm_.xor_rax_rax();
    }
    
    asm_.mov_rcx_rax();
    asm_.call_mem_rip(pe_.getImportRVA("ExitProcess"));
    
    // Reset for function compilation
    stackAllocated_ = false;
    functionStackSize_ = 0;
    varRegisters_.clear();
    
    // Emit top-level functions
    for (auto* fn : functions) {
        fn->accept(*this);
    }
    
    // Emit specialized generic functions
    emitSpecializedFunctions();
    
    // Emit module functions
    for (auto* mod : modules) {
        for (auto& stmt : mod->body) {
            if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
                // Skip generic functions in modules too
                if (!fn->typeParams.empty()) continue;
                fn->accept(*this);
            }
        }
    }
    
    // Emit impl block methods
    for (auto& stmt : node.statements) {
        if (auto* impl = dynamic_cast<ImplBlock*>(stmt.get())) {
            for (auto& method : impl->methods) {
                std::string mangledName;
                if (!impl->traitName.empty()) {
                    mangledName = impl->typeName + "_" + impl->traitName + "_" + method->name;
                } else {
                    mangledName = impl->typeName + "_" + method->name;
                }
                std::string originalName = method->name;
                method->name = mangledName;
                
                // Set current impl type so self parameter gets correct record type
                currentImplTypeName_ = impl->typeName;
                method->accept(*this);
                currentImplTypeName_.clear();
                
                method->name = originalName;
            }
        }
    }
    
    // Emit callback trampolines for C interop
    for (const auto& [fnName, info] : callbacks_) {
        emitCallbackTrampoline(fnName, info);
    }
    
    // Emit GC collection routine if GC is enabled
    if (useGC_) {
        emitGCCollectRoutine();
    }
}

// Type inference from call sites - infer parameter types from how functions are called
void NativeCodeGen::inferParamTypesFromCallSites(Program& program, const std::vector<FnDecl*>& functions) {
    // Build a set of function names for quick lookup
    std::set<std::string> functionNames;
    for (auto* fn : functions) {
        functionNames.insert(fn->name);
    }
    
    // Scan all statements for call expressions
    for (auto& stmt : program.statements) {
        inferParamTypesFromStmt(stmt.get(), functionNames);
    }
    
    // Infer return types from function bodies
    // If a function returns a parameter that we've inferred is a string, the function returns a string
    for (auto* fn : functions) {
        if (fn->returnType.empty() || fn->returnType == "auto") {
            auto fnIt = inferredParamTypes_.find(fn->name);
            if (fnIt != inferredParamTypes_.end()) {
                // Check if the function returns one of its string parameters
                if (auto* block = dynamic_cast<Block*>(fn->body.get())) {
                    for (auto& stmt : block->statements) {
                        if (auto* retStmt = dynamic_cast<ReturnStmt*>(stmt.get())) {
                            if (auto* retId = dynamic_cast<Identifier*>(retStmt->value.get())) {
                                // Check if this identifier is a string parameter
                                for (size_t i = 0; i < fn->params.size(); i++) {
                                    if (fn->params[i].first == retId->name) {
                                        auto paramIt = fnIt->second.find(i);
                                        if (paramIt != fnIt->second.end() && paramIt->second == "str") {
                                            // This function returns a string
                                            stringReturningFunctions_.insert(fn->name);
                                        }
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    // Now apply inferred types to constStrVars for string parameters
    for (auto* fn : functions) {
        auto fnIt = inferredParamTypes_.find(fn->name);
        if (fnIt != inferredParamTypes_.end()) {
            for (size_t i = 0; i < fn->params.size(); i++) {
                // Only infer if parameter has no explicit type
                if (fn->params[i].second.empty() || fn->params[i].second == "auto") {
                    auto paramIt = fnIt->second.find(i);
                    if (paramIt != fnIt->second.end() && paramIt->second == "str") {
                        // Mark this parameter as a string in constStrVars
                        // This will be used during function compilation
                        // We store it with a special prefix to indicate it's an inferred param type
                        std::string key = fn->name + ":" + fn->params[i].first;
                        inferredParamTypes_[fn->name][i] = "str";
                    }
                }
            }
        }
    }
}

void NativeCodeGen::inferParamTypesFromStmt(Statement* stmt, const std::set<std::string>& functionNames) {
    if (!stmt) return;
    
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        inferParamTypesFromExpr(exprStmt->expr.get(), functionNames);
    }
    else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        inferParamTypesFromExpr(varDecl->initializer.get(), functionNames);
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        inferParamTypesFromExpr(assignStmt->value.get(), functionNames);
    }
    else if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            inferParamTypesFromStmt(s.get(), functionNames);
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        inferParamTypesFromExpr(ifStmt->condition.get(), functionNames);
        inferParamTypesFromStmt(ifStmt->thenBranch.get(), functionNames);
        for (auto& [cond, branch] : ifStmt->elifBranches) {
            inferParamTypesFromExpr(cond.get(), functionNames);
            inferParamTypesFromStmt(branch.get(), functionNames);
        }
        inferParamTypesFromStmt(ifStmt->elseBranch.get(), functionNames);
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        inferParamTypesFromExpr(whileStmt->condition.get(), functionNames);
        inferParamTypesFromStmt(whileStmt->body.get(), functionNames);
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
        inferParamTypesFromExpr(forStmt->iterable.get(), functionNames);
        inferParamTypesFromStmt(forStmt->body.get(), functionNames);
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        inferParamTypesFromExpr(returnStmt->value.get(), functionNames);
    }
    else if (auto* fnDecl = dynamic_cast<FnDecl*>(stmt)) {
        inferParamTypesFromStmt(fnDecl->body.get(), functionNames);
    }
    else if (auto* matchStmt = dynamic_cast<MatchStmt*>(stmt)) {
        inferParamTypesFromExpr(matchStmt->value.get(), functionNames);
        for (auto& case_ : matchStmt->cases) {
            inferParamTypesFromExpr(case_.pattern.get(), functionNames);
            if (case_.guard) inferParamTypesFromExpr(case_.guard.get(), functionNames);
            inferParamTypesFromStmt(case_.body.get(), functionNames);
        }
        inferParamTypesFromStmt(matchStmt->defaultCase.get(), functionNames);
    }
}

void NativeCodeGen::inferParamTypesFromExpr(Expression* expr, const std::set<std::string>& functionNames) {
    if (!expr) return;
    
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        // Check if this is a call to a user-defined function
        if (auto* ident = dynamic_cast<Identifier*>(call->callee.get())) {
            if (functionNames.count(ident->name)) {
                // Analyze arguments to infer parameter types
                for (size_t i = 0; i < call->args.size(); i++) {
                    Expression* arg = call->args[i].get();
                    
                    // Check if argument is a string literal
                    if (dynamic_cast<StringLiteral*>(arg)) {
                        inferredParamTypes_[ident->name][i] = "str";
                    }
                    // Check if argument is an interpolated string
                    else if (dynamic_cast<InterpolatedString*>(arg)) {
                        inferredParamTypes_[ident->name][i] = "str";
                    }
                    // Check if argument is a known string variable
                    else if (auto* argIdent = dynamic_cast<Identifier*>(arg)) {
                        if (constStrVars.count(argIdent->name)) {
                            inferredParamTypes_[ident->name][i] = "str";
                        }
                    }
                    // Check if argument is a DSL block (which produces strings)
                    else if (dynamic_cast<DSLBlock*>(arg)) {
                        inferredParamTypes_[ident->name][i] = "str";
                    }
                }
            }
        }
        
        // Recurse into callee and arguments
        inferParamTypesFromExpr(call->callee.get(), functionNames);
        for (auto& arg : call->args) {
            inferParamTypesFromExpr(arg.get(), functionNames);
        }
    }
    else if (auto* assign = dynamic_cast<AssignExpr*>(expr)) {
        // Handle assignment expressions like: result = echo("Hello")
        inferParamTypesFromExpr(assign->target.get(), functionNames);
        inferParamTypesFromExpr(assign->value.get(), functionNames);
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        inferParamTypesFromExpr(binary->left.get(), functionNames);
        inferParamTypesFromExpr(binary->right.get(), functionNames);
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        inferParamTypesFromExpr(unary->operand.get(), functionNames);
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        inferParamTypesFromExpr(ternary->condition.get(), functionNames);
        inferParamTypesFromExpr(ternary->thenExpr.get(), functionNames);
        inferParamTypesFromExpr(ternary->elseExpr.get(), functionNames);
    }
    else if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        inferParamTypesFromExpr(index->object.get(), functionNames);
        inferParamTypesFromExpr(index->index.get(), functionNames);
    }
    else if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        inferParamTypesFromExpr(member->object.get(), functionNames);
    }
    else if (auto* list = dynamic_cast<ListExpr*>(expr)) {
        for (auto& elem : list->elements) {
            inferParamTypesFromExpr(elem.get(), functionNames);
        }
    }
    else if (auto* lambda = dynamic_cast<LambdaExpr*>(expr)) {
        inferParamTypesFromExpr(lambda->body.get(), functionNames);
    }
}

} // namespace tyl
