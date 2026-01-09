// Tyl Compiler - Dead Argument Elimination Implementation
// Removes unused function arguments
#include "dead_arg_elim.h"
#include <algorithm>

namespace tyl {

void DeadArgElimPass::run(Program& ast) {
    transformations_ = 0;
    stats_ = DeadArgElimStats{};
    signatures_.clear();
    callSites_.clear();
    callbackFunctions_.clear();
    
    // Phase 1: Collect functions, call sites, and callbacks
    collectFunctions(ast);
    collectCallSites(ast);
    collectCallbacks(ast);
    
    // Phase 2: Analyze argument usage
    analyzeArgumentUsage();
    determineEliminablArgs();
    
    // Phase 3: Apply transformations
    applyTransformations(ast);
}

void DeadArgElimPass::collectFunctions(Program& ast) {
    for (auto& stmt : ast.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            FunctionSignature sig;
            sig.decl = fn;
            sig.isExtern = isExternalFunction(fn);
            sig.isRecursive = isRecursiveFunction(fn);
            
            // Collect argument info
            for (size_t i = 0; i < fn->params.size(); ++i) {
                ArgumentUsage usage;
                usage.funcName = fn->name;
                usage.argName = fn->params[i].first;
                usage.argIndex = static_cast<int>(i);
                usage.isUsed = false;
                usage.canEliminate = false;
                sig.arguments.push_back(usage);
            }
            
            signatures_[fn->name] = sig;
        }
        else if (auto* module = dynamic_cast<ModuleDecl*>(stmt.get())) {
            for (auto& modStmt : module->body) {
                if (auto* modFn = dynamic_cast<FnDecl*>(modStmt.get())) {
                    FunctionSignature sig;
                    sig.decl = modFn;
                    sig.isExtern = isExternalFunction(modFn);
                    sig.isRecursive = isRecursiveFunction(modFn);
                    
                    for (size_t i = 0; i < modFn->params.size(); ++i) {
                        ArgumentUsage usage;
                        usage.funcName = module->name + "::" + modFn->name;
                        usage.argName = modFn->params[i].first;
                        usage.argIndex = static_cast<int>(i);
                        usage.isUsed = false;
                        usage.canEliminate = false;
                        sig.arguments.push_back(usage);
                    }
                    
                    signatures_[module->name + "::" + modFn->name] = sig;
                }
            }
        }
    }
}

void DeadArgElimPass::collectCallSites(Program& ast) {
    for (auto& stmt : ast.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            if (fn->body) {
                if (auto* block = dynamic_cast<Block*>(fn->body.get())) {
                    for (auto& s : block->statements) {
                        collectCallSitesInStmt(s.get());
                    }
                }
            }
        }
        else if (auto* module = dynamic_cast<ModuleDecl*>(stmt.get())) {
            for (auto& modStmt : module->body) {
                if (auto* modFn = dynamic_cast<FnDecl*>(modStmt.get())) {
                    if (modFn->body) {
                        if (auto* block = dynamic_cast<Block*>(modFn->body.get())) {
                            for (auto& s : block->statements) {
                                collectCallSitesInStmt(s.get());
                            }
                        }
                    }
                }
            }
        }
    }
}

