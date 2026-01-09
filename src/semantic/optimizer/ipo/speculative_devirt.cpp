// Tyl Compiler - Speculative Devirtualization Pass Implementation
// Based on LLVM's WholeProgramDevirt pass
#include "speculative_devirt.h"
#include <iostream>

namespace tyl {

void SpeculativeDevirtPass::run(Program& ast) {
    transformations_ = 0;
    stats_ = SpeculativeDevirtStats{};
    traits_.clear();
    types_.clear();
    traitImpls_.clear();
    virtualCalls_.clear();
    
    // Phase 1: Collect type information
    collectTraits(ast);
    collectTypes(ast);
    collectImplementations(ast);
    
    // Phase 2: Find and analyze virtual calls
    findVirtualCalls(ast);
    analyzeVirtualCalls();
    
    // Phase 3: Apply devirtualization
    applyDevirtualization(ast);
    
    transformations_ = stats_.callsDevirtualized + stats_.speculativeGuardsInserted;
}

void SpeculativeDevirtPass::collectTraits(Program& ast) {
    for (auto& stmt : ast.statements) {
        if (auto* trait = dynamic_cast<TraitDecl*>(stmt.get())) {
            traits_[trait->name] = trait;
        }
        else if (auto* module = dynamic_cast<ModuleDecl*>(stmt.get())) {
            for (auto& modStmt : module->body) {
                if (auto* trait = dynamic_cast<TraitDecl*>(modStmt.get())) {
                    traits_[module->name + "::" + trait->name] = trait;
                }
            }
        }
    }
}

void SpeculativeDevirtPass::collectTypes(Program& ast) {
    for (auto& stmt : ast.statements) {
        if (auto* record = dynamic_cast<RecordDecl*>(stmt.get())) {
            TypeUsageInfo info;
            info.typeName = record->name;
            info.isSealed = true;  // Records are sealed by default in Tyl
            types_[record->name] = info;
        }
        else if (auto* enumDecl = dynamic_cast<EnumDecl*>(stmt.get())) {
            TypeUsageInfo info;
            info.typeName = enumDecl->name;
            info.isSealed = true;
            types_[enumDecl->name] = info;
        }
        else if (auto* module = dynamic_cast<ModuleDecl*>(stmt.get())) {
            for (auto& modStmt : module->body) {
                if (auto* record = dynamic_cast<RecordDecl*>(modStmt.get())) {
                    TypeUsageInfo info;
                    info.typeName = module->name + "::" + record->name;
                    info.isSealed = true;
                    types_[info.typeName] = info;
                }
            }
        }
    }
}

void SpeculativeDevirtPass::collectImplementations(Program& ast) {
    for (auto& stmt : ast.statements) {
        if (auto* implBlock = dynamic_cast<ImplBlock*>(stmt.get())) {
            // impl Trait for Type { ... }
            if (!implBlock->traitName.empty()) {
                for (auto& method : implBlock->methods) {
                    TraitMethodImpl implInfo;
                    implInfo.traitName = implBlock->traitName;
                    implInfo.methodName = method->name;
                    implInfo.implType = implBlock->typeName;
                    implInfo.implementation = method.get();
                    
                    std::string key = implBlock->traitName + "::" + method->name;
                    traitImpls_[key].push_back(implInfo);
                    
                    // Update type info
                    if (types_.count(implBlock->typeName)) {
                        types_[implBlock->typeName].implementedTraits.insert(implBlock->traitName);
                    }
                }
            }
        }
        else if (auto* module = dynamic_cast<ModuleDecl*>(stmt.get())) {
            for (auto& modStmt : module->body) {
                if (auto* implBlock = dynamic_cast<ImplBlock*>(modStmt.get())) {
                    if (!implBlock->traitName.empty()) {
                        for (auto& method : implBlock->methods) {
                            TraitMethodImpl implInfo;
                            implInfo.traitName = implBlock->traitName;
                            implInfo.methodName = method->name;
                            implInfo.implType = implBlock->typeName;
                            implInfo.implementation = method.get();
                            
                            std::string key = implBlock->traitName + "::" + method->name;
                            traitImpls_[key].push_back(implInfo);
                        }
                    }
                }
            }
        }
    }
}

void SpeculativeDevirtPass::findVirtualCalls(Program& ast) {
    for (auto& stmt : ast.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            if (fn->body) {
                findVirtualCallsInStmt(fn->body.get());
            }
        }
        else if (auto* module = dynamic_cast<ModuleDecl*>(stmt.get())) {
            for (auto& modStmt : module->body) {
                if (auto* fn = dynamic_cast<FnDecl*>(modStmt.get())) {
                    if (fn->body) {
                        findVirtualCallsInStmt(fn->body.get());
                    }
                }
            }
        }
    }
}

