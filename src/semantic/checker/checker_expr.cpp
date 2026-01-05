// Tyl Compiler - Type Checker Expression Visitors
// Expression type checking

#include "checker_base.h"

namespace tyl {

TypePtr TypeChecker::commonType(TypePtr a, TypePtr b) {
    auto& reg = TypeRegistry::instance();
    if (a->kind == TypeKind::UNKNOWN) return b;
    if (b->kind == TypeKind::UNKNOWN) return a;
    if (a->equals(b.get())) return a;
    if (a->isNumeric() && b->isNumeric()) {
        if (a->isFloat() || b->isFloat()) return reg.floatType();
        return reg.intType();
    }
    return reg.anyType();
}

bool TypeChecker::isAssignable(TypePtr target, TypePtr source) {
    if (!target || !source) return false;
    if (target->kind == TypeKind::UNKNOWN || source->kind == TypeKind::UNKNOWN) return true;
    if (target->kind == TypeKind::ANY) return true;
    if (target->equals(source.get())) return true;
    // Special case: list types should be compared by element type
    if (target->kind == TypeKind::LIST && source->kind == TypeKind::LIST) {
        auto* targetList = static_cast<ListType*>(target.get());
        auto* sourceList = static_cast<ListType*>(source.get());
        // Recursively check element type assignability
        if (targetList->element && sourceList->element) {
            return isAssignable(targetList->element, sourceList->element);
        }
        return true;  // If either element is null, assume compatible
    }
    // Special case: map types
    if (target->kind == TypeKind::MAP && source->kind == TypeKind::MAP) {
        auto* targetMap = static_cast<MapType*>(target.get());
        auto* sourceMap = static_cast<MapType*>(source.get());
        return isAssignable(targetMap->key, sourceMap->key) && 
               isAssignable(targetMap->value, sourceMap->value);
    }
    // Special case: reference types (&T and &mut T)
    if (target->kind == TypeKind::REF && source->kind == TypeKind::REF) {
        auto* targetRef = static_cast<PtrType*>(target.get());
        auto* sourceRef = static_cast<PtrType*>(source.get());
        // &mut T can be passed where &T is expected (covariance)
        // But &T cannot be passed where &mut T is expected
        if (target->isMutable && !source->isMutable) {
            return false;  // Can't pass immutable ref where mutable is expected
        }
        return isAssignable(targetRef->pointee, sourceRef->pointee);
    }
    // Special case: pointer types (*T)
    if (target->kind == TypeKind::PTR && source->kind == TypeKind::PTR) {
        auto* targetPtr = static_cast<PtrType*>(target.get());
        auto* sourcePtr = static_cast<PtrType*>(source.get());
        return isAssignable(targetPtr->pointee, sourcePtr->pointee);
    }
    // Special case: REF to PTR or PTR to REF (both are pointer-like)
    if ((target->kind == TypeKind::REF || target->kind == TypeKind::PTR) &&
        (source->kind == TypeKind::REF || source->kind == TypeKind::PTR)) {
        auto* targetPtr = static_cast<PtrType*>(target.get());
        auto* sourcePtr = static_cast<PtrType*>(source.get());
        return isAssignable(targetPtr->pointee, sourcePtr->pointee);
    }
    if (target->isNumeric() && source->isNumeric()) {
        if (target->isFloat() && source->isInteger()) return true;
        if (target->size() >= source->size()) return true;
    }
    return false;
}

bool TypeChecker::isComparable(TypePtr a, TypePtr b) {
    if (a->kind == TypeKind::ANY || b->kind == TypeKind::ANY) return true;
    if (a->isNumeric() && b->isNumeric()) return true;
    if (a->kind == TypeKind::STRING && b->kind == TypeKind::STRING) return true;
    if (a->kind == TypeKind::BOOL && b->kind == TypeKind::BOOL) return true;
    return a->equals(b.get());
}

void TypeChecker::visit(IntegerLiteral& node) { 
    if (!node.suffix.empty()) {
        // Use the suffix to determine the type (e.g., "i32" -> int32Type)
        currentType_ = TypeRegistry::instance().fromString(node.suffix);
    } else {
        currentType_ = TypeRegistry::instance().intType(); 
    }
}
void TypeChecker::visit(FloatLiteral& node) { 
    if (!node.suffix.empty()) {
        // Use the suffix to determine the type (e.g., "f32" -> float32Type)
        currentType_ = TypeRegistry::instance().fromString(node.suffix);
    } else {
        currentType_ = TypeRegistry::instance().floatType(); 
    }
}
void TypeChecker::visit(StringLiteral&) { currentType_ = TypeRegistry::instance().stringType(); }
void TypeChecker::visit(CharLiteral&) { currentType_ = TypeRegistry::instance().charType(); }
void TypeChecker::visit(ByteStringLiteral&) { currentType_ = TypeRegistry::instance().byteArrayType(); }
void TypeChecker::visit(BoolLiteral&) { currentType_ = TypeRegistry::instance().boolType(); }
void TypeChecker::visit(NilLiteral&) {
    auto nilType = TypeRegistry::instance().unknownType();
    nilType->isNullable = true;
    currentType_ = nilType;
}

void TypeChecker::visit(Identifier& node) {
    Symbol* sym = symbols_.lookup(node.name);
    if (!sym) {
        error("Undefined identifier '" + node.name + "'", node.location);
        currentType_ = TypeRegistry::instance().errorType();
        return;
    }
    
    // Mark the variable as used
    sym->isUsed = true;
    
    // Ownership check: verify variable is usable
    if (sym->kind == SymbolKind::VARIABLE) {
        if (sym->ownershipState == OwnershipState::MOVED) {
            error("use of moved value '" + node.name + "' (moved at " + 
                  sym->moveLocation.filename + ":" + std::to_string(sym->moveLocation.line) + ")", 
                  node.location);
        } else if (sym->ownershipState == OwnershipState::UNINITIALIZED && !sym->isParameter) {
            error("use of uninitialized variable '" + node.name + "'", node.location);
        }
    }
    
    currentType_ = sym->type;
}


void TypeChecker::visit(BinaryExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr leftType = inferType(node.left.get());
    TypePtr rightType = inferType(node.right.get());
    
    // Check for raw pointer arithmetic - requires unsafe block
    // References (&T) are auto-dereferenced, so they don't count as pointer arithmetic
    auto isRawPointer = [](const TypePtr& t) -> bool {
        if (t->kind == TypeKind::PTR) {
            if (auto* ptr = dynamic_cast<PtrType*>(t.get())) {
                return ptr->isRaw;  // Only raw pointers (*T), not references (&T)
            }
        }
        return false;
    };
    
    bool isPointerArithmetic = (isRawPointer(leftType) || isRawPointer(rightType)) &&
                               (node.op == TokenType::PLUS || node.op == TokenType::MINUS);
    if (isPointerArithmetic && !symbols_.inUnsafe()) {
        error("Pointer arithmetic requires unsafe block", node.location);
    }
    
    // Auto-dereference references in arithmetic expressions
    // &int + 1 should be treated as int + 1, not pointer arithmetic
    auto derefIfRef = [](const TypePtr& t) -> TypePtr {
        if (t->kind == TypeKind::PTR) {
            if (auto* ptr = dynamic_cast<PtrType*>(t.get())) {
                if (!ptr->isRaw) {  // Reference type (&T), not raw pointer (*T)
                    return ptr->pointee;
                }
            }
        }
        return t;
    };
    
    TypePtr effectiveLeftType = derefIfRef(leftType);
    TypePtr effectiveRightType = derefIfRef(rightType);
    
    // If either operand is ANY, allow the operation and return ANY
    if (effectiveLeftType->kind == TypeKind::ANY || effectiveRightType->kind == TypeKind::ANY) {
        switch (node.op) {
            case TokenType::PLUS:
            case TokenType::MINUS:
            case TokenType::STAR:
            case TokenType::SLASH:
            case TokenType::PERCENT:
                currentType_ = reg.anyType();
                return;
            case TokenType::EQ: case TokenType::NE:
            case TokenType::LT: case TokenType::GT:
            case TokenType::LE: case TokenType::GE:
            case TokenType::AND: case TokenType::OR:
                currentType_ = reg.boolType();
                return;
            default:
                currentType_ = reg.anyType();
                return;
        }
    }
    
    // Handle pointer arithmetic result type (only for raw pointers)
    if (isPointerArithmetic) {
        // Pointer difference: ptr - ptr = int (element count between pointers)
        if (isRawPointer(leftType) && isRawPointer(rightType) && node.op == TokenType::MINUS) {
            currentType_ = reg.intType();  // ptr - ptr = int (element count)
            return;
        }
        if (isRawPointer(leftType)) {
            currentType_ = leftType;  // ptr + int = ptr, ptr - int = ptr
        } else {
            currentType_ = rightType;  // int + ptr = ptr
        }
        return;
    }
    
    switch (node.op) {
        case TokenType::PLUS:
            if (effectiveLeftType->kind == TypeKind::STRING || effectiveRightType->kind == TypeKind::STRING) {
                currentType_ = reg.stringType();
            } else if (effectiveLeftType->isNumeric() && effectiveRightType->isNumeric()) {
                currentType_ = commonType(effectiveLeftType, effectiveRightType);
            } else {
                error("Invalid operands for '+'", node.location);
                currentType_ = reg.errorType();
            }
            break;
        case TokenType::MINUS: case TokenType::STAR: case TokenType::SLASH: case TokenType::PERCENT:
            if (leftType->isNumeric() && rightType->isNumeric()) {
                currentType_ = commonType(leftType, rightType);
            } else {
                error("Arithmetic operators require numeric operands", node.location);
                currentType_ = reg.errorType();
            }
            break;
        case TokenType::EQ: case TokenType::NE:
            if (!isComparable(leftType, rightType)) warning("Comparing incompatible types", node.location);
            currentType_ = reg.boolType();
            break;
        case TokenType::LT: case TokenType::GT: case TokenType::LE: case TokenType::GE:
            currentType_ = reg.boolType();
            break;
        case TokenType::AND: case TokenType::OR:
            currentType_ = reg.boolType();
            break;
        default:
            currentType_ = reg.unknownType();
            break;
    }
}

void TypeChecker::visit(UnaryExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr operandType = inferType(node.operand.get());
    switch (node.op) {
        case TokenType::MINUS:
            currentType_ = operandType->isNumeric() ? operandType : reg.errorType();
            break;
        case TokenType::NOT: case TokenType::BANG:
            currentType_ = reg.boolType();
            break;
        default:
            currentType_ = reg.unknownType();
            break;
    }
}

void TypeChecker::visit(CallExpr& node) {
    auto& reg = TypeRegistry::instance();
    
    // Check for unsafe operations: alloc(), free(), stackalloc(), placement_new(), gc_pin(), gc_unpin(), gc_add_root(), gc_remove_root(), set_allocator(), memcpy(), memset(), memmove(), memcmp() require unsafe block
    if (auto* id = dynamic_cast<Identifier*>(node.callee.get())) {
        if (id->name == "alloc" || id->name == "free" || 
            id->name == "stackalloc" || id->name == "placement_new" ||
            id->name == "gc_pin" || id->name == "gc_unpin" ||
            id->name == "gc_add_root" || id->name == "gc_remove_root" ||
            id->name == "set_allocator" ||
            id->name == "memcpy" || id->name == "memset" ||
            id->name == "memmove" || id->name == "memcmp") {
            if (!symbols_.inUnsafe()) {
                error("'" + id->name + "' requires unsafe block", node.location);
            }
        }
        
        // Special handling for type introspection operators
        // sizeof(T), alignof(T), offsetof(Record, field) take type/field names as arguments
        // These should not be checked as regular identifiers
        if (id->name == "sizeof" || id->name == "alignof") {
            // These take a type name as argument - return int
            currentType_ = reg.intType();
            return;
        }
        if (id->name == "offsetof") {
            // Takes record type name and field name - return int
            currentType_ = reg.intType();
            return;
        }
    }
    
    TypePtr calleeType = inferType(node.callee.get());
    
    if (calleeType->kind == TypeKind::FUNCTION) {
        auto* fnType = static_cast<FunctionType*>(calleeType.get());
        
        // Handle generic function instantiation
        if (!fnType->typeParams.empty()) {
            // Try to infer type arguments from call arguments
            std::vector<TypePtr> inferredTypeArgs;
            std::unordered_map<std::string, TypePtr> typeArgMap;
            
            // Infer type arguments from argument types
            for (size_t i = 0; i < node.args.size() && i < fnType->params.size(); i++) {
                TypePtr argType = inferType(node.args[i].get());
                TypePtr paramType = fnType->params[i].second;
                
                // If param is a type parameter, bind it
                if (paramType->kind == TypeKind::TYPE_PARAM) {
                    auto* tp = static_cast<TypeParamType*>(paramType.get());
                    auto it = typeArgMap.find(tp->name);
                    if (it == typeArgMap.end()) {
                        typeArgMap[tp->name] = argType;
                    } else {
                        // Unify with existing binding
                        typeArgMap[tp->name] = unify(it->second, argType, node.location);
                    }
                }
            }
            
            // Build type args in order
            for (const auto& tpName : fnType->typeParams) {
                auto it = typeArgMap.find(tpName);
                if (it != typeArgMap.end()) {
                    inferredTypeArgs.push_back(it->second);
                } else {
                    inferredTypeArgs.push_back(reg.anyType());  // Couldn't infer
                }
            }
            
            // Instantiate the generic function
            TypePtr instantiated = instantiateGenericFunction(fnType, inferredTypeArgs, node.location);
            if (instantiated->kind == TypeKind::FUNCTION) {
                auto* instFn = static_cast<FunctionType*>(instantiated.get());
                currentType_ = instFn->returnType ? instFn->returnType : reg.voidType();
                return;
            }
        }
        
        // Non-generic function call passed arguments
        for (size_t i = 0; i < node.args.size(); i++) {
            TypePtr argType = inferType(node.args[i].get());
            
            // Only check against parameters if we have a corresponding parameter
            if (i < fnType->params.size()) {
                TypePtr paramType = fnType->params[i].second;
                
                if (!isAssignable(paramType, argType)) {
                    error("Argument type mismatch: expected '" + paramType->toString() + 
                          "', got '" + argType->toString() + "'", node.args[i]->location);
                }
            }
        }
        currentType_ = fnType->returnType ? fnType->returnType : reg.voidType();
    } else if (calleeType->kind == TypeKind::PTR) {
        // Handle function pointer calls: *fn(int) -> int
        auto* ptrType = static_cast<PtrType*>(calleeType.get());
        if (ptrType->pointee && ptrType->pointee->kind == TypeKind::FUNCTION) {
            auto* fnType = static_cast<FunctionType*>(ptrType->pointee.get());
            
            // Visit all arguments to mark them as used
            for (size_t i = 0; i < node.args.size(); i++) {
                TypePtr argType = inferType(node.args[i].get());
                
                // Only check against parameters if we have a corresponding parameter
                if (i < fnType->params.size()) {
                    TypePtr paramType = fnType->params[i].second;
                    
                    if (!isAssignable(paramType, argType)) {
                        error("Argument type mismatch: expected '" + paramType->toString() + 
                              "', got '" + argType->toString() + "'", node.args[i]->location);
                    }
                }
            }
            currentType_ = fnType->returnType ? fnType->returnType : reg.voidType();
        } else {
            // Pointer but not to a function - still visit args
            for (auto& arg : node.args) inferType(arg.get());
            currentType_ = reg.anyType();
        }
    } else if (calleeType->kind == TypeKind::ANY) {
        for (auto& arg : node.args) inferType(arg.get());
        currentType_ = reg.anyType();
    } else {
        // Unknown callee type - still visit args to mark them as used
        for (auto& arg : node.args) inferType(arg.get());
        currentType_ = reg.errorType();
    }
}

void TypeChecker::visit(MemberExpr& node) {
    auto& reg = TypeRegistry::instance();
    
    // Check if this is an enum member access (e.g., Status.Ok)
    if (auto* id = dynamic_cast<Identifier*>(node.object.get())) {
        // Check if the identifier is an enum type
        TypePtr enumType = symbols_.lookupType(id->name);
        if (enumType) {
            // Look up the qualified enum variant name
            std::string qualifiedName = id->name + "." + node.member;
            Symbol* variantSym = symbols_.lookup(qualifiedName);
            if (variantSym) {
                currentType_ = variantSym->type;
                return;
            }
        }
        
        // Also check if it's a module member access
        Symbol* moduleSym = symbols_.lookup(id->name);
        if (moduleSym && moduleSym->kind == SymbolKind::MODULE) {
            std::string qualifiedName = id->name + "." + node.member;
            Symbol* memberSym = symbols_.lookup(qualifiedName);
            if (memberSym) {
                currentType_ = memberSym->type;
                return;
            }
        }
    }
    
    TypePtr objType = inferType(node.object.get());
    
    // Handle .clone() method for explicit deep copy (ownership system)
    if (node.member == "clone") {
        // clone() returns a new owned copy of the value
        // This prevents the move and creates a deep copy
        auto fnType = std::make_shared<FunctionType>();
        fnType->returnType = objType;
        currentType_ = fnType;
        return;
    }
    
    // Handle atomic type method access
    if (objType->kind == TypeKind::ATOMIC) {
        auto* at = static_cast<AtomicType*>(objType.get());
        // Atomic methods: load, store, swap, cas, add, sub, and, or, xor
        if (node.member == "load") {
            // load() returns the element type
            auto fnType = std::make_shared<FunctionType>();
            fnType->returnType = at->element;
            currentType_ = fnType;
            return;
        }
        if (node.member == "store") {
            // store(v) returns void
            auto fnType = std::make_shared<FunctionType>();
            fnType->params.push_back({"value", at->element});
            fnType->returnType = reg.voidType();
            currentType_ = fnType;
            return;
        }
        if (node.member == "swap") {
            // swap(v) returns old value (element type)
            auto fnType = std::make_shared<FunctionType>();
            fnType->params.push_back({"value", at->element});
            fnType->returnType = at->element;
            currentType_ = fnType;
            return;
        }
        if (node.member == "cas") {
            // cas(expected, desired) returns bool (1 if success)
            auto fnType = std::make_shared<FunctionType>();
            fnType->params.push_back({"expected", at->element});
            fnType->params.push_back({"desired", at->element});
            fnType->returnType = reg.intType();
            currentType_ = fnType;
            return;
        }
        if (node.member == "add" || node.member == "sub" ||
            node.member == "and" || node.member == "or" || node.member == "xor" ||
            node.member == "fetch_and" || node.member == "fetch_or" || node.member == "fetch_xor") {
            // These return old value (element type)
            auto fnType = std::make_shared<FunctionType>();
            fnType->params.push_back({"value", at->element});
            fnType->returnType = at->element;
            currentType_ = fnType;
            return;
        }
    }
    
    // Handle Box type method access
    if (objType->kind == TypeKind::BOX) {
        auto* bt = static_cast<BoxType*>(objType.get());
        if (node.member == "deref" || node.member == "get") {
            // deref() / get() returns the element type
            auto fnType = std::make_shared<FunctionType>();
            fnType->returnType = bt->element;
            currentType_ = fnType;
            return;
        }
        if (node.member == "into_inner") {
            // into_inner() consumes the Box and returns the inner value
            auto fnType = std::make_shared<FunctionType>();
            fnType->returnType = bt->element;
            currentType_ = fnType;
            return;
        }
    }
    
    // Handle Rc type method access
    if (objType->kind == TypeKind::RC) {
        auto* rt = static_cast<RcType*>(objType.get());
        if (node.member == "deref" || node.member == "get") {
            // deref() / get() returns the element type
            auto fnType = std::make_shared<FunctionType>();
            fnType->returnType = rt->element;
            currentType_ = fnType;
            return;
        }
        if (node.member == "clone") {
            // clone() returns a new Rc with incremented refcount
            auto fnType = std::make_shared<FunctionType>();
            fnType->returnType = objType;
            currentType_ = fnType;
            return;
        }
        if (node.member == "strong_count") {
            // strong_count() returns the reference count
            auto fnType = std::make_shared<FunctionType>();
            fnType->returnType = reg.intType();
            currentType_ = fnType;
            return;
        }
        if (node.member == "downgrade") {
            // downgrade() returns a Weak reference
            auto fnType = std::make_shared<FunctionType>();
            fnType->returnType = reg.weakType(rt->element, false);
            currentType_ = fnType;
            return;
        }
    }
    
    // Handle Arc type method access
    if (objType->kind == TypeKind::ARC) {
        auto* at = static_cast<ArcType*>(objType.get());
        if (node.member == "deref" || node.member == "get") {
            // deref() / get() returns the element type
            auto fnType = std::make_shared<FunctionType>();
            fnType->returnType = at->element;
            currentType_ = fnType;
            return;
        }
        if (node.member == "clone") {
            // clone() returns a new Arc with atomically incremented refcount
            auto fnType = std::make_shared<FunctionType>();
            fnType->returnType = objType;
            currentType_ = fnType;
            return;
        }
        if (node.member == "strong_count") {
            // strong_count() returns the reference count
            auto fnType = std::make_shared<FunctionType>();
            fnType->returnType = reg.intType();
            currentType_ = fnType;
            return;
        }
        if (node.member == "downgrade") {
            // downgrade() returns a Weak reference
            auto fnType = std::make_shared<FunctionType>();
            fnType->returnType = reg.weakType(at->element, true);
            currentType_ = fnType;
            return;
        }
    }
    
    // Handle Weak type method access
    if (objType->kind == TypeKind::WEAK) {
        auto* wt = static_cast<WeakType*>(objType.get());
        if (node.member == "upgrade") {
            // upgrade() returns Option[Rc[T]] or Option[Arc[T]] (nil if deallocated)
            auto fnType = std::make_shared<FunctionType>();
            if (wt->isAtomic) {
                fnType->returnType = reg.arcType(wt->element);
            } else {
                fnType->returnType = reg.rcType(wt->element);
            }
            fnType->returnType->isNullable = true;  // Can return nil
            currentType_ = fnType;
            return;
        }
        if (node.member == "strong_count") {
            // strong_count() returns the strong reference count (0 if deallocated)
            auto fnType = std::make_shared<FunctionType>();
            fnType->returnType = reg.intType();
            currentType_ = fnType;
            return;
        }
    }
    
    // Handle Cell type method access
    if (objType->kind == TypeKind::CELL) {
        auto* ct = static_cast<CellType*>(objType.get());
        if (node.member == "get") {
            // get() returns a copy of the value
            auto fnType = std::make_shared<FunctionType>();
            fnType->returnType = ct->element;
            currentType_ = fnType;
            return;
        }
        if (node.member == "set") {
            // set(v) sets the value, returns void
            auto fnType = std::make_shared<FunctionType>();
            fnType->params.push_back({"value", ct->element});
            fnType->returnType = reg.voidType();
            currentType_ = fnType;
            return;
        }
        if (node.member == "replace") {
            // replace(v) sets the value and returns the old value
            auto fnType = std::make_shared<FunctionType>();
            fnType->params.push_back({"value", ct->element});
            fnType->returnType = ct->element;
            currentType_ = fnType;
            return;
        }
    }
    
    // Handle RefCell type method access
    if (objType->kind == TypeKind::REFCELL) {
        auto* rct = static_cast<RefCellType*>(objType.get());
        if (node.member == "borrow") {
            // borrow() returns an immutable reference to the value
            auto fnType = std::make_shared<FunctionType>();
            fnType->returnType = reg.refType(rct->element);
            currentType_ = fnType;
            return;
        }
        if (node.member == "borrow_mut") {
            // borrow_mut() returns a mutable reference to the value
            auto fnType = std::make_shared<FunctionType>();
            auto refType = reg.refType(rct->element);
            refType->isMutable = true;
            fnType->returnType = refType;
            currentType_ = fnType;
            return;
        }
        if (node.member == "get") {
            // get() returns a copy of the value (for Copy types)
            auto fnType = std::make_shared<FunctionType>();
            fnType->returnType = rct->element;
            currentType_ = fnType;
            return;
        }
        if (node.member == "set") {
            // set(v) sets the value, returns void
            auto fnType = std::make_shared<FunctionType>();
            fnType->params.push_back({"value", rct->element});
            fnType->returnType = reg.voidType();
            currentType_ = fnType;
            return;
        }
    }
    
    if (objType->kind == TypeKind::RECORD) {
        auto* recType = static_cast<RecordType*>(objType.get());
        TypePtr fieldType = recType->getField(node.member);
        currentType_ = fieldType ? fieldType : reg.errorType();
    } else {
        currentType_ = reg.anyType();
    }
}

void TypeChecker::visit(IndexExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr objType = inferType(node.object.get());
    TypePtr indexType = inferType(node.index.get());
    
    // Check if index is a range expression (for slicing)
    bool isRangeIndex = dynamic_cast<RangeExpr*>(node.index.get()) != nullptr ||
                        dynamic_cast<InclusiveRangeExpr*>(node.index.get()) != nullptr;
    
    if (objType->kind == TypeKind::LIST) {
        if (isRangeIndex) {
            // List slicing returns a new list
            currentType_ = objType;
        } else {
            currentType_ = static_cast<ListType*>(objType.get())->element;
        }
    } else if (objType->kind == TypeKind::STRING) {
        if (isRangeIndex) {
            // String slicing returns a str_view
            currentType_ = reg.strViewType();
        } else {
            // Single character access returns a string (for now)
            currentType_ = reg.stringType();
        }
    } else if (objType->kind == TypeKind::STR_VIEW) {
        if (isRangeIndex) {
            // str_view slicing returns another str_view
            currentType_ = reg.strViewType();
        } else {
            // Single character access returns a string
            currentType_ = reg.stringType();
        }
    } else {
        currentType_ = reg.anyType();
    }
}

void TypeChecker::visit(ListExpr& node) {
    auto& reg = TypeRegistry::instance();
    if (node.elements.empty()) {
        currentType_ = reg.listType(reg.unknownType());
        return;
    }
    TypePtr elemType = inferType(node.elements[0].get());
    for (size_t i = 1; i < node.elements.size(); i++) {
        elemType = commonType(elemType, inferType(node.elements[i].get()));
    }
    currentType_ = reg.listType(elemType);
}

void TypeChecker::visit(RecordExpr& node) {
    // If the record has a type name (e.g., Point{x: 1, y: 2}), look up the declared type
    if (!node.typeName.empty()) {
        TypePtr declaredType = symbols_.lookupType(node.typeName);
        if (declaredType && declaredType->kind == TypeKind::RECORD) {
            auto* recType = static_cast<RecordType*>(declaredType.get());
            // Type check each field against the declared type
            for (auto& field : node.fields) {
                TypePtr fieldType = inferType(field.second.get());
                // Find the field in the declared type and check compatibility
                for (const auto& declField : recType->fields) {
                    if (declField.name == field.first) {
                        if (!isAssignable(declField.type, fieldType)) {
                            error("Field '" + field.first + "' type mismatch: expected '" + 
                                  declField.type->toString() + "', got '" + fieldType->toString() + "'",
                                  node.location);
                        }
                        break;
                    }
                }
            }
            currentType_ = declaredType;
            return;
        }
    }
    
    // Anonymous record - create a new record type from the fields
    auto recType = std::make_shared<RecordType>();
    for (auto& field : node.fields) {
        TypePtr fieldType = inferType(field.second.get());
        recType->fields.push_back({field.first, fieldType, false});
    }
    currentType_ = recType;
}

void TypeChecker::visit(MapExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr keyType = reg.stringType();  // Maps always have string keys for now
    TypePtr valueType = reg.anyType();
    
    if (!node.entries.empty()) {
        // Infer value type from first entry
        valueType = inferType(node.entries[0].second.get());
    }
    
    // For now, represent map as a generic type
    // A proper implementation would have a MapType
    currentType_ = reg.anyType();
}

void TypeChecker::visit(RangeExpr& node) {
    auto& reg = TypeRegistry::instance();
    inferType(node.start.get());
    inferType(node.end.get());
    if (node.step) inferType(node.step.get());
    currentType_ = reg.listType(reg.intType());
}

void TypeChecker::visit(LambdaExpr& node) {
    auto fnType = std::make_shared<FunctionType>();
    auto& reg = TypeRegistry::instance();
    symbols_.pushScope(Scope::Kind::FUNCTION);
    for (auto& p : node.params) {
        TypePtr ptype = parseTypeAnnotation(p.second);
        if (ptype->kind == TypeKind::UNKNOWN) ptype = reg.anyType();
        fnType->params.push_back(std::make_pair(p.first, ptype));
        Symbol sym(p.first, SymbolKind::PARAMETER, ptype);
        symbols_.define(sym);
    }
    TypePtr bodyType = inferType(node.body.get());
    fnType->returnType = bodyType;
    symbols_.popScope();
    currentType_ = fnType;
}

void TypeChecker::visit(TernaryExpr& node) {
    inferType(node.condition.get());
    TypePtr thenType = inferType(node.thenExpr.get());
    TypePtr elseType = inferType(node.elseExpr.get());
    currentType_ = commonType(thenType, elseType);
}

void TypeChecker::visit(ListCompExpr& node) {
    auto& reg = TypeRegistry::instance();
    symbols_.pushScope(Scope::Kind::BLOCK);
    TypePtr iterType = inferType(node.iterable.get());
    TypePtr elemType = reg.anyType();
    if (iterType->kind == TypeKind::LIST) {
        elemType = static_cast<ListType*>(iterType.get())->element;
    }
    Symbol varSym(node.var, SymbolKind::VARIABLE, elemType);
    symbols_.define(varSym);
    if (node.condition) inferType(node.condition.get());
    TypePtr exprType = inferType(node.expr.get());
    symbols_.popScope();
    currentType_ = reg.listType(exprType);
}

void TypeChecker::visit(AddressOfExpr& node) {
    auto& reg = TypeRegistry::instance();
    // Address-of operator requires unsafe block for raw pointer creation
    if (!symbols_.inUnsafe()) {
        error("Address-of operator '&' requires unsafe block", node.location);
    }
    TypePtr operandType = inferType(node.operand.get());
    currentType_ = reg.ptrType(operandType, true);
}

void TypeChecker::visit(BorrowExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr operandType = inferType(node.operand.get());
    
    // Borrow creates a reference type (safe, no unsafe required)
    // &x creates &T, &mut x creates &mut T
    auto refType = reg.refType(operandType);
    refType->isMutable = node.isMutable;
    
    // Track the borrow in the ownership system
    if (auto* id = dynamic_cast<Identifier*>(node.operand.get())) {
        Symbol* sym = symbols_.lookup(id->name);
        if (sym && sym->kind == SymbolKind::VARIABLE) {
            if (node.isMutable) {
                // Mutable borrow - check that variable is mutable
                if (!sym->isMutable) {
                    error("cannot borrow '" + id->name + "' as mutable, as it is not declared as mutable", node.location);
                }
                // Check for existing borrows
                if (sym->ownershipState == OwnershipState::BORROWED_SHARED) {
                    error("cannot borrow '" + id->name + "' as mutable because it is already borrowed", node.location);
                }
                if (sym->ownershipState == OwnershipState::BORROWED_MUT) {
                    error("cannot borrow '" + id->name + "' as mutable more than once at a time", node.location);
                }
                sym->ownershipState = OwnershipState::BORROWED_MUT;
            } else {
                // Immutable borrow
                if (sym->ownershipState == OwnershipState::BORROWED_MUT) {
                    error("cannot borrow '" + id->name + "' as immutable because it is already borrowed as mutable", node.location);
                }
                sym->ownershipState = OwnershipState::BORROWED_SHARED;
            }
            
            // Lifetime constraint enforcement
            // Track the borrow in the ownership tracker with lifetime info
            if (borrowCheckEnabled_) {
                // Get the lifetime of the borrowed value
                const OwnershipInfo* borrowedInfo = ownership_.getInfo(id->name);
                if (borrowedInfo) {
                    // Create a lifetime for this borrow based on current scope
                    Lifetime borrowLifetime = ownership_.createLifetime("'borrow");
                    
                    // Check that the borrowed value outlives the borrow
                    auto lifetimeError = ownership_.checkLifetimeValid(borrowLifetime, borrowedInfo->lifetime, node.location);
                    if (lifetimeError) {
                        error(*lifetimeError, node.location);
                    }
                    
                    // Record the borrow with lifetime tracking
                    auto borrowError = ownership_.recordBorrow(id->name, "_borrow", node.isMutable, 
                                                                node.location, symbols_.scopeDepth(), borrowLifetime);
                    if (borrowError) {
                        error(*borrowError, node.location);
                    }
                }
            }
        }
    }
    
    currentType_ = refType;
}

void TypeChecker::visit(DerefExpr& node) {
    auto& reg = TypeRegistry::instance();
    // Pointer dereference requires unsafe block
    if (!symbols_.inUnsafe()) {
        error("Pointer dereference '*' requires unsafe block", node.location);
    }
    TypePtr operandType = inferType(node.operand.get());
    if (operandType->isPointer()) {
        currentType_ = static_cast<PtrType*>(operandType.get())->pointee;
    } else {
        error("Cannot dereference non-pointer type", node.location);
        currentType_ = reg.errorType();
    }
}

void TypeChecker::visit(NewExpr& node) {
    auto& reg = TypeRegistry::instance();
    // Raw allocation with 'new' requires unsafe block
    if (!symbols_.inUnsafe()) {
        error("'new' expression requires unsafe block", node.location);
    }
    TypePtr allocType = symbols_.lookupType(node.typeName);
    if (!allocType) allocType = reg.fromString(node.typeName);
    for (auto& arg : node.args) inferType(arg.get());
    currentType_ = reg.ptrType(allocType, true);
}

void TypeChecker::visit(CastExpr& node) {
    TypePtr sourceType = inferType(node.expr.get());
    TypePtr targetType = parseTypeAnnotation(node.targetType);
    
    // Pointer casting requires unsafe block
    bool isPointerCast = (sourceType->isPointer() && targetType->isPointer()) ||
                         (sourceType->isPointer() && targetType->kind == TypeKind::INT) ||
                         (sourceType->kind == TypeKind::INT && targetType->isPointer());
    if (isPointerCast && !symbols_.inUnsafe()) {
        error("Pointer casting requires unsafe block", node.location);
    }
    
    currentType_ = targetType;
}

void TypeChecker::visit(InterpolatedString& node) {
    auto& reg = TypeRegistry::instance();
    for (auto& part : node.parts) {
        if (auto* exprPtr = std::get_if<ExprPtr>(&part)) {
            inferType(exprPtr->get());
        }
    }
    currentType_ = reg.stringType();
}

void TypeChecker::visit(AwaitExpr& node) {
    TypePtr operandType = inferType(node.operand.get());
    // For now, await just returns the operand type
    currentType_ = operandType;
}

void TypeChecker::visit(SpawnExpr& node) {
    TypePtr operandType = inferType(node.operand.get());
    // Spawn returns a future/task type - for now use any
    currentType_ = TypeRegistry::instance().anyType();
}

void TypeChecker::visit(DSLBlock& node) {
    (void)node;
    // DSL blocks are opaque - return string type
    currentType_ = TypeRegistry::instance().stringType();
}

void TypeChecker::visit(AssignExpr& node) {
    // Check for pointer dereference assignment (*ptr = value) - requires unsafe block
    if (dynamic_cast<DerefExpr*>(node.target.get())) {
        if (!symbols_.inUnsafe()) {
            error("Pointer dereference assignment requires unsafe block", node.location);
        }
    }
    
    // For assignment targets, we need to get the type without triggering ownership errors
    // because we're about to reassign the variable (which restores ownership)
    TypePtr targetType;
    Symbol* targetSym = nullptr;
    
    if (auto* id = dynamic_cast<Identifier*>(node.target.get())) {
        targetSym = symbols_.lookup(id->name);
        if (!targetSym) {
            // This is a new variable declaration via assignment
            TypePtr valueType = inferType(node.value.get());
            Symbol sym(id->name, SymbolKind::VARIABLE, valueType);
            sym.isInitialized = true;
            sym.isMutable = true;  // Variables declared via = are mutable by default
            sym.ownershipState = OwnershipState::OWNED;
            sym.isCopyType = valueType->isPrimitive() || valueType->isPointer();
            sym.needsDrop = !sym.isCopyType && (valueType->kind == TypeKind::LIST || 
                                                 valueType->kind == TypeKind::STRING ||
                                                 valueType->kind == TypeKind::MAP ||
                                                 valueType->kind == TypeKind::RECORD);
            
            // Check if initializer is a move from another variable
            if (auto* srcId = dynamic_cast<Identifier*>(node.value.get())) {
                Symbol* srcSym = symbols_.lookup(srcId->name);
                if (srcSym && srcSym->kind == SymbolKind::VARIABLE) {
                    if (srcSym->ownershipState == OwnershipState::MOVED) {
                        error("use of moved value '" + srcId->name + "'", node.value->location);
                    } else if (srcSym->ownershipState == OwnershipState::UNINITIALIZED) {
                        error("use of uninitialized variable '" + srcId->name + "'", node.value->location);
                    } else if (!srcSym->isCopyType && srcSym->ownershipState == OwnershipState::OWNED) {
                        if (srcSym->borrowCount > 0) {
                            error("cannot move '" + srcId->name + "' while borrowed", node.value->location);
                        } else {
                            srcSym->ownershipState = OwnershipState::MOVED;
                            srcSym->moveLocation = node.location;
                        }
                    }
                }
            }
            
            symbols_.define(sym);
            currentType_ = valueType;
            return;
        }
        targetType = targetSym->type;
        targetSym->isUsed = true;
    } else {
        targetType = inferType(node.target.get());
    }
    
    TypePtr valueType = inferType(node.value.get());
    
    if (targetSym) {
        if (!targetSym->isMutable) {
            error("Cannot assign to immutable variable", node.location);
        }
        
        // Ownership: check if we're assigning from a moved value
        if (auto* srcId = dynamic_cast<Identifier*>(node.value.get())) {
            Symbol* srcSym = symbols_.lookup(srcId->name);
            if (srcSym && srcSym->kind == SymbolKind::VARIABLE) {
                if (srcSym->ownershipState == OwnershipState::MOVED) {
                    error("use of moved value '" + srcId->name + "'", node.value->location);
                } else if (srcSym->ownershipState == OwnershipState::UNINITIALIZED) {
                    error("use of uninitialized variable '" + srcId->name + "'", node.value->location);
                } else if (!srcSym->isCopyType && srcSym->ownershipState == OwnershipState::OWNED) {
                    if (srcSym->borrowCount > 0) {
                        error("cannot move '" + srcId->name + "' while borrowed", node.value->location);
                    } else {
                        srcSym->ownershipState = OwnershipState::MOVED;
                        srcSym->moveLocation = node.location;
                    }
                }
            }
        }
        
        // Target becomes owned again after assignment
        targetSym->ownershipState = OwnershipState::OWNED;
        targetSym->isInitialized = true;
    }
    
    if (!isAssignable(targetType, valueType)) {
        error("Type mismatch in assignment: cannot assign '" + valueType->toString() + 
              "' to '" + targetType->toString() + "'", node.location);
    }
    currentType_ = targetType;
}

void TypeChecker::visit(PropagateExpr& node) {
    // The ? operator unwraps a Result type
    // For now, we just infer the type of the operand
    // A full implementation would check that operand is Result[T, E] and return T
    TypePtr operandType = inferType(node.operand.get());
    // For simplicity, assume the unwrapped type is the same as the operand
    // (proper Result type handling would extract the Ok type)
    currentType_ = operandType;
}

void TypeChecker::visit(ChanSendExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr chanType = inferType(node.channel.get());
    TypePtr valueType = inferType(node.value.get());
    
    // Check that channel is actually a channel type
    if (chanType->kind != TypeKind::CHANNEL) {
        error("Cannot send to non-channel type '" + chanType->toString() + "'", node.location);
        currentType_ = reg.voidType();
        return;
    }
    
    auto* ch = static_cast<ChannelType*>(chanType.get());
    if (!isAssignable(ch->element, valueType)) {
        error("Cannot send '" + valueType->toString() + "' to channel of type '" + 
              ch->element->toString() + "'", node.location);
    }
    
    // Send returns void (or could return bool for non-blocking)
    currentType_ = reg.voidType();
}

void TypeChecker::visit(ChanRecvExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr chanType = inferType(node.channel.get());
    
    // Check that channel is actually a channel type
    if (chanType->kind != TypeKind::CHANNEL) {
        error("Cannot receive from non-channel type '" + chanType->toString() + "'", node.location);
        currentType_ = reg.anyType();
        return;
    }
    
    auto* ch = static_cast<ChannelType*>(chanType.get());
    // Receive returns the element type
    currentType_ = ch->element;
}

void TypeChecker::visit(MakeChanExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr elemType = parseTypeAnnotation(node.elementType);
    if (elemType->kind == TypeKind::UNKNOWN) {
        elemType = reg.anyType();
    }
    currentType_ = reg.channelType(elemType, node.bufferSize);
}

void TypeChecker::visit(MakeMutexExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr elemType = parseTypeAnnotation(node.elementType);
    if (elemType->kind == TypeKind::UNKNOWN) {
        elemType = reg.anyType();
    }
    currentType_ = reg.mutexType(elemType);
}

void TypeChecker::visit(MakeRWLockExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr elemType = parseTypeAnnotation(node.elementType);
    if (elemType->kind == TypeKind::UNKNOWN) {
        elemType = reg.anyType();
    }
    currentType_ = reg.rwlockType(elemType);
}

void TypeChecker::visit(MakeCondExpr& node) {
    (void)node;
    auto& reg = TypeRegistry::instance();
    currentType_ = reg.condType();
}

void TypeChecker::visit(MakeSemaphoreExpr& node) {
    (void)node;
    auto& reg = TypeRegistry::instance();
    currentType_ = reg.semaphoreType();
}

void TypeChecker::visit(MutexLockExpr& node) {
    auto& reg = TypeRegistry::instance();
    node.mutex->accept(*this);
    // Lock returns void
    currentType_ = reg.voidType();
}

void TypeChecker::visit(MutexUnlockExpr& node) {
    auto& reg = TypeRegistry::instance();
    node.mutex->accept(*this);
    currentType_ = reg.voidType();
}

void TypeChecker::visit(RWLockReadExpr& node) {
    auto& reg = TypeRegistry::instance();
    node.rwlock->accept(*this);
    currentType_ = reg.voidType();
}

void TypeChecker::visit(RWLockWriteExpr& node) {
    auto& reg = TypeRegistry::instance();
    node.rwlock->accept(*this);
    currentType_ = reg.voidType();
}

void TypeChecker::visit(RWLockUnlockExpr& node) {
    auto& reg = TypeRegistry::instance();
    node.rwlock->accept(*this);
    currentType_ = reg.voidType();
}

void TypeChecker::visit(CondWaitExpr& node) {
    auto& reg = TypeRegistry::instance();
    node.cond->accept(*this);
    node.mutex->accept(*this);
    currentType_ = reg.voidType();
}

void TypeChecker::visit(CondSignalExpr& node) {
    auto& reg = TypeRegistry::instance();
    node.cond->accept(*this);
    currentType_ = reg.voidType();
}

void TypeChecker::visit(CondBroadcastExpr& node) {
    auto& reg = TypeRegistry::instance();
    node.cond->accept(*this);
    currentType_ = reg.voidType();
}

void TypeChecker::visit(SemAcquireExpr& node) {
    auto& reg = TypeRegistry::instance();
    node.sem->accept(*this);
    currentType_ = reg.voidType();
}

void TypeChecker::visit(SemReleaseExpr& node) {
    auto& reg = TypeRegistry::instance();
    node.sem->accept(*this);
    currentType_ = reg.voidType();
}

void TypeChecker::visit(SemTryAcquireExpr& node) {
    auto& reg = TypeRegistry::instance();
    node.sem->accept(*this);
    // Returns bool (1 if acquired, 0 if not)
    currentType_ = reg.boolType();
}

void TypeChecker::visit(MakeAtomicExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr elemType = parseTypeAnnotation(node.elementType);
    if (elemType->kind == TypeKind::UNKNOWN) {
        elemType = reg.intType();  // Default to int
    }
    // Validate that element type is an integer type
    if (!elemType->isInteger()) {
        error("Atomic type requires integer element type, got '" + elemType->toString() + "'", node.location);
    }
    // Type check the initial value
    if (node.initialValue) {
        TypePtr initType = inferType(node.initialValue.get());
        if (!isAssignable(elemType, initType)) {
            error("Atomic initial value type mismatch: expected '" + elemType->toString() + 
                  "', got '" + initType->toString() + "'", node.location);
        }
    }
    currentType_ = reg.atomicType(elemType);
}

void TypeChecker::visit(AtomicLoadExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr atomicType = inferType(node.atomic.get());
    
    if (atomicType->kind != TypeKind::ATOMIC) {
        error("Cannot load from non-atomic type '" + atomicType->toString() + "'", node.location);
        currentType_ = reg.intType();
        return;
    }
    
    auto* at = static_cast<AtomicType*>(atomicType.get());
    currentType_ = at->element;
}

void TypeChecker::visit(AtomicStoreExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr atomicType = inferType(node.atomic.get());
    TypePtr valueType = inferType(node.value.get());
    
    if (atomicType->kind != TypeKind::ATOMIC) {
        error("Cannot store to non-atomic type '" + atomicType->toString() + "'", node.location);
        currentType_ = reg.voidType();
        return;
    }
    
    auto* at = static_cast<AtomicType*>(atomicType.get());
    if (!isAssignable(at->element, valueType)) {
        error("Cannot store '" + valueType->toString() + "' to atomic of type '" + 
              at->element->toString() + "'", node.location);
    }
    
    currentType_ = reg.voidType();
}

void TypeChecker::visit(AtomicSwapExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr atomicType = inferType(node.atomic.get());
    TypePtr valueType = inferType(node.value.get());
    
    if (atomicType->kind != TypeKind::ATOMIC) {
        error("Cannot swap on non-atomic type '" + atomicType->toString() + "'", node.location);
        currentType_ = reg.intType();
        return;
    }
    
    auto* at = static_cast<AtomicType*>(atomicType.get());
    if (!isAssignable(at->element, valueType)) {
        error("Cannot swap '" + valueType->toString() + "' with atomic of type '" + 
              at->element->toString() + "'", node.location);
    }
    
    // Swap returns the old value
    currentType_ = at->element;
}

void TypeChecker::visit(AtomicCasExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr atomicType = inferType(node.atomic.get());
    TypePtr expectedType = inferType(node.expected.get());
    TypePtr desiredType = inferType(node.desired.get());
    
    if (atomicType->kind != TypeKind::ATOMIC) {
        error("Cannot perform CAS on non-atomic type '" + atomicType->toString() + "'", node.location);
        currentType_ = reg.boolType();
        return;
    }
    
    auto* at = static_cast<AtomicType*>(atomicType.get());
    if (!isAssignable(at->element, expectedType)) {
        error("Expected value type '" + expectedType->toString() + "' does not match atomic type '" + 
              at->element->toString() + "'", node.location);
    }
    if (!isAssignable(at->element, desiredType)) {
        error("Desired value type '" + desiredType->toString() + "' does not match atomic type '" + 
              at->element->toString() + "'", node.location);
    }
    
    // CAS returns bool (1 if successful, 0 if not)
    currentType_ = reg.boolType();
}

void TypeChecker::visit(AtomicAddExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr atomicType = inferType(node.atomic.get());
    TypePtr valueType = inferType(node.value.get());
    
    if (atomicType->kind != TypeKind::ATOMIC) {
        error("Cannot perform atomic add on non-atomic type '" + atomicType->toString() + "'", node.location);
        currentType_ = reg.intType();
        return;
    }
    
    auto* at = static_cast<AtomicType*>(atomicType.get());
    if (!at->element->isInteger()) {
        error("Atomic add requires integer atomic type, got '" + at->element->toString() + "'", node.location);
    }
    if (!valueType->isInteger()) {
        error("Atomic add requires integer value, got '" + valueType->toString() + "'", node.location);
    }
    
    // Returns old value
    currentType_ = at->element;
}

void TypeChecker::visit(AtomicSubExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr atomicType = inferType(node.atomic.get());
    TypePtr valueType = inferType(node.value.get());
    
    if (atomicType->kind != TypeKind::ATOMIC) {
        error("Cannot perform atomic sub on non-atomic type '" + atomicType->toString() + "'", node.location);
        currentType_ = reg.intType();
        return;
    }
    
    auto* at = static_cast<AtomicType*>(atomicType.get());
    if (!at->element->isInteger()) {
        error("Atomic sub requires integer atomic type, got '" + at->element->toString() + "'", node.location);
    }
    if (!valueType->isInteger()) {
        error("Atomic sub requires integer value, got '" + valueType->toString() + "'", node.location);
    }
    
    currentType_ = at->element;
}

void TypeChecker::visit(AtomicAndExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr atomicType = inferType(node.atomic.get());
    TypePtr valueType = inferType(node.value.get());
    
    if (atomicType->kind != TypeKind::ATOMIC) {
        error("Cannot perform atomic AND on non-atomic type '" + atomicType->toString() + "'", node.location);
        currentType_ = reg.intType();
        return;
    }
    
    auto* at = static_cast<AtomicType*>(atomicType.get());
    if (!at->element->isInteger()) {
        error("Atomic AND requires integer atomic type, got '" + at->element->toString() + "'", node.location);
    }
    
    currentType_ = at->element;
}

void TypeChecker::visit(AtomicOrExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr atomicType = inferType(node.atomic.get());
    TypePtr valueType = inferType(node.value.get());
    
    if (atomicType->kind != TypeKind::ATOMIC) {
        error("Cannot perform atomic OR on non-atomic type '" + atomicType->toString() + "'", node.location);
        currentType_ = reg.intType();
        return;
    }
    
    auto* at = static_cast<AtomicType*>(atomicType.get());
    if (!at->element->isInteger()) {
        error("Atomic OR requires integer atomic type, got '" + at->element->toString() + "'", node.location);
    }
    
    currentType_ = at->element;
}

void TypeChecker::visit(AtomicXorExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr atomicType = inferType(node.atomic.get());
    TypePtr valueType = inferType(node.value.get());
    
    if (atomicType->kind != TypeKind::ATOMIC) {
        error("Cannot perform atomic XOR on non-atomic type '" + atomicType->toString() + "'", node.location);
        currentType_ = reg.intType();
        return;
    }
    
    auto* at = static_cast<AtomicType*>(atomicType.get());
    if (!at->element->isInteger()) {
        error("Atomic XOR requires integer atomic type, got '" + at->element->toString() + "'", node.location);
    }
    
    currentType_ = at->element;
}

// ============================================================================
// Smart Pointer expressions
// ============================================================================

void TypeChecker::visit(MakeBoxExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr valueType = inferType(node.value.get());
    TypePtr elemType = node.elementType.empty() ? valueType : parseTypeAnnotation(node.elementType);
    currentType_ = reg.boxType(elemType);
}

void TypeChecker::visit(MakeRcExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr valueType = inferType(node.value.get());
    TypePtr elemType = node.elementType.empty() ? valueType : parseTypeAnnotation(node.elementType);
    currentType_ = reg.rcType(elemType);
}

void TypeChecker::visit(MakeArcExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr valueType = inferType(node.value.get());
    TypePtr elemType = node.elementType.empty() ? valueType : parseTypeAnnotation(node.elementType);
    currentType_ = reg.arcType(elemType);
}

void TypeChecker::visit(MakeWeakExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr sourceType = inferType(node.source.get());
    
    // Source must be Rc or Arc
    if (sourceType->kind == TypeKind::RC) {
        auto* rc = static_cast<RcType*>(sourceType.get());
        currentType_ = reg.weakType(rc->element, false);
    } else if (sourceType->kind == TypeKind::ARC) {
        auto* arc = static_cast<ArcType*>(sourceType.get());
        currentType_ = reg.weakType(arc->element, true);
    } else {
        error("Weak reference can only be created from Rc or Arc, got '" + sourceType->toString() + "'", node.location);
        currentType_ = reg.weakType(reg.anyType(), false);
    }
}

void TypeChecker::visit(MakeCellExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr valueType = inferType(node.value.get());
    TypePtr elemType = node.elementType.empty() ? valueType : parseTypeAnnotation(node.elementType);
    currentType_ = reg.cellType(elemType);
}

void TypeChecker::visit(MakeRefCellExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr valueType = inferType(node.value.get());
    TypePtr elemType = node.elementType.empty() ? valueType : parseTypeAnnotation(node.elementType);
    currentType_ = reg.refCellType(elemType);
}

// ============================================================================
// Advanced Concurrency - Future/Promise
// ============================================================================

void TypeChecker::visit(MakeFutureExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr elemType = parseTypeAnnotation(node.elementType);
    if (elemType->kind == TypeKind::UNKNOWN) {
        elemType = reg.anyType();
    }
    currentType_ = reg.futureType(elemType);
}

void TypeChecker::visit(FutureGetExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr futureType = inferType(node.future.get());
    
    if (futureType->kind != TypeKind::FUTURE) {
        error("Cannot get value from non-future type '" + futureType->toString() + "'", node.location);
        currentType_ = reg.anyType();
        return;
    }
    
    auto* ft = static_cast<FutureType*>(futureType.get());
    currentType_ = ft->element;
}

void TypeChecker::visit(FutureSetExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr futureType = inferType(node.future.get());
    TypePtr valueType = inferType(node.value.get());
    
    if (futureType->kind != TypeKind::FUTURE) {
        error("Cannot set value on non-future type '" + futureType->toString() + "'", node.location);
        currentType_ = reg.voidType();
        return;
    }
    
    auto* ft = static_cast<FutureType*>(futureType.get());
    if (!isAssignable(ft->element, valueType)) {
        error("Cannot set '" + valueType->toString() + "' on future of type '" + 
              ft->element->toString() + "'", node.location);
    }
    
    currentType_ = reg.voidType();
}

void TypeChecker::visit(FutureIsReadyExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr futureType = inferType(node.future.get());
    
    if (futureType->kind != TypeKind::FUTURE) {
        error("Cannot check readiness of non-future type '" + futureType->toString() + "'", node.location);
    }
    
    currentType_ = reg.boolType();
}

// ============================================================================
// Advanced Concurrency - Thread Pool
// ============================================================================

void TypeChecker::visit(MakeThreadPoolExpr& node) {
    auto& reg = TypeRegistry::instance();
    if (node.numWorkers) {
        TypePtr numType = inferType(node.numWorkers.get());
        if (!numType->isInteger()) {
            error("Thread pool worker count must be an integer, got '" + numType->toString() + "'", node.location);
        }
    }
    currentType_ = reg.threadPoolType();
}

void TypeChecker::visit(ThreadPoolSubmitExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr poolType = inferType(node.pool.get());
    TypePtr taskType = inferType(node.task.get());
    
    if (poolType->kind != TypeKind::THREAD_POOL) {
        error("Cannot submit task to non-thread-pool type '" + poolType->toString() + "'", node.location);
    }
    
    // Task should be a function or callable
    if (taskType->kind != TypeKind::FUNCTION && taskType->kind != TypeKind::ANY) {
        error("Task must be a function, got '" + taskType->toString() + "'", node.location);
    }
    
    // Returns a future for the task result
    currentType_ = reg.futureType(reg.anyType());
}

void TypeChecker::visit(ThreadPoolShutdownExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr poolType = inferType(node.pool.get());
    
    if (poolType->kind != TypeKind::THREAD_POOL) {
        error("Cannot shutdown non-thread-pool type '" + poolType->toString() + "'", node.location);
    }
    
    currentType_ = reg.voidType();
}

// ============================================================================
// Advanced Concurrency - Select
// ============================================================================

void TypeChecker::visit(SelectExpr& node) {
    auto& reg = TypeRegistry::instance();
    
    for (auto& selectCase : node.cases) {
        TypePtr chanType = inferType(selectCase.channel.get());
        
        if (chanType->kind != TypeKind::CHANNEL) {
            error("Select case requires channel type, got '" + chanType->toString() + "'", node.location);
            continue;
        }
        
        auto* ch = static_cast<ChannelType*>(chanType.get());
        
        if (selectCase.isSend) {
            // Send case: check value type matches channel element type
            if (selectCase.value) {
                TypePtr valueType = inferType(selectCase.value.get());
                if (!isAssignable(ch->element, valueType)) {
                    error("Cannot send '" + valueType->toString() + "' to channel of type '" + 
                          ch->element->toString() + "'", node.location);
                }
            }
        }
        
        // Type check the case body
        if (selectCase.body) {
            selectCase.body->accept(*this);
        }
    }
    
    // Type check default case if present
    if (node.defaultCase) {
        node.defaultCase->accept(*this);
    }
    
    // Select returns void (or could return the selected case index)
    currentType_ = reg.voidType();
}

// ============================================================================
// Advanced Concurrency - Timeout
// ============================================================================

void TypeChecker::visit(TimeoutExpr& node) {
    auto& reg = TypeRegistry::instance();
    
    TypePtr opType = inferType(node.operation.get());
    TypePtr timeoutType = inferType(node.timeoutMs.get());
    
    if (!timeoutType->isInteger()) {
        error("Timeout duration must be an integer (milliseconds), got '" + timeoutType->toString() + "'", node.location);
    }
    
    // Returns the operation result or nil on timeout
    auto resultType = opType->clone();
    resultType->isNullable = true;
    currentType_ = resultType;
}

void TypeChecker::visit(ChanRecvTimeoutExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr chanType = inferType(node.channel.get());
    TypePtr timeoutType = inferType(node.timeoutMs.get());
    
    if (chanType->kind != TypeKind::CHANNEL) {
        error("Cannot receive from non-channel type '" + chanType->toString() + "'", node.location);
        currentType_ = reg.anyType();
        return;
    }
    
    if (!timeoutType->isInteger()) {
        error("Timeout duration must be an integer (milliseconds), got '" + timeoutType->toString() + "'", node.location);
    }
    
    auto* ch = static_cast<ChannelType*>(chanType.get());
    // Returns element type or nil on timeout
    auto resultType = ch->element->clone();
    resultType->isNullable = true;
    currentType_ = resultType;
}

void TypeChecker::visit(ChanSendTimeoutExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr chanType = inferType(node.channel.get());
    TypePtr valueType = inferType(node.value.get());
    TypePtr timeoutType = inferType(node.timeoutMs.get());
    
    if (chanType->kind != TypeKind::CHANNEL) {
        error("Cannot send to non-channel type '" + chanType->toString() + "'", node.location);
        currentType_ = reg.boolType();
        return;
    }
    
    auto* ch = static_cast<ChannelType*>(chanType.get());
    if (!isAssignable(ch->element, valueType)) {
        error("Cannot send '" + valueType->toString() + "' to channel of type '" + 
              ch->element->toString() + "'", node.location);
    }
    
    if (!timeoutType->isInteger()) {
        error("Timeout duration must be an integer (milliseconds), got '" + timeoutType->toString() + "'", node.location);
    }
    
    // Returns bool (true if sent, false if timeout)
    currentType_ = reg.boolType();
}

// ============================================================================
// Advanced Concurrency - Cancellation
// ============================================================================

void TypeChecker::visit(MakeCancelTokenExpr& node) {
    (void)node;
    currentType_ = TypeRegistry::instance().cancelTokenType();
}

void TypeChecker::visit(CancelExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr tokenType = inferType(node.token.get());
    
    if (tokenType->kind != TypeKind::CANCEL_TOKEN) {
        error("Cannot cancel non-cancel-token type '" + tokenType->toString() + "'", node.location);
    }
    
    currentType_ = reg.voidType();
}

void TypeChecker::visit(IsCancelledExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr tokenType = inferType(node.token.get());
    
    if (tokenType->kind != TypeKind::CANCEL_TOKEN) {
        error("Cannot check cancellation of non-cancel-token type '" + tokenType->toString() + "'", node.location);
    }
    
    currentType_ = reg.boolType();
}

// ============================================================================
// Async Runtime - Event Loop and Task Management
// ============================================================================

void TypeChecker::visit(AsyncRuntimeInitExpr& node) {
    auto& reg = TypeRegistry::instance();
    if (node.numWorkers) {
        TypePtr numType = inferType(node.numWorkers.get());
        if (!numType->isInteger()) {
            error("Async runtime worker count must be an integer, got '" + numType->toString() + "'", node.location);
        }
    }
    currentType_ = reg.voidType();
}

void TypeChecker::visit(AsyncRuntimeRunExpr& node) {
    (void)node;
    currentType_ = TypeRegistry::instance().voidType();
}

void TypeChecker::visit(AsyncRuntimeShutdownExpr& node) {
    (void)node;
    currentType_ = TypeRegistry::instance().voidType();
}

void TypeChecker::visit(AsyncSpawnExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr taskType = inferType(node.task.get());
    
    // Task should be a function or callable
    if (taskType->kind != TypeKind::FUNCTION && taskType->kind != TypeKind::ANY) {
        error("Async spawn task must be a function, got '" + taskType->toString() + "'", node.location);
    }
    
    // Returns a future for the task result
    currentType_ = reg.futureType(reg.anyType());
}

void TypeChecker::visit(AsyncSleepExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr durationType = inferType(node.durationMs.get());
    
    if (!durationType->isInteger()) {
        error("Async sleep duration must be an integer (milliseconds), got '" + durationType->toString() + "'", node.location);
    }
    
    currentType_ = reg.voidType();
}

void TypeChecker::visit(AsyncYieldExpr& node) {
    (void)node;
    currentType_ = TypeRegistry::instance().voidType();
}

// ============================================================================
// Syntax Redesign - New Expression Visitors
// ============================================================================

void TypeChecker::visit(PlaceholderExpr& node) {
    // Placeholder _ should be transformed during parsing
    // If we see it here, it's an error
    error("Placeholder '_' can only be used in lambda expressions", node.location);
    currentType_ = TypeRegistry::instance().anyType();
}

void TypeChecker::visit(InclusiveRangeExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr startType = inferType(node.start.get());
    TypePtr endType = inferType(node.end.get());
    
    if (!startType->isInteger()) {
        error("Inclusive range start must be an integer, got '" + startType->toString() + "'", node.location);
    }
    if (!endType->isInteger()) {
        error("Inclusive range end must be an integer, got '" + endType->toString() + "'", node.location);
    }
    
    if (node.step) {
        TypePtr stepType = inferType(node.step.get());
        if (!stepType->isInteger()) {
            error("Inclusive range step must be an integer, got '" + stepType->toString() + "'", node.location);
        }
    }
    
    // Range returns a list type
    currentType_ = reg.listType(reg.intType());
}

void TypeChecker::visit(SafeNavExpr& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr objType = inferType(node.object.get());
    
    // Safe navigation works on nullable types or record types
    if (objType->kind == TypeKind::RECORD) {
        auto* recordType = static_cast<RecordType*>(objType.get());
        
        // Look up the field
        TypePtr fieldType = nullptr;
        for (const auto& field : recordType->fields) {
            if (field.name == node.member) {
                fieldType = field.type;
                break;
            }
        }
        
        if (fieldType) {
            // Result is nullable version of field type
            auto resultType = fieldType->clone();
            resultType->isNullable = true;
            currentType_ = resultType;
        } else {
            error("Record type '" + recordType->name + "' has no field '" + node.member + "'", node.location);
            currentType_ = reg.anyType();
        }
    } else if (objType->isNullable || objType->kind == TypeKind::ANY) {
        // For nullable or any types, result is any (nullable)
        auto resultType = reg.anyType();
        resultType->isNullable = true;
        currentType_ = resultType;
    } else {
        error("Safe navigation '?.' requires nullable or record type, got '" + objType->toString() + "'", node.location);
        currentType_ = reg.anyType();
    }
}

void TypeChecker::visit(TypeCheckExpr& node) {
    auto& reg = TypeRegistry::instance();
    
    // Type check the value
    inferType(node.value.get());
    
    // Validate the type name
    TypePtr targetType = parseTypeAnnotation(node.typeName);
    if (targetType->kind == TypeKind::UNKNOWN) {
        warning("Unknown type '" + node.typeName + "' in type check", node.location);
    }
    
    // Result is always bool
    currentType_ = reg.boolType();
}

} // namespace tyl