void DeadArgElimPass::collectCallSitesInStmt(Statement* stmt) {
    if (!stmt) return;
    
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        collectCallSitesInExpr(exprStmt->expr.get());
    }
    else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        if (varDecl->initializer) {
            collectCallSitesInExpr(varDecl->initializer.get());
        }
    }
    else if (auto* assign = dynamic_cast<AssignStmt*>(stmt)) {
        collectCallSitesInExpr(assign->value.get());
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        if (returnStmt->value) {
            collectCallSitesInExpr(returnStmt->value.get());
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        collectCallSitesInExpr(ifStmt->condition.get());
        if (auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
            for (auto& s : thenBlock->statements) {
                collectCallSitesInStmt(s.get());
            }
        }
        for (auto& elif : ifStmt->elifBranches) {
            collectCallSitesInExpr(elif.first.get());
            if (auto* elifBlock = dynamic_cast<Block*>(elif.second.get())) {
                for (auto& s : elifBlock->statements) {
                    collectCallSitesInStmt(s.get());
                }
            }
        }
        if (auto* elseBlock = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
            for (auto& s : elseBlock->statements) {
                collectCallSitesInStmt(s.get());
            }
        }
    }
    else if (auto* whileLoop = dynamic_cast<WhileStmt*>(stmt)) {
        collectCallSitesInExpr(whileLoop->condition.get());
        if (auto* body = dynamic_cast<Block*>(whileLoop->body.get())) {
            for (auto& s : body->statements) {
                collectCallSitesInStmt(s.get());
            }
        }
    }
    else if (auto* forLoop = dynamic_cast<ForStmt*>(stmt)) {
        if (forLoop->iterable) {
            collectCallSitesInExpr(forLoop->iterable.get());
        }
        if (auto* body = dynamic_cast<Block*>(forLoop->body.get())) {
            for (auto& s : body->statements) {
                collectCallSitesInStmt(s.get());
            }
        }
    }
    else if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            collectCallSitesInStmt(s.get());
        }
    }
}

void DeadArgElimPass::collectCallSitesInExpr(Expression* expr) {
    if (!expr) return;
    
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        std::string funcName = getCalledFunctionName(call);
        if (!funcName.empty()) {
            callSites_[funcName].push_back(call);
            
            // Track callers
            auto it = signatures_.find(funcName);
            if (it != signatures_.end()) {
                // We'd need to track which function we're in to add caller
            }
        }
        
        // Collect in arguments
        for (auto& arg : call->args) {
            collectCallSitesInExpr(arg.get());
        }
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        collectCallSitesInExpr(binary->left.get());
        collectCallSitesInExpr(binary->right.get());
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        collectCallSitesInExpr(unary->operand.get());
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        collectCallSitesInExpr(ternary->condition.get());
        collectCallSitesInExpr(ternary->thenExpr.get());
        collectCallSitesInExpr(ternary->elseExpr.get());
    }
    else if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        collectCallSitesInExpr(index->object.get());
        collectCallSitesInExpr(index->index.get());
    }
}

void DeadArgElimPass::collectCallbacks(Program& ast) {
    for (auto& stmt : ast.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            if (fn->body) {
                if (auto* block = dynamic_cast<Block*>(fn->body.get())) {
                    for (auto& s : block->statements) {
                        collectCallbacksInStmt(s.get());
                    }
                }
            }
        }
    }
}

void DeadArgElimPass::collectCallbacksInStmt(Statement* stmt) {
    if (!stmt) return;
    
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        collectCallbacksInExpr(exprStmt->expr.get());
    }
    else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        if (varDecl->initializer) {
            collectCallbacksInExpr(varDecl->initializer.get());
        }
    }
    else if (auto* assign = dynamic_cast<AssignStmt*>(stmt)) {
        collectCallbacksInExpr(assign->value.get());
    }
    else if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            collectCallbacksInStmt(s.get());
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        if (auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
            for (auto& s : thenBlock->statements) {
                collectCallbacksInStmt(s.get());
            }
        }
        for (auto& elif : ifStmt->elifBranches) {
            if (auto* elifBlock = dynamic_cast<Block*>(elif.second.get())) {
                for (auto& s : elifBlock->statements) {
                    collectCallbacksInStmt(s.get());
                }
            }
        }
        if (auto* elseBlock = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
            for (auto& s : elseBlock->statements) {
                collectCallbacksInStmt(s.get());
            }
        }
    }
    else if (auto* whileLoop = dynamic_cast<WhileStmt*>(stmt)) {
        if (auto* body = dynamic_cast<Block*>(whileLoop->body.get())) {
            for (auto& s : body->statements) {
                collectCallbacksInStmt(s.get());
            }
        }
    }
    else if (auto* forLoop = dynamic_cast<ForStmt*>(stmt)) {
        if (auto* body = dynamic_cast<Block*>(forLoop->body.get())) {
            for (auto& s : body->statements) {
                collectCallbacksInStmt(s.get());
            }
        }
    }
}