void SpeculativeDevirtPass::findVirtualCallsInStmt(Statement* stmt) {
    if (!stmt) return;
    
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        findVirtualCallsInExpr(exprStmt->expr.get());
    }
    else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        if (varDecl->initializer) {
            findVirtualCallsInExpr(varDecl->initializer.get());
        }
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        findVirtualCallsInExpr(assignStmt->value.get());
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        if (returnStmt->value) {
            findVirtualCallsInExpr(returnStmt->value.get());
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        findVirtualCallsInExpr(ifStmt->condition.get());
        findVirtualCallsInStmt(ifStmt->thenBranch.get());
        for (auto& elif : ifStmt->elifBranches) {
            findVirtualCallsInExpr(elif.first.get());
            findVirtualCallsInStmt(elif.second.get());
        }
        findVirtualCallsInStmt(ifStmt->elseBranch.get());
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        findVirtualCallsInExpr(whileStmt->condition.get());
        findVirtualCallsInStmt(whileStmt->body.get());
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
        findVirtualCallsInExpr(forStmt->iterable.get());
        findVirtualCallsInStmt(forStmt->body.get());
    }
    else if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            findVirtualCallsInStmt(s.get());
        }
    }
}

void SpeculativeDevirtPass::findVirtualCallsInExpr(Expression* expr) {
    if (!expr) return;
    
    // Look for method calls: obj.method(args)
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        if (auto* member = dynamic_cast<MemberExpr*>(call->callee.get())) {
            // This is a method call - potential virtual call
            VirtualCallSite site;
            site.call = call;
            site.memberAccess = member;
            site.methodName = member->member;
            
            // Try to determine the receiver type
            // For now, we look for simple identifier receivers
            if (auto* ident = dynamic_cast<Identifier*>(member->object.get())) {
                site.receiverType = ident->name;  // This would need type info
            }
            
            virtualCalls_.push_back(site);
            stats_.virtualCallsAnalyzed++;
        }
        
        // Recurse into arguments
        for (auto& arg : call->args) {
            findVirtualCallsInExpr(arg.get());
        }
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        findVirtualCallsInExpr(binary->left.get());
        findVirtualCallsInExpr(binary->right.get());
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        findVirtualCallsInExpr(unary->operand.get());
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        findVirtualCallsInExpr(ternary->condition.get());
        findVirtualCallsInExpr(ternary->thenExpr.get());
        findVirtualCallsInExpr(ternary->elseExpr.get());
    }
    else if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        findVirtualCallsInExpr(member->object.get());
    }
    else if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        findVirtualCallsInExpr(index->object.get());
        findVirtualCallsInExpr(index->index.get());
    }
}

void SpeculativeDevirtPass::analyzeVirtualCalls() {
    for (auto& site : virtualCalls_) {
        // Check if this is a monomorphic call site
        if (isMonomorphicCallSite(site)) {
            site.isMonomorphic = true;
            site.canDevirtualize = true;
            stats_.monomorphicSites++;
        }
        else {
            // Try to infer the concrete type
            std::string inferredType = inferConcreteType(site);
            if (!inferredType.empty()) {
                site.inferredType = inferredType;
                site.canDevirtualize = true;
            }
            stats_.polymorphicSites++;
        }
    }
}

bool SpeculativeDevirtPass::isMonomorphicCallSite(VirtualCallSite& site) {
    // Check all traits for this method name
    for (auto& [key, impls] : traitImpls_) {
        // Extract method name from key (trait::method)
        size_t pos = key.rfind("::");
        if (pos != std::string::npos) {
            std::string methodName = key.substr(pos + 2);
            if (methodName == site.methodName) {
                // Found implementations for this method
                if (impls.size() == 1) {
                    // Only one implementation - monomorphic!
                    site.possibleTypes.push_back(impls[0].implType);
                    return true;
                }
                else {
                    // Multiple implementations
                    for (auto& impl : impls) {
                        site.possibleTypes.push_back(impl.implType);
                    }
                }
            }
        }
    }
    
    return false;
}

std::string SpeculativeDevirtPass::inferConcreteType(VirtualCallSite& site) {
    // Try to infer the concrete type from context
    // This is a simplified version - a full implementation would use
    // type inference and data flow analysis
    
    // If we have a single possible type, use it
    if (site.possibleTypes.size() == 1) {
        return site.possibleTypes[0];
    }
    
    // If the receiver is a NewExpr, we know the exact type
    if (site.memberAccess && site.memberAccess->object) {
        if (auto* newExpr = dynamic_cast<NewExpr*>(site.memberAccess->object.get())) {
            return newExpr->typeName;
        }
    }
    
    return "";
}

