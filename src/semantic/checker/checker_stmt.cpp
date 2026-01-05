// Tyl Compiler - Type Checker Statement Visitors
// Statement type checking

#include "checker_base.h"

namespace tyl {

void TypeChecker::visit(ExprStmt& node) { inferType(node.expr.get()); }

void TypeChecker::visit(VarDecl& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr declaredType = parseTypeAnnotation(node.typeName);
    TypePtr initType = reg.unknownType();
    if (node.initializer) initType = inferType(node.initializer.get());
    TypePtr varType = (declaredType->kind != TypeKind::UNKNOWN) ? declaredType : initType;
    Symbol sym(node.name, SymbolKind::VARIABLE, varType);
    sym.isInitialized = node.initializer != nullptr;
    sym.isMutable = node.isMutable;
    sym.storage = symbols_.currentScope()->isGlobal() ? StorageClass::GLOBAL : StorageClass::LOCAL;
    sym.location = node.location;  // Store location for unused variable warnings
    sym.isUsed = false;            // Initialize as unused
    
    // Ownership tracking
    if (node.initializer) {
        sym.ownershipState = OwnershipState::OWNED;
        
        // Check if initializer is a move from another variable
        if (auto* srcId = dynamic_cast<Identifier*>(node.initializer.get())) {
            Symbol* srcSym = symbols_.lookup(srcId->name);
            if (srcSym && srcSym->kind == SymbolKind::VARIABLE) {
                // Check if source is usable
                if (srcSym->ownershipState == OwnershipState::MOVED) {
                    error("use of moved value '" + srcId->name + "'", node.initializer->location);
                } else if (srcSym->ownershipState == OwnershipState::UNINITIALIZED) {
                    error("use of uninitialized variable '" + srcId->name + "'", node.initializer->location);
                } else if (!srcSym->isCopyType && srcSym->ownershipState == OwnershipState::OWNED) {
                    // Non-copy type: this is a move
                    if (srcSym->borrowCount > 0) {
                        error("cannot move '" + srcId->name + "' while borrowed", node.initializer->location);
                    } else {
                        srcSym->ownershipState = OwnershipState::MOVED;
                        srcSym->moveLocation = node.location;
                    }
                }
                // Copy types: no move, just copy
            }
        }
    } else {
        sym.ownershipState = OwnershipState::UNINITIALIZED;
    }
    
    // Determine if this is a Copy type
    sym.isCopyType = varType->isPrimitive() || varType->isPointer();
    
    // Determine if this needs Drop (cleanup)
    sym.needsDrop = !sym.isCopyType && (varType->kind == TypeKind::LIST || 
                                         varType->kind == TypeKind::STRING ||
                                         varType->kind == TypeKind::MAP ||
                                         varType->kind == TypeKind::RECORD);
    
    symbols_.define(sym);
}

void TypeChecker::visit(AssignStmt& node) {
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
        if (targetSym) {
            targetType = targetSym->type;
            targetSym->isUsed = true;
        } else {
            error("Undefined identifier '" + id->name + "'", node.location);
            targetType = TypeRegistry::instance().errorType();
        }
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
                    // Non-copy type: this is a move
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
}

void TypeChecker::visit(Block& node) {
    symbols_.pushScope(Scope::Kind::BLOCK);
    for (auto& stmt : node.statements) stmt->accept(*this);
    checkUnusedVariables(symbols_.currentScope());  // Check for unused variables before popping
    symbols_.popScope();
}

void TypeChecker::visit(IfStmt& node) {
    inferType(node.condition.get());
    node.thenBranch->accept(*this);
    for (auto& elif : node.elifBranches) {
        inferType(elif.first.get());
        elif.second->accept(*this);
    }
    if (node.elseBranch) node.elseBranch->accept(*this);
}

void TypeChecker::visit(WhileStmt& node) {
    inferType(node.condition.get());
    symbols_.pushScope(Scope::Kind::LOOP);
    node.body->accept(*this);
    checkUnusedVariables(symbols_.currentScope());
    symbols_.popScope();
}

void TypeChecker::visit(ForStmt& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr iterType = inferType(node.iterable.get());
    TypePtr elemType = reg.anyType();
    if (iterType->kind == TypeKind::LIST) elemType = static_cast<ListType*>(iterType.get())->element;
    symbols_.pushScope(Scope::Kind::LOOP);
    Symbol varSym(node.var, SymbolKind::VARIABLE, elemType);
    varSym.location = node.location;
    varSym.ownershipState = OwnershipState::OWNED;  // Loop variable is initialized by the iterator
    varSym.isInitialized = true;
    symbols_.define(varSym);
    node.body->accept(*this);
    checkUnusedVariables(symbols_.currentScope());
    symbols_.popScope();
}

void TypeChecker::visit(MatchStmt& node) {
    inferType(node.value.get());
    for (auto& c : node.cases) {
        // Check if pattern is the wildcard '_' - don't try to infer its type
        if (auto* ident = dynamic_cast<Identifier*>(c.pattern.get())) {
            if (ident->name == "_") {
                // Wildcard pattern - skip type inference, check guard and body
                if (c.guard) inferType(c.guard.get());
                c.body->accept(*this);
                continue;
            }
            // Variable binding pattern - define the variable in scope
            if (ident->name.length() > 0 && std::islower(ident->name[0])) {
                symbols_.define(Symbol(ident->name, SymbolKind::VARIABLE, getType(node.value.get())));
            }
        }
        inferType(c.pattern.get());
        if (c.guard) inferType(c.guard.get());
        c.body->accept(*this);
    }
    if (node.defaultCase) node.defaultCase->accept(*this);
}

void TypeChecker::visit(ReturnStmt& node) {
    if (node.value) inferType(node.value.get());
}

void TypeChecker::visit(BreakStmt& node) {
    if (!symbols_.inLoop()) error("Break statement outside of loop", node.location);
}

void TypeChecker::visit(ContinueStmt& node) {
    if (!symbols_.inLoop()) error("Continue statement outside of loop", node.location);
}

void TypeChecker::visit(TryStmt& node) {
    TypePtr tryType = inferType(node.tryExpr.get());
    TypePtr elseType = inferType(node.elseExpr.get());
    currentType_ = commonType(tryType, elseType);
}

void TypeChecker::visit(UnsafeBlock& node) {
    symbols_.pushScope(Scope::Kind::UNSAFE);
    node.body->accept(*this);
    symbols_.popScope();
}

void TypeChecker::visit(DeleteStmt& node) {
    if (!symbols_.inUnsafe()) error("Delete requires unsafe block", node.location);
    inferType(node.expr.get());
}

void TypeChecker::visit(LockStmt& node) {
    // Type check the mutex expression
    TypePtr mutexType = inferType(node.mutex.get());
    
    // Verify it's a mutex type
    if (mutexType->kind != TypeKind::MUTEX) {
        error("lock statement requires a Mutex type, got '" + mutexType->toString() + "'", node.location);
    }
    
    // Type check the body
    node.body->accept(*this);
}

void TypeChecker::visit(AsmStmt& node) {
    // Inline assembly requires unsafe block
    if (!symbols_.inUnsafe()) {
        error("Inline assembly requires unsafe block", node.location);
    }
    // No type checking needed for assembly code itself
}

void TypeChecker::visit(DestructuringDecl& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr initType = inferType(node.initializer.get());
    
    // For tuple destructuring, try to get element types
    if (node.kind == DestructuringDecl::Kind::TUPLE) {
        if (initType->kind == TypeKind::LIST) {
            auto* listType = static_cast<ListType*>(initType.get());
            for (const auto& name : node.names) {
                Symbol sym(name, SymbolKind::VARIABLE, listType->element);
                sym.isMutable = node.isMutable;
                symbols_.define(sym);
            }
        } else {
            // Unknown tuple type - use any
            for (const auto& name : node.names) {
                Symbol sym(name, SymbolKind::VARIABLE, reg.anyType());
                sym.isMutable = node.isMutable;
                symbols_.define(sym);
            }
        }
    } else if (node.kind == DestructuringDecl::Kind::RECORD) {
        if (initType->kind == TypeKind::RECORD) {
            auto* recType = static_cast<RecordType*>(initType.get());
            for (const auto& name : node.names) {
                TypePtr fieldType = recType->getField(name);
                Symbol sym(name, SymbolKind::VARIABLE, fieldType ? fieldType : reg.anyType());
                sym.isMutable = node.isMutable;
                symbols_.define(sym);
            }
        } else {
            for (const auto& name : node.names) {
                Symbol sym(name, SymbolKind::VARIABLE, reg.anyType());
                sym.isMutable = node.isMutable;
                symbols_.define(sym);
            }
        }
    }
}

void TypeChecker::visit(SyntaxMacroDecl& node) {
    // Syntax macros are compile-time constructs
    Symbol macroSym(node.name, SymbolKind::MACRO, TypeRegistry::instance().anyType());
    symbols_.define(macroSym);
}

// Syntax Redesign - New Statement Visitors
void TypeChecker::visit(LoopStmt& node) {
    symbols_.pushScope(Scope::Kind::LOOP);
    node.body->accept(*this);
    symbols_.popScope();
}

void TypeChecker::visit(WithStmt& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr resourceType = inferType(node.resource.get());
    
    symbols_.pushScope(Scope::Kind::BLOCK);
    if (!node.alias.empty()) {
        Symbol aliasSym(node.alias, SymbolKind::VARIABLE, resourceType);
        symbols_.define(aliasSym);
    }
    node.body->accept(*this);
    symbols_.popScope();
}

void TypeChecker::visit(ScopeStmt& node) {
    if (node.timeout) {
        TypePtr timeoutType = inferType(node.timeout.get());
        auto& reg = TypeRegistry::instance();
        if (!isAssignable(reg.intType(), timeoutType)) {
            warning("Scope timeout should be an integer (milliseconds)", node.location);
        }
    }
    symbols_.pushScope(Scope::Kind::BLOCK);
    node.body->accept(*this);
    symbols_.popScope();
}

void TypeChecker::visit(RequireStmt& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr condType = inferType(node.condition.get());
    if (!isAssignable(reg.boolType(), condType)) {
        error("Require condition must be a boolean expression", node.location);
    }
}

void TypeChecker::visit(EnsureStmt& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr condType = inferType(node.condition.get());
    if (!isAssignable(reg.boolType(), condType)) {
        error("Ensure condition must be a boolean expression", node.location);
    }
}

void TypeChecker::visit(InvariantStmt& node) {
    auto& reg = TypeRegistry::instance();
    TypePtr condType = inferType(node.condition.get());
    if (!isAssignable(reg.boolType(), condType)) {
        error("Invariant condition must be a boolean expression", node.location);
    }
}

void TypeChecker::visit(ComptimeBlock& node) {
    // Comptime blocks are evaluated at compile time
    // For type checking, we just check the body normally
    node.body->accept(*this);
}

void TypeChecker::visit(EffectDecl& node) {
    // Register the effect in the type system
    auto& reg = TypeRegistry::instance();
    
    // Create the effect type
    auto effectType = reg.effectType(node.name);
    
    // Add type parameters
    for (const auto& typeParam : node.typeParams) {
        effectType->typeArgs.push_back(reg.typeParamType(typeParam));
    }
    
    // Add operations
    for (const auto& op : node.operations) {
        EffectOperation effectOp;
        effectOp.name = op.name;
        for (const auto& param : op.params) {
            TypePtr paramType = parseTypeAnnotation(param.second);
            effectOp.params.push_back({param.first, paramType});
        }
        effectOp.returnType = parseTypeAnnotation(op.returnType);
        effectType->operations.push_back(effectOp);
    }
    
    // Register the effect
    reg.registerEffect(node.name, effectType);
}

void TypeChecker::visit(PerformEffectExpr& node) {
    auto& reg = TypeRegistry::instance();
    
    // Look up the effect
    auto effectType = reg.lookupEffect(node.effectName);
    if (!effectType) {
        error("Unknown effect '" + node.effectName + "'", node.location);
        currentType_ = reg.errorType();
        return;
    }
    
    // Look up the operation
    const EffectOperation* op = effectType->getOperation(node.opName);
    if (!op) {
        error("Effect '" + node.effectName + "' has no operation '" + node.opName + "'", node.location);
        currentType_ = reg.errorType();
        return;
    }
    
    // Type check the arguments
    if (node.args.size() != op->params.size()) {
        error("Effect operation '" + node.opName + "' expects " + std::to_string(op->params.size()) + 
              " arguments, got " + std::to_string(node.args.size()), node.location);
    }
    
    for (size_t i = 0; i < node.args.size(); i++) {
        node.args[i]->accept(*this);
        if (i < op->params.size()) {
            TypePtr argType = currentType_;
            TypePtr paramType = op->params[i].second;
            if (!isAssignable(paramType, argType)) {
                error("Argument type mismatch in effect operation '" + node.opName + "'", node.location);
            }
        }
    }
    
    // The return type is the operation's declared return type
    currentType_ = op->returnType ? op->returnType : reg.voidType();
}

void TypeChecker::visit(HandleExpr& node) {
    auto& reg = TypeRegistry::instance();
    
    // Type check the expression being handled
    node.expr->accept(*this);
    TypePtr exprType = currentType_;
    
    // Type check each handler body
    for (auto& handler : node.handlers) {
        // Look up the effect to validate the handler
        auto effectType = reg.lookupEffect(handler.effectName);
        if (!effectType) {
            error("Unknown effect '" + handler.effectName + "' in handler", node.location);
            continue;
        }
        
        // Look up the operation
        const EffectOperation* op = effectType->getOperation(handler.opName);
        if (!op) {
            error("Effect '" + handler.effectName + "' has no operation '" + handler.opName + "'", node.location);
            continue;
        }
        
        // Create a new scope for the handler body with bound parameters
        symbols_.pushScope(Scope::Kind::BLOCK);
        
        // Bind the handler parameters
        for (size_t i = 0; i < handler.paramNames.size() && i < op->params.size(); i++) {
            Symbol paramSym(handler.paramNames[i], SymbolKind::VARIABLE, op->params[i].second);
            paramSym.isInitialized = true;
            paramSym.isParameter = true;
            paramSym.ownershipState = OwnershipState::OWNED;
            symbols_.define(paramSym);
        }
        
        // If there's a resume parameter, bind it as a function type
        if (!handler.resumeParam.empty()) {
            auto resumeFnType = std::make_shared<FunctionType>();
            resumeFnType->params.push_back({"value", op->returnType ? op->returnType : reg.anyType()});
            resumeFnType->returnType = exprType;
            Symbol resumeSym(handler.resumeParam, SymbolKind::VARIABLE, resumeFnType);
            resumeSym.isInitialized = true;
            resumeSym.isParameter = true;
            resumeSym.ownershipState = OwnershipState::OWNED;
            symbols_.define(resumeSym);
        }
        
        if (handler.body) {
            handler.body->accept(*this);
        }
        
        symbols_.popScope();
    }
    
    // The result type is the expression type (handlers may transform it)
    currentType_ = exprType;
}

void TypeChecker::visit(ResumeExpr& node) {
    auto& reg = TypeRegistry::instance();
    
    // Type check the resume value if present
    if (node.value) {
        node.value->accept(*this);
    } else {
        currentType_ = reg.voidType();
    }
    
    // Resume returns the type of the value being resumed with
    // The actual continuation type is determined by the handler context
}

void TypeChecker::visit(Program& node) {
    for (auto& stmt : node.statements) stmt->accept(*this);
}

} // namespace tyl