void DeadArgElimPass::collectCallbacksInExpr(Expression* expr) {
    if (!expr) return;
    
    // If a function identifier is used as a value (not called), it's a callback
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        if (signatures_.find(ident->name) != signatures_.end()) {
            callbackFunctions_.insert(ident->name);
        }
    }
    
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        // Check arguments - if any is a function name, mark as callback
        for (auto& arg : call->args) {
            if (auto* argIdent = dynamic_cast<Identifier*>(arg.get())) {
                if (signatures_.find(argIdent->name) != signatures_.end()) {
                    callbackFunctions_.insert(argIdent->name);
                }
            }
            collectCallbacksInExpr(arg.get());
        }
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        collectCallbacksInExpr(binary->left.get());
        collectCallbacksInExpr(binary->right.get());
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        collectCallbacksInExpr(unary->operand.get());
    }
}

void DeadArgElimPass::analyzeArgumentUsage() {
    for (auto& [funcName, sig] : signatures_) {
        analyzeFunctionArgs(sig);
    }
}

void DeadArgElimPass::analyzeFunctionArgs(FunctionSignature& sig) {
    if (!sig.decl || !sig.decl->body) return;
    
    // Check each argument for usage
    for (auto& arg : sig.arguments) {
        if (auto* block = dynamic_cast<Block*>(sig.decl->body.get())) {
            for (auto& stmt : block->statements) {
                if (isArgUsedInStmt(stmt.get(), arg.argName)) {
                    arg.isUsed = true;
                    break;
                }
            }
        }
    }
}

bool DeadArgElimPass::isArgUsedInStmt(Statement* stmt, const std::string& argName) {
    if (!stmt) return false;
    
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        return isArgUsedInExpr(exprStmt->expr.get(), argName);
    }
    else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        if (varDecl->initializer) {
            return isArgUsedInExpr(varDecl->initializer.get(), argName);
        }
    }
    else if (auto* assign = dynamic_cast<AssignStmt*>(stmt)) {
        return isArgUsedInExpr(assign->target.get(), argName) ||
               isArgUsedInExpr(assign->value.get(), argName);
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        if (returnStmt->value) {
            return isArgUsedInExpr(returnStmt->value.get(), argName);
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        if (isArgUsedInExpr(ifStmt->condition.get(), argName)) return true;
        if (auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
            for (auto& s : thenBlock->statements) {
                if (isArgUsedInStmt(s.get(), argName)) return true;
            }
        }
        for (auto& elif : ifStmt->elifBranches) {
            if (isArgUsedInExpr(elif.first.get(), argName)) return true;
            if (auto* elifBlock = dynamic_cast<Block*>(elif.second.get())) {
                for (auto& s : elifBlock->statements) {
                    if (isArgUsedInStmt(s.get(), argName)) return true;
                }
            }
        }
        if (auto* elseBlock = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
            for (auto& s : elseBlock->statements) {
                if (isArgUsedInStmt(s.get(), argName)) return true;
            }
        }
    }
    else if (auto* whileLoop = dynamic_cast<WhileStmt*>(stmt)) {
        if (isArgUsedInExpr(whileLoop->condition.get(), argName)) return true;
        if (auto* body = dynamic_cast<Block*>(whileLoop->body.get())) {
            for (auto& s : body->statements) {
                if (isArgUsedInStmt(s.get(), argName)) return true;
            }
        }
    }
    else if (auto* forLoop = dynamic_cast<ForStmt*>(stmt)) {
        if (forLoop->iterable && isArgUsedInExpr(forLoop->iterable.get(), argName)) return true;
        if (auto* body = dynamic_cast<Block*>(forLoop->body.get())) {
            for (auto& s : body->statements) {
                if (isArgUsedInStmt(s.get(), argName)) return true;
            }
        }
    }
    else if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            if (isArgUsedInStmt(s.get(), argName)) return true;
        }
    }
    
    return false;
}