void SpeculativeDevirtPass::applyDevirtualization(Program& ast) {
    for (auto& site : virtualCalls_) {
        if (site.canDevirtualize) {
            devirtualizeCall(site);
        }
    }
    
    // Replace virtual calls in the AST
    replaceVirtualCallsInStmt(reinterpret_cast<StmtPtr&>(ast.statements));
}

void SpeculativeDevirtPass::devirtualizeCall(VirtualCallSite& site) {
    if (!site.call || !site.memberAccess) return;
    
    // Find the implementation to use
    std::string targetType = site.isMonomorphic ? 
        site.possibleTypes[0] : site.inferredType;
    
    if (targetType.empty()) return;
    
    // Find the implementation for this type
    for (auto& [key, impls] : traitImpls_) {
        size_t pos = key.rfind("::");
        if (pos != std::string::npos) {
            std::string methodName = key.substr(pos + 2);
            if (methodName == site.methodName) {
                for (auto& impl : impls) {
                    if (impl.implType == targetType) {
                        // Found the implementation - create direct call
                        auto directCall = createDirectCall(site, impl);
                        if (directCall) {
                            // Replace the call expression
                            // Note: In a real implementation, we'd need to properly
                            // replace the expression in the AST
                            stats_.callsDevirtualized++;
                        }
                        return;
                    }
                }
            }
        }
    }
}

ExprPtr SpeculativeDevirtPass::createDirectCall(VirtualCallSite& site, 
                                                 const TraitMethodImpl& impl) {
    if (!site.call || !impl.implementation) return nullptr;
    
    // Create a direct call to the implementation
    // The mangled name is Type::method or Type_Trait_method
    std::string mangledName = getMangledMethodName(impl.implType, impl.traitName, 
                                                    impl.methodName);
    
    auto directCall = std::make_unique<CallExpr>(
        std::make_unique<Identifier>(mangledName, site.call->location),
        site.call->location
    );
    
    // Add the receiver as the first argument (self)
    if (site.memberAccess && site.memberAccess->object) {
        // Clone the receiver expression
        // For simplicity, we just reference it directly
        // A full implementation would properly clone the expression
    }
    
    // Copy the original arguments
    for (auto& arg : site.call->args) {
        // Clone arguments (simplified - just create identifier refs)
        if (auto* ident = dynamic_cast<Identifier*>(arg.get())) {
            directCall->args.push_back(
                std::make_unique<Identifier>(ident->name, ident->location)
            );
        }
    }
    
    return directCall;
}

StmtPtr SpeculativeDevirtPass::createSpeculativeGuard(VirtualCallSite& site,
                                                       const TraitMethodImpl& impl) {
    if (!site.call || !impl.implementation) return nullptr;
    
    // Create: if (type_of(obj) == TargetType) { direct_call } else { virtual_call }
    
    // Condition: type check (simplified - would need runtime type info)
    // For now, we just create a placeholder
    auto condition = std::make_unique<BoolLiteral>(true, site.call->location);
    
    // Then branch: direct call
    auto directCall = createDirectCall(site, impl);
    StmtPtr thenBranch;
    if (directCall) {
        thenBranch = std::make_unique<ExprStmt>(std::move(directCall), site.call->location);
    } else {
        // Fallback: empty block
        thenBranch = std::make_unique<Block>(site.call->location);
    }
    
    auto ifStmt = std::make_unique<IfStmt>(std::move(condition), std::move(thenBranch), site.call->location);
    
    // Else branch: original virtual call (fallback)
    // Would need to clone the original call
    
    stats_.speculativeGuardsInserted++;
    return ifStmt;
}

void SpeculativeDevirtPass::replaceVirtualCallsInStmt(StmtPtr& stmt) {
    // This would traverse the AST and replace devirtualized calls
    // Implementation depends on how the AST is structured
}

void SpeculativeDevirtPass::replaceVirtualCallsInExpr(ExprPtr& expr) {
    // This would replace virtual call expressions with direct calls
}

std::string SpeculativeDevirtPass::getMangledMethodName(const std::string& typeName,
                                                         const std::string& traitName,
                                                         const std::string& methodName) {
    // Generate a mangled name for the trait method implementation
    // Format: Type_Trait_method or Type::Trait::method
    return typeName + "_" + traitName + "_" + methodName;
}

} // namespace tyl
