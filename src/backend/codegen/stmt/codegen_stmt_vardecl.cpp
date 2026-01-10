// Tyl Compiler - Native Code Generator Variable Declaration
// Handles: VarDecl

#include "backend/codegen/codegen_base.h"
#include "frontend/token/token.h"

namespace tyl {

void NativeCodeGen::visit(VarDecl& node) {
    if (node.initializer) {
        // For compile-time constants (NAME :: value), only store in constVars
        // and skip code generation - they will be inlined at use sites
        if (node.isConst) {
            int64_t intVal;
            if (tryEvalConstant(node.initializer.get(), intVal)) {
                constVars[node.name] = intVal;
                return;  // No code generation needed for compile-time constants
            }
            double floatVal;
            if (tryEvalConstantFloat(node.initializer.get(), floatVal)) {
                constFloatVars[node.name] = floatVal;
                return;  // No code generation needed for compile-time constants
            }
            std::string strVal;
            if (tryEvalConstantString(node.initializer.get(), strVal)) {
                constStrVars[node.name] = strVal;
                return;  // No code generation needed for compile-time constants
            }
            // If we can't evaluate the constant at compile time, fall through
            // to generate runtime code (this shouldn't happen for valid constants)
        }
        
        bool isFloat = isFloatExpression(node.initializer.get());
        
        // Check for lambda/closure
        if (dynamic_cast<LambdaExpr*>(node.initializer.get())) {
            closureVars_.insert(node.name);
        }
        
        // Check for function pointer type
        bool isFnPtr = false;
        if (!node.typeName.empty() && node.typeName.size() > 3 && 
            node.typeName.substr(0, 3) == "*fn") {
            isFnPtr = true;
            fnPtrVars_.insert(node.name);
        }
        if (auto* addrOf = dynamic_cast<AddressOfExpr*>(node.initializer.get())) {
            if (auto* fnId = dynamic_cast<Identifier*>(addrOf->operand.get())) {
                if (asm_.labels.find(fnId->name) != asm_.labels.end()) {
                    isFnPtr = true;
                    fnPtrVars_.insert(node.name);
                }
            }
        }
        if (auto* fnId = dynamic_cast<Identifier*>(node.initializer.get())) {
            if (asm_.labels.count(fnId->name)) {
                isFnPtr = true;
                fnPtrVars_.insert(node.name);
            }
        }
        
        // Check for generic function call with float arguments
        if (!isFloat) {
            if (auto* call = dynamic_cast<CallExpr*>(node.initializer.get())) {
                if (auto* id = dynamic_cast<Identifier*>(call->callee.get())) {
                    auto it = genericFunctions_.find(id->name);
                    if (it != genericFunctions_.end()) {
                        for (auto& arg : call->args) {
                            if (dynamic_cast<FloatLiteral*>(arg.get())) {
                                isFloat = true;
                                break;
                            }
                            if (auto* argId = dynamic_cast<Identifier*>(arg.get())) {
                                if (floatVars.count(argId->name) || constFloatVars.count(argId->name)) {
                                    isFloat = true;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
        
        // Track constants
        if (!node.isMutable) {
            if (isFloat) {
                double floatVal;
                if (tryEvalConstantFloat(node.initializer.get(), floatVal)) {
                    constFloatVars[node.name] = floatVal;
                }
            } else {
                int64_t intVal;
                if (tryEvalConstant(node.initializer.get(), intVal)) {
                    constVars[node.name] = intVal;
                }
            }
            std::string strVal;
            if (tryEvalConstantString(node.initializer.get(), strVal)) {
                constStrVars[node.name] = strVal;
            }
        }
        
        if (isFloat) {
            floatVars.insert(node.name);
        }
        
        if (dynamic_cast<StringLiteral*>(node.initializer.get()) ||
            dynamic_cast<InterpolatedString*>(node.initializer.get()) ||
            isStringReturningExpr(node.initializer.get())) {
            if (constStrVars.find(node.name) == constStrVars.end()) {
                constStrVars[node.name] = "";
            }
        }
        
        // Check for fixed-size array type BEFORE handling ListExpr
        // Fixed arrays use 0-based indexing, dynamic lists use 1-based
        if (!node.typeName.empty() && node.typeName.size() > 2 && 
            node.typeName.front() == '[' && node.typeName.back() == ']') {
            // This is a fixed array declaration with initializer
            emitFixedArrayDecl(node);
            return;
        }
        
        // Track list sizes and force list variables to stack
        if (auto* list = dynamic_cast<ListExpr*>(node.initializer.get())) {
            listSizes[node.name] = list->elements.size();
            listVars.insert(node.name);  // Track as list variable
            
            // Force list variables to stack to avoid register clobbering issues
            varRegisters_.erase(node.name);
            globalVarRegisters_.erase(node.name);
            
            std::vector<int64_t> values;
            bool allConst = true;
            for (auto& elem : list->elements) {
                int64_t val;
                if (tryEvalConstant(elem.get(), val)) {
                    values.push_back(val);
                } else {
                    allConst = false;
                    break;
                }
            }
            if (allConst) {
                constListVars[node.name] = values;
            }
            
            // Evaluate and store on stack (early return to avoid register allocation)
            node.initializer->accept(*this);
            allocLocal(node.name);
            asm_.mov_mem_rbp_rax(locals[node.name]);
            return;
        }
        
        // Handle move semantics: let b = a where a is a list variable
        // This copies the pointer (move), making b point to the same list
        if (auto* srcId = dynamic_cast<Identifier*>(node.initializer.get())) {
            if (listVars.count(srcId->name)) {
                // b is now a list variable too
                listVars.insert(node.name);
                // Copy list size if known
                if (listSizes.count(srcId->name)) {
                    listSizes[node.name] = listSizes[srcId->name];
                }
                // Copy const list values if known
                if (constListVars.count(srcId->name)) {
                    constListVars[node.name] = constListVars[srcId->name];
                }
                // Force to stack
                varRegisters_.erase(node.name);
                globalVarRegisters_.erase(node.name);
                
                // Evaluate source (loads pointer into RAX) and store
                node.initializer->accept(*this);
                allocLocal(node.name);
                asm_.mov_mem_rbp_rax(locals[node.name]);
                return;
            }
            
            // Handle fixed array variable copy: let b = a where a is a fixed array
            auto fixedIt = varFixedArrayTypes_.find(srcId->name);
            if (fixedIt != varFixedArrayTypes_.end()) {
                // Copy the fixed array type info to the target variable
                varFixedArrayTypes_[node.name] = fixedIt->second;
                // Force to stack
                varRegisters_.erase(node.name);
                globalVarRegisters_.erase(node.name);
                
                node.initializer->accept(*this);
                allocLocal(node.name);
                asm_.mov_mem_rbp_rax(locals[node.name]);
                return;
            }
        }
        
        // Handle assignment from index into fixed array: let row0 = mat[0]
        // This gives us a pointer to a sub-array which is itself a fixed array
        if (auto* indexExpr = dynamic_cast<IndexExpr*>(node.initializer.get())) {
            // Check if the object is a fixed array
            if (auto* objId = dynamic_cast<Identifier*>(indexExpr->object.get())) {
                auto fixedIt = varFixedArrayTypes_.find(objId->name);
                if (fixedIt != varFixedArrayTypes_.end()) {
                    // The element type might be another fixed array
                    const std::string& elemType = fixedIt->second.elementType;
                    if (!elemType.empty() && elemType[0] == '[') {
                        // Parse the inner array type to get its info
                        std::string inner = elemType.substr(1, elemType.size() - 2);
                        int bracketDepth = 0;
                        size_t semicolonPos = std::string::npos;
                        for (size_t j = 0; j < inner.size(); j++) {
                            if (inner[j] == '[') bracketDepth++;
                            else if (inner[j] == ']') bracketDepth--;
                            else if (inner[j] == ';' && bracketDepth == 0) {
                                semicolonPos = j;
                                break;
                            }
                        }
                        
                        if (semicolonPos != std::string::npos) {
                            std::string innerElemType = inner.substr(0, semicolonPos);
                            std::string sizeStr = inner.substr(semicolonPos + 1);
                            while (!innerElemType.empty() && innerElemType.back() == ' ') innerElemType.pop_back();
                            while (!sizeStr.empty() && sizeStr[0] == ' ') sizeStr = sizeStr.substr(1);
                            while (!sizeStr.empty() && sizeStr.back() == ' ') sizeStr.pop_back();
                            
                            FixedArrayInfo info;
                            info.elementType = innerElemType;
                            info.size = std::stoull(sizeStr);
                            info.elementSize = getTypeSize(innerElemType);
                            varFixedArrayTypes_[node.name] = info;
                            
                            // Force to stack
                            varRegisters_.erase(node.name);
                            globalVarRegisters_.erase(node.name);
                            
                            node.initializer->accept(*this);
                            allocLocal(node.name);
                            asm_.mov_mem_rbp_rax(locals[node.name]);
                            return;
                        }
                    }
                }
            }
        }
        
        // Track list-returning function calls (split, etc.)
        if (auto* call = dynamic_cast<CallExpr*>(node.initializer.get())) {
            if (auto* calleeId = dynamic_cast<Identifier*>(call->callee.get())) {
                // Functions that return lists
                if (calleeId->name == "split" || calleeId->name == "keys" || 
                    calleeId->name == "values" || calleeId->name == "range") {
                    listVars.insert(node.name);
                    // Force list variables to stack
                    varRegisters_.erase(node.name);
                    globalVarRegisters_.erase(node.name);
                    
                    // Evaluate and store on stack (early return to avoid register allocation)
                    node.initializer->accept(*this);
                    allocLocal(node.name);
                    asm_.mov_mem_rbp_rax(locals[node.name]);
                    return;
                }
            }
        }
        
        // Track record type
        if (auto* call = dynamic_cast<CallExpr*>(node.initializer.get())) {
            if (auto* calleeId = dynamic_cast<Identifier*>(call->callee.get())) {
                if (recordTypes_.find(calleeId->name) != recordTypes_.end()) {
                    varRecordTypes_[node.name] = calleeId->name;
                }
            }
        }
        if (!node.typeName.empty() && recordTypes_.find(node.typeName) != recordTypes_.end()) {
            varRecordTypes_[node.name] = node.typeName;
        }
        
        if (auto* recExpr = dynamic_cast<RecordExpr*>(node.initializer.get())) {
            if (!recExpr->typeName.empty()) {
                varRecordTypes_[node.name] = recExpr->typeName;
                
                // Force types with Drop trait to stack for proper cleanup
                std::string dropKey = "Drop:" + recExpr->typeName;
                if (impls_.count(dropKey)) {
                    varRegisters_.erase(node.name);
                    globalVarRegisters_.erase(node.name);
                    
                    node.initializer->accept(*this);
                    allocLocal(node.name);
                    asm_.mov_mem_rbp_rax(locals[node.name]);
                    return;
                }
            }
        }
        
        // Handle move semantics for record types with Drop trait
        // When we have `let r2 = r1` where r1 is a record with Drop, we need to:
        // 1. Track r2 as having the same record type
        // 2. Force r2 to stack allocation for proper cleanup
        if (auto* srcId = dynamic_cast<Identifier*>(node.initializer.get())) {
            auto typeIt = varRecordTypes_.find(srcId->name);
            if (typeIt != varRecordTypes_.end()) {
                std::string typeName = typeIt->second;
                varRecordTypes_[node.name] = typeName;
                
                // Check if this type has Drop trait
                std::string dropKey = "Drop:" + typeName;
                if (impls_.count(dropKey)) {
                    varRegisters_.erase(node.name);
                    globalVarRegisters_.erase(node.name);
                    
                    node.initializer->accept(*this);
                    allocLocal(node.name);
                    asm_.mov_mem_rbp_rax(locals[node.name]);
                    return;
                }
            }
        }
        
        // Force concurrency types to stack to avoid register clobbering issues
        // These types are often used across multiple operations and need stable storage
        bool forcedToStack = false;
        
        // Check for Future type
        if (dynamic_cast<MakeFutureExpr*>(node.initializer.get())) {
            varRegisters_.erase(node.name);
            globalVarRegisters_.erase(node.name);
            forcedToStack = true;
        }
        // Check for ThreadPool type
        if (dynamic_cast<MakeThreadPoolExpr*>(node.initializer.get())) {
            varRegisters_.erase(node.name);
            globalVarRegisters_.erase(node.name);
            forcedToStack = true;
        }
        // Check for CancelToken type
        if (dynamic_cast<MakeCancelTokenExpr*>(node.initializer.get())) {
            varRegisters_.erase(node.name);
            globalVarRegisters_.erase(node.name);
            forcedToStack = true;
        }
        // Check for Channel type
        if (dynamic_cast<MakeChanExpr*>(node.initializer.get())) {
            varRegisters_.erase(node.name);
            globalVarRegisters_.erase(node.name);
            forcedToStack = true;
        }
        
        if (forcedToStack) {
            node.initializer->accept(*this);
            allocLocal(node.name);
            asm_.mov_mem_rbp_rax(locals[node.name]);
            return;
        }
        
        // Track atomic type
        if (!node.typeName.empty() && node.typeName.size() > 7 && 
            node.typeName.substr(0, 7) == "Atomic[") {
            std::string elemType = node.typeName.substr(7, node.typeName.size() - 8);
            AtomicInfo info;
            info.elementType = elemType;
            info.elementSize = getTypeSize(elemType);
            if (info.elementSize == 0) info.elementSize = 8;  // Default to 8 bytes
            varAtomicTypes_[node.name] = info;
            
            // Force atomic variables to stack (not registers) to avoid register conflicts
            // when other variables are assigned to the same register
            // Remove from register allocation if present
            varRegisters_.erase(node.name);
            globalVarRegisters_.erase(node.name);
            
            node.initializer->accept(*this);
            allocLocal(node.name);
            asm_.mov_mem_rbp_rax(locals[node.name]);
            return;
        }
        // Also track if initializer is MakeAtomicExpr
        if (auto* makeAtomic = dynamic_cast<MakeAtomicExpr*>(node.initializer.get())) {
            AtomicInfo info;
            info.elementType = makeAtomic->elementType;
            info.elementSize = getTypeSize(makeAtomic->elementType);
            if (info.elementSize == 0) info.elementSize = 8;
            varAtomicTypes_[node.name] = info;
            
            // Force atomic variables to stack (not registers)
            // Remove from register allocation if present
            varRegisters_.erase(node.name);
            globalVarRegisters_.erase(node.name);
            
            node.initializer->accept(*this);
            allocLocal(node.name);
            asm_.mov_mem_rbp_rax(locals[node.name]);
            return;
        }
        
        // Track smart pointer types and force them to stack
        if (auto* makeBox = dynamic_cast<MakeBoxExpr*>(node.initializer.get())) {
            SmartPtrInfo info;
            info.elementType = makeBox->elementType;
            info.elementSize = getTypeSize(makeBox->elementType);
            if (info.elementSize == 0) info.elementSize = 8;
            info.kind = SmartPtrInfo::Kind::Box;
            varSmartPtrTypes_[node.name] = info;
            
            // Force smart pointer variables to stack (not registers)
            varRegisters_.erase(node.name);
            globalVarRegisters_.erase(node.name);
            
            node.initializer->accept(*this);
            allocLocal(node.name);
            asm_.mov_mem_rbp_rax(locals[node.name]);
            return;
        }
        if (auto* makeRc = dynamic_cast<MakeRcExpr*>(node.initializer.get())) {
            SmartPtrInfo info;
            info.elementType = makeRc->elementType;
            info.elementSize = getTypeSize(makeRc->elementType);
            if (info.elementSize == 0) info.elementSize = 8;
            info.kind = SmartPtrInfo::Kind::Rc;
            varSmartPtrTypes_[node.name] = info;
            
            // Force smart pointer variables to stack (not registers)
            varRegisters_.erase(node.name);
            globalVarRegisters_.erase(node.name);
            
            node.initializer->accept(*this);
            allocLocal(node.name);
            asm_.mov_mem_rbp_rax(locals[node.name]);
            return;
        }
        if (auto* makeArc = dynamic_cast<MakeArcExpr*>(node.initializer.get())) {
            SmartPtrInfo info;
            info.elementType = makeArc->elementType;
            info.elementSize = getTypeSize(makeArc->elementType);
            if (info.elementSize == 0) info.elementSize = 8;
            info.kind = SmartPtrInfo::Kind::Arc;
            varSmartPtrTypes_[node.name] = info;
            
            // Force smart pointer variables to stack (not registers)
            varRegisters_.erase(node.name);
            globalVarRegisters_.erase(node.name);
            
            node.initializer->accept(*this);
            allocLocal(node.name);
            asm_.mov_mem_rbp_rax(locals[node.name]);
            return;
        }
        if (auto* makeWeak = dynamic_cast<MakeWeakExpr*>(node.initializer.get())) {
            SmartPtrInfo info;
            info.elementType = "";  // Weak doesn't have direct element type
            info.elementSize = 8;
            info.kind = SmartPtrInfo::Kind::Weak;
            info.isAtomic = makeWeak->isAtomic;
            varSmartPtrTypes_[node.name] = info;
            
            // Force smart pointer variables to stack (not registers)
            varRegisters_.erase(node.name);
            globalVarRegisters_.erase(node.name);
            
            node.initializer->accept(*this);
            allocLocal(node.name);
            asm_.mov_mem_rbp_rax(locals[node.name]);
            return;
        }
        if (auto* makeCell = dynamic_cast<MakeCellExpr*>(node.initializer.get())) {
            SmartPtrInfo info;
            info.elementType = makeCell->elementType;
            info.elementSize = getTypeSize(makeCell->elementType);
            if (info.elementSize == 0) info.elementSize = 8;
            info.kind = SmartPtrInfo::Kind::Cell;
            varSmartPtrTypes_[node.name] = info;
            
            // Force smart pointer variables to stack (not registers)
            varRegisters_.erase(node.name);
            globalVarRegisters_.erase(node.name);
            
            node.initializer->accept(*this);
            allocLocal(node.name);
            asm_.mov_mem_rbp_rax(locals[node.name]);
            return;
        }
        if (auto* makeRefCell = dynamic_cast<MakeRefCellExpr*>(node.initializer.get())) {
            SmartPtrInfo info;
            info.elementType = makeRefCell->elementType;
            info.elementSize = getTypeSize(makeRefCell->elementType);
            if (info.elementSize == 0) info.elementSize = 8;
            info.kind = SmartPtrInfo::Kind::RefCell;
            varSmartPtrTypes_[node.name] = info;
            
            // Force smart pointer variables to stack (not registers)
            varRegisters_.erase(node.name);
            globalVarRegisters_.erase(node.name);
            
            node.initializer->accept(*this);
            allocLocal(node.name);
            asm_.mov_mem_rbp_rax(locals[node.name]);
            return;
        }
        
        // Track smart pointer types from method calls (e.g., rc.clone())
        if (auto* callExpr = dynamic_cast<CallExpr*>(node.initializer.get())) {
            if (auto* memberExpr = dynamic_cast<MemberExpr*>(callExpr->callee.get())) {
                if (auto* objId = dynamic_cast<Identifier*>(memberExpr->object.get())) {
                    auto smartIt = varSmartPtrTypes_.find(objId->name);
                    if (smartIt != varSmartPtrTypes_.end()) {
                        // Clone returns the same smart pointer type
                        if (memberExpr->member == "clone") {
                            varSmartPtrTypes_[node.name] = smartIt->second;
                            
                            // Force smart pointer variables to stack (not registers)
                            varRegisters_.erase(node.name);
                            globalVarRegisters_.erase(node.name);
                            
                            node.initializer->accept(*this);
                            allocLocal(node.name);
                            asm_.mov_mem_rbp_rax(locals[node.name]);
                            return;
                        }
                        // downgrade returns a Weak pointer
                        else if (memberExpr->member == "downgrade") {
                            SmartPtrInfo info;
                            info.elementType = smartIt->second.elementType;
                            info.elementSize = smartIt->second.elementSize;
                            info.kind = SmartPtrInfo::Kind::Weak;
                            info.isAtomic = (smartIt->second.kind == SmartPtrInfo::Kind::Arc);
                            varSmartPtrTypes_[node.name] = info;
                            
                            // Force smart pointer variables to stack (not registers)
                            varRegisters_.erase(node.name);
                            globalVarRegisters_.erase(node.name);
                            
                            node.initializer->accept(*this);
                            allocLocal(node.name);
                            asm_.mov_mem_rbp_rax(locals[node.name]);
                            return;
                        }
                        // upgrade returns Rc or Arc (depending on source)
                        else if (memberExpr->member == "upgrade" && smartIt->second.kind == SmartPtrInfo::Kind::Weak) {
                            SmartPtrInfo info;
                            info.elementType = smartIt->second.elementType;
                            info.elementSize = smartIt->second.elementSize;
                            info.kind = smartIt->second.isAtomic ? SmartPtrInfo::Kind::Arc : SmartPtrInfo::Kind::Rc;
                            varSmartPtrTypes_[node.name] = info;
                            
                            // Force smart pointer variables to stack (not registers)
                            varRegisters_.erase(node.name);
                            globalVarRegisters_.erase(node.name);
                            
                            node.initializer->accept(*this);
                            allocLocal(node.name);
                            asm_.mov_mem_rbp_rax(locals[node.name]);
                            return;
                        }
                    }
                }
            }
        }
        
        node.initializer->accept(*this);
        
        // Check for refinement type constraint
        if (!node.typeName.empty()) {
            auto refIt = refinementTypes_.find(node.typeName);
            if (refIt != refinementTypes_.end()) {
                // Track that this variable has a refinement type
                varRefinementTypes_[node.name] = node.typeName;
                
                // Try compile-time verification first
                int64_t constVal;
                if (tryEvalConstant(node.initializer.get(), constVal)) {
                    // We have a constant value - verify at compile time
                    bool constraintSatisfied = tryEvalRefinementConstraint(refIt->second, constVal);
                    if (!constraintSatisfied) {
                        // Emit compile-time error (as a warning for now, still emit runtime check)
                        std::cerr << "warning: Refinement type constraint may fail for type '" 
                                  << refIt->second.name << "' with value " << constVal << std::endl;
                    }
                }
                
                // Emit runtime check for the constraint
                // Value is in RAX, we need to check the constraint
                emitRefinementCheck(refIt->second, node.location);
            }
        }
        
        // Post-evaluation type inference
        if (auto* recExpr = dynamic_cast<RecordExpr*>(node.initializer.get())) {
            if (!recExpr->typeName.empty()) {
                varRecordTypes_[node.name] = recExpr->typeName;
            } else if (!recExpr->fields.empty()) {
                for (auto& [typeName, typeInfo] : recordTypes_) {
                    if (typeInfo.fieldNames.size() == recExpr->fields.size()) {
                        bool match = true;
                        for (size_t i = 0; i < recExpr->fields.size(); i++) {
                            if (recExpr->fields[i].first != typeInfo.fieldNames[i]) {
                                match = false;
                                break;
                            }
                        }
                        if (match) {
                            varRecordTypes_[node.name] = typeName;
                            break;
                        }
                    }
                }
            }
        }
        
        if (lastExprWasFloat_) {
            isFloat = true;
            floatVars.insert(node.name);
        }
        
        // Track comprehensive variable type for 'is' type checks
        if (!node.typeName.empty()) {
            varTypes_[node.name] = node.typeName;
        } else if (isFloat) {
            varTypes_[node.name] = "float";
        } else if (constStrVars.count(node.name) || dynamic_cast<StringLiteral*>(node.initializer.get()) ||
                   dynamic_cast<InterpolatedString*>(node.initializer.get())) {
            varTypes_[node.name] = "str";
        } else if (dynamic_cast<BoolLiteral*>(node.initializer.get())) {
            varTypes_[node.name] = "bool";
            boolVars_.insert(node.name);  // Track as boolean variable
        } else if (varRecordTypes_.count(node.name)) {
            varTypes_[node.name] = varRecordTypes_[node.name];
        } else if (listVars.count(node.name)) {
            varTypes_[node.name] = "list";
        } else {
            varTypes_[node.name] = "int";  // Default to int
        }
        
        // Check register allocation
        VarRegister allocatedReg = regAlloc_.getRegister(node.name);
        auto regIt = varRegisters_.find(node.name);
        if (allocatedReg != VarRegister::NONE && (regIt == varRegisters_.end() || regIt->second == VarRegister::NONE)) {
            varRegisters_[node.name] = allocatedReg;
            regIt = varRegisters_.find(node.name);
        }
        
        if (regIt != varRegisters_.end() && regIt->second != VarRegister::NONE) {
            if (isFloat && lastExprWasFloat_) {
                asm_.movq_rax_xmm0();
            }
            switch (regIt->second) {
                case VarRegister::RBX: asm_.mov_rbx_rax(); break;
                case VarRegister::R12: asm_.mov_r12_rax(); break;
                case VarRegister::R13: asm_.mov_r13_rax(); break;
                case VarRegister::R14: asm_.mov_r14_rax(); break;
                case VarRegister::R15: asm_.mov_r15_rax(); break;
                default: break;
            }
            return;
        }
        
        auto globalRegIt = globalVarRegisters_.find(node.name);
        if (globalRegIt != globalVarRegisters_.end() && globalRegIt->second != VarRegister::NONE) {
            if (isFloat && lastExprWasFloat_) {
                asm_.movq_rax_xmm0();
            }
            switch (globalRegIt->second) {
                case VarRegister::RBX: asm_.mov_rbx_rax(); break;
                case VarRegister::R12: asm_.mov_r12_rax(); break;
                case VarRegister::R13: asm_.mov_r13_rax(); break;
                case VarRegister::R14: asm_.mov_r14_rax(); break;
                case VarRegister::R15: asm_.mov_r15_rax(); break;
                default: break;
            }
            return;
        }
        
        if (isFloat && lastExprWasFloat_) {
            allocLocal(node.name);
            asm_.movsd_mem_rbp_xmm0(locals[node.name]);
            return;
        }
        
        allocLocal(node.name);
        asm_.mov_mem_rbp_rax(locals[node.name]);
        return;
    } else {
        // No initializer - handle type-based allocation
        emitUninitializedVarDecl(node);
    }
}

void NativeCodeGen::emitUninitializedVarDecl(VarDecl& node) {
    // Track record type from type annotation
    if (!node.typeName.empty() && recordTypes_.find(node.typeName) != recordTypes_.end()) {
        varRecordTypes_[node.name] = node.typeName;
        size_t recordSize = static_cast<size_t>(getRecordSize(node.typeName));
        
        if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
        
        asm_.call_mem_rip(pe_.getImportRVA("GetProcessHeap"));
        asm_.mov_rcx_rax();
        asm_.mov_rdx_imm64(0x08);
        asm_.mov_r8_imm64(recordSize);
        asm_.call_mem_rip(pe_.getImportRVA("HeapAlloc"));
        
        if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
        
        auto regIt = varRegisters_.find(node.name);
        if (regIt != varRegisters_.end() && regIt->second != VarRegister::NONE) {
            switch (regIt->second) {
                case VarRegister::RBX: asm_.mov_rbx_rax(); break;
                case VarRegister::R12: asm_.mov_r12_rax(); break;
                case VarRegister::R13: asm_.mov_r13_rax(); break;
                case VarRegister::R14: asm_.mov_r14_rax(); break;
                case VarRegister::R15: asm_.mov_r15_rax(); break;
                default: break;
            }
        } else {
            allocLocal(node.name);
            asm_.mov_mem_rbp_rax(locals[node.name]);
        }
        return;
    }
    
    // Check for fixed-size array type
    if (!node.typeName.empty() && node.typeName.size() > 2 && 
        node.typeName.front() == '[' && node.typeName.back() == ']') {
        emitFixedArrayDecl(node);
        return;
    }
    
    asm_.xor_rax_rax();
    
    auto regIt = varRegisters_.find(node.name);
    if (regIt != varRegisters_.end() && regIt->second != VarRegister::NONE) {
        switch (regIt->second) {
            case VarRegister::RBX: asm_.mov_rbx_rax(); break;
            case VarRegister::R12: asm_.mov_r12_rax(); break;
            case VarRegister::R13: asm_.mov_r13_rax(); break;
            case VarRegister::R14: asm_.mov_r14_rax(); break;
            case VarRegister::R15: asm_.mov_r15_rax(); break;
            default: break;
        }
        return;
    }
    
    auto globalRegIt = globalVarRegisters_.find(node.name);
    if (globalRegIt != globalVarRegisters_.end() && globalRegIt->second != VarRegister::NONE) {
        switch (globalRegIt->second) {
            case VarRegister::RBX: asm_.mov_rbx_rax(); break;
            case VarRegister::R12: asm_.mov_r12_rax(); break;
            case VarRegister::R13: asm_.mov_r13_rax(); break;
            case VarRegister::R14: asm_.mov_r14_rax(); break;
            case VarRegister::R15: asm_.mov_r15_rax(); break;
            default: break;
        }
        return;
    }
    
    allocLocal(node.name);
    asm_.mov_mem_rbp_rax(locals[node.name]);
}

void NativeCodeGen::emitFixedArrayDecl(VarDecl& node) {
    std::string inner = node.typeName.substr(1, node.typeName.size() - 2);
    
    int bracketDepth = 0;
    size_t semicolonPos = std::string::npos;
    for (size_t i = 0; i < inner.size(); i++) {
        if (inner[i] == '[') bracketDepth++;
        else if (inner[i] == ']') bracketDepth--;
        else if (inner[i] == ';' && bracketDepth == 0) {
            semicolonPos = i;
            break;
        }
    }
    
    if (semicolonPos != std::string::npos) {
        std::string elemType = inner.substr(0, semicolonPos);
        std::string sizeStr = inner.substr(semicolonPos + 1);
        while (!sizeStr.empty() && (sizeStr[0] == ' ' || sizeStr[0] == '\t')) sizeStr = sizeStr.substr(1);
        while (!sizeStr.empty() && (sizeStr.back() == ' ' || sizeStr.back() == '\t')) sizeStr.pop_back();
        
        size_t arrayCount = std::stoull(sizeStr);
        int32_t elemSize = getTypeSize(elemType);
        int32_t arraySize = elemSize * static_cast<int32_t>(arrayCount);
        
        FixedArrayInfo info;
        info.elementType = elemType;
        info.size = arrayCount;
        info.elementSize = elemSize;
        varFixedArrayTypes_[node.name] = info;
        
        // Force fixed arrays to stack (not registers) for consistent access
        varRegisters_.erase(node.name);
        globalVarRegisters_.erase(node.name);
        
        // Check if element type is itself an array (nested arrays store pointers)
        bool isNestedArray = !elemType.empty() && elemType[0] == '[';
        int32_t storageElemSize = isNestedArray ? 8 : elemSize;  // Pointers are 8 bytes
        int32_t actualArraySize = storageElemSize * static_cast<int32_t>(arrayCount);
        
        if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
        
        asm_.call_mem_rip(pe_.getImportRVA("GetProcessHeap"));
        asm_.mov_rcx_rax();
        asm_.mov_rdx_imm64(0x08);
        asm_.mov_r8_imm64(static_cast<size_t>(actualArraySize));
        asm_.call_mem_rip(pe_.getImportRVA("HeapAlloc"));
        
        if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
        
        // Store array pointer
        allocLocal(node.name);
        asm_.mov_mem_rbp_rax(locals[node.name]);
        
        // Initialize array elements if there's an initializer
        if (node.initializer) {
            if (auto* list = dynamic_cast<ListExpr*>(node.initializer.get())) {
                for (size_t i = 0; i < list->elements.size() && i < arrayCount; i++) {
                    // Evaluate element
                    list->elements[i]->accept(*this);
                    asm_.push_rax();  // Save element value
                    
                    // Load array base pointer
                    asm_.mov_rax_mem_rbp(locals[node.name]);
                    
                    // Calculate offset: base + i * storageElemSize
                    if (i > 0) {
                        asm_.add_rax_imm32(static_cast<int32_t>(i) * storageElemSize);
                    }
                    
                    // Store element
                    asm_.pop_rcx();  // Get element value
                    if (storageElemSize == 1) {
                        asm_.code.push_back(0x88);
                        asm_.code.push_back(0x08);  // mov [rax], cl
                    } else if (storageElemSize == 2) {
                        asm_.code.push_back(0x66);
                        asm_.code.push_back(0x89);
                        asm_.code.push_back(0x08);  // mov [rax], cx
                    } else if (storageElemSize == 4) {
                        asm_.code.push_back(0x89);
                        asm_.code.push_back(0x08);  // mov [rax], ecx
                    } else {
                        asm_.mov_mem_rax_rcx();  // mov [rax], rcx
                    }
                }
            }
        }
    }
}

void NativeCodeGen::emitRefinementCheck(const RefinementTypeInfo& info, SourceLocation loc) {
    // Value to check is in RAX
    // We need to evaluate the constraint expression with _ replaced by the value
    // If constraint fails, exit with error
    
    // Save the value
    asm_.push_rax();
    
    // The constraint expression uses PlaceholderExpr (_) to refer to the value
    // We need to evaluate it with RAX as the placeholder value
    // For now, we'll handle simple comparisons: _ > 0, _ >= 0, _ < N, etc.
    
    if (auto* binary = dynamic_cast<BinaryExpr*>(info.constraint)) {
        // Check if left side is placeholder
        bool leftIsPlaceholder = dynamic_cast<PlaceholderExpr*>(binary->left.get()) != nullptr;
        bool rightIsPlaceholder = dynamic_cast<PlaceholderExpr*>(binary->right.get()) != nullptr;
        
        if (leftIsPlaceholder && !rightIsPlaceholder) {
            // _ op value - value is in RAX (on stack), evaluate right side
            binary->right->accept(*this);
            asm_.mov_rcx_rax();  // Right value in RCX
            asm_.pop_rax();      // Restore left value (placeholder) to RAX
            asm_.push_rax();     // Save it again for later use
            
            // Compare RAX with RCX
            asm_.cmp_rax_rcx();
            
            // Generate appropriate jump based on operator
            std::string passLabel = newLabel("refine_pass");
            std::string failLabel = newLabel("refine_fail");
            
            switch (binary->op) {
                case TokenType::GT:
                    asm_.jg_rel32(passLabel);
                    break;
                case TokenType::GE:
                    asm_.jge_rel32(passLabel);
                    break;
                case TokenType::LT:
                    asm_.jl_rel32(passLabel);
                    break;
                case TokenType::LE:
                    asm_.jle_rel32(passLabel);
                    break;
                case TokenType::EQ:
                    asm_.jz_rel32(passLabel);
                    break;
                case TokenType::NE:
                    asm_.jnz_rel32(passLabel);
                    break;
                default:
                    // Unknown operator, skip check
                    asm_.pop_rax();
                    return;
            }
            
            // Fail path - print error and exit
            asm_.label(failLabel);
            
            // Print error message
            std::string errorMsg = "Refinement type constraint failed for type '" + info.name + "'\n";
            uint32_t errorRVA = addString(errorMsg);
            
            // Get stdout handle
            asm_.mov_rcx_imm64(static_cast<uint64_t>(-11));  // STD_OUTPUT_HANDLE
            if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
            asm_.call_mem_rip(pe_.getImportRVA("GetStdHandle"));
            if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
            asm_.mov_rcx_rax();  // Handle in RCX
            
            // Write error message
            asm_.lea_rax_rip_fixup(errorRVA);
            asm_.mov_rdx_rax();
            asm_.mov_r8_imm64(errorMsg.size());
            asm_.xor_r9_r9();  // lpNumberOfCharsWritten = NULL
            if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
            // Push 5th arg (lpReserved = NULL)
            asm_.code.push_back(0x48); asm_.code.push_back(0xC7);
            asm_.code.push_back(0x44); asm_.code.push_back(0x24);
            asm_.code.push_back(0x20); // [rsp+0x20]
            asm_.code.push_back(0x00); asm_.code.push_back(0x00);
            asm_.code.push_back(0x00); asm_.code.push_back(0x00);
            asm_.call_mem_rip(pe_.getImportRVA("WriteConsoleA"));
            if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
            
            // Exit with error code 1
            asm_.mov_rcx_imm64(1);
            asm_.call_mem_rip(pe_.getImportRVA("ExitProcess"));
            
            // Pass path - restore value and continue
            asm_.label(passLabel);
            asm_.pop_rax();
            
        } else if (!leftIsPlaceholder && rightIsPlaceholder) {
            // value op _ - evaluate left side, compare with RAX
            asm_.pop_rax();      // Restore placeholder value to RAX
            asm_.push_rax();     // Save it again
            asm_.mov_rcx_rax();  // Placeholder value in RCX
            
            binary->left->accept(*this);  // Left value in RAX
            
            // Compare RAX with RCX
            asm_.cmp_rax_rcx();
            
            std::string passLabel = newLabel("refine_pass");
            std::string failLabel = newLabel("refine_fail");
            
            switch (binary->op) {
                case TokenType::GT:
                    asm_.jg_rel32(passLabel);
                    break;
                case TokenType::GE:
                    asm_.jge_rel32(passLabel);
                    break;
                case TokenType::LT:
                    asm_.jl_rel32(passLabel);
                    break;
                case TokenType::LE:
                    asm_.jle_rel32(passLabel);
                    break;
                case TokenType::EQ:
                    asm_.jz_rel32(passLabel);
                    break;
                case TokenType::NE:
                    asm_.jnz_rel32(passLabel);
                    break;
                default:
                    asm_.pop_rax();
                    return;
            }
            
            // Fail path
            asm_.label(failLabel);
            std::string errorMsg = "Refinement type constraint failed for type '" + info.name + "'\n";
            uint32_t errorRVA = addString(errorMsg);
            
            asm_.mov_rcx_imm64(static_cast<uint64_t>(-11));
            if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
            asm_.call_mem_rip(pe_.getImportRVA("GetStdHandle"));
            if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
            asm_.mov_rcx_rax();
            asm_.lea_rax_rip_fixup(errorRVA);
            asm_.mov_rdx_rax();
            asm_.mov_r8_imm64(errorMsg.size());
            asm_.xor_r9_r9();
            if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
            asm_.code.push_back(0x48); asm_.code.push_back(0xC7);
            asm_.code.push_back(0x44); asm_.code.push_back(0x24);
            asm_.code.push_back(0x20);
            asm_.code.push_back(0x00); asm_.code.push_back(0x00);
            asm_.code.push_back(0x00); asm_.code.push_back(0x00);
            asm_.call_mem_rip(pe_.getImportRVA("WriteConsoleA"));
            if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
            
            asm_.mov_rcx_imm64(1);
            asm_.call_mem_rip(pe_.getImportRVA("ExitProcess"));
            
            asm_.label(passLabel);
            asm_.pop_rax();
        } else {
            // Both or neither are placeholders - not supported yet
            asm_.pop_rax();
        }
    } else {
        // Non-binary constraint - not supported yet
        asm_.pop_rax();
    }
    
    (void)loc;  // Suppress unused warning
}

bool NativeCodeGen::tryEvalRefinementConstraint(const RefinementTypeInfo& info, int64_t value) {
    // Try to evaluate the constraint at compile time with the given value
    auto* binary = dynamic_cast<BinaryExpr*>(info.constraint);
    if (!binary) return true;  // Can't evaluate, assume it passes
    
    bool leftIsPlaceholder = dynamic_cast<PlaceholderExpr*>(binary->left.get()) != nullptr;
    bool rightIsPlaceholder = dynamic_cast<PlaceholderExpr*>(binary->right.get()) != nullptr;
    
    int64_t left, right;
    
    if (leftIsPlaceholder && !rightIsPlaceholder) {
        left = value;
        if (!tryEvalConstant(binary->right.get(), right)) {
            return true;  // Can't evaluate right side, assume it passes
        }
    } else if (!leftIsPlaceholder && rightIsPlaceholder) {
        right = value;
        if (!tryEvalConstant(binary->left.get(), left)) {
            return true;  // Can't evaluate left side, assume it passes
        }
    } else {
        return true;  // Can't evaluate, assume it passes
    }
    
    // Evaluate the comparison
    switch (binary->op) {
        case TokenType::GT:  return left > right;
        case TokenType::GE:  return left >= right;
        case TokenType::LT:  return left < right;
        case TokenType::LE:  return left <= right;
        case TokenType::EQ:  return left == right;
        case TokenType::NE:  return left != right;
        default: return true;  // Unknown operator, assume it passes
    }
}

} // namespace tyl