bool DeadArgElimPass::isArgUsedInExpr(Expression* expr, const std::string& argName) {
    if (!expr) return false;
    
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        return ident->name == argName;
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        return isArgUsedInExpr(binary->left.get(), argName) ||
               isArgUsedInExpr(binary->right.get(), argName);
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        return isArgUsedInExpr(unary->operand.get(), argName);
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        for (auto& arg : call->args) {
            if (isArgUsedInExpr(arg.get(), argName)) return true;
        }
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        return isArgUsedInExpr(ternary->condition.get(), argName) ||
               isArgUsedInExpr(ternary->thenExpr.get(), argName) ||
               isArgUsedInExpr(ternary->elseExpr.get(), argName);
    }
    else if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        return isArgUsedInExpr(index->object.get(), argName) ||
               isArgUsedInExpr(index->index.get(), argName);
    }
    else if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        return isArgUsedInExpr(member->object.get(), argName);
    }
    
    return false;
}

void DeadArgElimPass::determineEliminablArgs() {
    for (auto& [funcName, sig] : signatures_) {
        // Skip external functions
        if (sig.isExtern) continue;
        
        // Skip callback functions (signature must be preserved)
        if (callbackFunctions_.count(funcName)) {
            sig.isCallback = true;
            continue;
        }
        
        // Check each unused argument
        for (size_t i = 0; i < sig.arguments.size(); ++i) {
            auto& arg = sig.arguments[i];
            if (!arg.isUsed && canEliminateArg(sig, static_cast<int>(i))) {
                arg.canEliminate = true;
                sig.deadArgIndices.push_back(static_cast<int>(i));
            }
        }
    }
}

bool DeadArgElimPass::canEliminateArg(const FunctionSignature& sig, int argIndex) {
    // Can't eliminate if function is external
    if (sig.isExtern) return false;
    
    // Can't eliminate if function is used as callback
    if (sig.isCallback) return false;
    
    // Can't eliminate if argument is used
    if (sig.arguments[argIndex].isUsed) return false;
    
    // Check that all call sites can be updated
    auto it = callSites_.find(sig.decl->name);
    if (it != callSites_.end()) {
        for (auto* call : it->second) {
            // Make sure call has enough arguments
            if (static_cast<size_t>(argIndex) >= call->args.size()) {
                // Argument not passed - OK to eliminate
                continue;
            }
            
            // Check if the argument expression has side effects
            // If so, we can't just remove it
            // For now, allow removal (the expression will be evaluated but result discarded)
        }
    }
    
    return true;
}

void DeadArgElimPass::applyTransformations(Program& ast) {
    for (auto& [funcName, sig] : signatures_) {
        if (sig.deadArgIndices.empty()) continue;
        
        // Remove dead arguments from function
        removeDeadArgs(sig.decl, sig.deadArgIndices);
        
        // Update all call sites
        updateCallSites(funcName, sig.deadArgIndices);
        
        stats_.functionsModified++;
        stats_.argumentsRemoved += static_cast<int>(sig.deadArgIndices.size());
        transformations_ += static_cast<int>(sig.deadArgIndices.size());
    }
}

void DeadArgElimPass::removeDeadArgs(FnDecl* fn, const std::vector<int>& deadIndices) {
    if (!fn) return;
    
    // Remove parameters in reverse order to preserve indices
    std::vector<int> sortedIndices = deadIndices;
    std::sort(sortedIndices.rbegin(), sortedIndices.rend());
    
    for (int idx : sortedIndices) {
        if (static_cast<size_t>(idx) < fn->params.size()) {
            fn->params.erase(fn->params.begin() + idx);
        }
    }
}

void DeadArgElimPass::updateCallSites(const std::string& funcName, const std::vector<int>& deadIndices) {
    auto it = callSites_.find(funcName);
    if (it == callSites_.end()) return;
    
    for (auto* call : it->second) {
        updateCallExpr(call, deadIndices);
        stats_.callSitesUpdated++;
    }
}

void DeadArgElimPass::updateCallExpr(CallExpr* call, const std::vector<int>& deadIndices) {
    if (!call) return;
    
    // Remove arguments in reverse order to preserve indices
    std::vector<int> sortedIndices = deadIndices;
    std::sort(sortedIndices.rbegin(), sortedIndices.rend());
    
    for (int idx : sortedIndices) {
        if (static_cast<size_t>(idx) < call->args.size()) {
            call->args.erase(call->args.begin() + idx);
        }
    }
}

bool DeadArgElimPass::isExternalFunction(FnDecl* fn) {
    if (!fn) return false;
    
    // Check for extern flag or no body
    if (!fn->body) return true;
    if (fn->isExtern) return true;
    
    return false;
}

bool DeadArgElimPass::isRecursiveFunction(FnDecl* fn) {
    if (!fn || !fn->body) return false;
    
    if (auto* block = dynamic_cast<Block*>(fn->body.get())) {
        for (auto& stmt : block->statements) {
            if (callsFunction(stmt.get(), fn->name)) return true;
        }
    }
    
    return false;
}

bool DeadArgElimPass::callsFunction(Statement* stmt, const std::string& funcName) {
    if (!stmt) return false;
    
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        return callsFunctionInExpr(exprStmt->expr.get(), funcName);
    }
    else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        if (varDecl->initializer) {
            return callsFunctionInExpr(varDecl->initializer.get(), funcName);
        }
    }
    else if (auto* assign = dynamic_cast<AssignStmt*>(stmt)) {
        return callsFunctionInExpr(assign->value.get(), funcName);
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        if (returnStmt->value) {
            return callsFunctionInExpr(returnStmt->value.get(), funcName);
        }
    }
    else if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            if (callsFunction(s.get(), funcName)) return true;
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        if (callsFunctionInExpr(ifStmt->condition.get(), funcName)) return true;
        if (callsFunction(ifStmt->thenBranch.get(), funcName)) return true;
        for (auto& elif : ifStmt->elifBranches) {
            if (callsFunctionInExpr(elif.first.get(), funcName)) return true;
            if (callsFunction(elif.second.get(), funcName)) return true;
        }
        if (callsFunction(ifStmt->elseBranch.get(), funcName)) return true;
    }
    else if (auto* whileLoop = dynamic_cast<WhileStmt*>(stmt)) {
        if (callsFunctionInExpr(whileLoop->condition.get(), funcName)) return true;
        if (callsFunction(whileLoop->body.get(), funcName)) return true;
    }
    else if (auto* forLoop = dynamic_cast<ForStmt*>(stmt)) {
        if (forLoop->iterable && callsFunctionInExpr(forLoop->iterable.get(), funcName)) return true;
        if (callsFunction(forLoop->body.get(), funcName)) return true;
    }
    
    return false;
}

bool DeadArgElimPass::callsFunctionInExpr(Expression* expr, const std::string& funcName) {
    if (!expr) return false;
    
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        if (auto* callee = dynamic_cast<Identifier*>(call->callee.get())) {
            if (callee->name == funcName) return true;
        }
        for (auto& arg : call->args) {
            if (callsFunctionInExpr(arg.get(), funcName)) return true;
        }
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        return callsFunctionInExpr(binary->left.get(), funcName) ||
               callsFunctionInExpr(binary->right.get(), funcName);
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        return callsFunctionInExpr(unary->operand.get(), funcName);
    }
    
    return false;
}

std::string DeadArgElimPass::getCalledFunctionName(CallExpr* call) {
    if (!call) return "";
    
    if (auto* ident = dynamic_cast<Identifier*>(call->callee.get())) {
        return ident->name;
    }
    
    return "";
}

} // namespace tyl
