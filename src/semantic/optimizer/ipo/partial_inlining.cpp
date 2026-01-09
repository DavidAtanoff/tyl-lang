// Tyl Compiler - Partial Inlining Pass Implementation
// Based on LLVM's PartialInlining pass
#include "partial_inlining.h"
#include <iostream>

namespace tyl {

std::string PartialInliningPass::generateUniqueName(const std::string& base) {
    return base + "_cold_" + std::to_string(uniqueCounter_++);
}

void PartialInliningPass::run(Program& ast) {
    transformations_ = 0;
    stats_ = PartialInliningStats{};
    candidates_.clear();
    coldFunctions_.clear();
    uniqueCounter_ = 0;
    
    // Phase 1: Find candidates
    findCandidates(ast);
    
    if (candidates_.empty()) return;
    
    // Phase 2: Apply transformations
    applyTransformations(ast);
    
    transformations_ = stats_.functionsPartiallyInlined + stats_.callSitesOptimized;
}

void PartialInliningPass::findCandidates(Program& ast) {
    for (auto& stmt : ast.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            PartialInlineCandidate candidate;
            if (analyzeFunction(fn, candidate)) {
                candidates_[fn->name] = candidate;
                stats_.candidatesFound++;
            }
        }
        else if (auto* module = dynamic_cast<ModuleDecl*>(stmt.get())) {
            for (auto& modStmt : module->body) {
                if (auto* fn = dynamic_cast<FnDecl*>(modStmt.get())) {
                    PartialInlineCandidate candidate;
                    if (analyzeFunction(fn, candidate)) {
                        candidates_[fn->name] = candidate;
                        stats_.candidatesFound++;
                    }
                }
            }
        }
    }
}

bool PartialInliningPass::analyzeFunction(FnDecl* fn, PartialInlineCandidate& candidate) {
    if (!fn || !fn->body) return false;
    
    // Skip extern, async, and main functions
    if (fn->isExtern || fn->isAsync || fn->name == "main") return false;
    
    candidate.decl = fn;
    candidate.name = fn->name;
    candidate.totalCost = estimateCost(fn->body.get());
    
    // Check for early return pattern first (most common)
    if (hasEarlyReturnPattern(fn, candidate)) {
        // Verify cost savings are worthwhile
        if (candidate.hotPathCost <= maxInlineHotPathCost_ &&
            candidate.coldPathCost >= minColdRegionCost_) {
            float savings = (float)candidate.coldPathCost / candidate.totalCost;
            if (savings >= minCostSavingsRatio_) {
                return true;
            }
        }
    }
    
    // Check for cold branch pattern
    if (hasColdBranchPattern(fn, candidate)) {
        if (candidate.hotPathCost <= maxInlineHotPathCost_ &&
            candidate.coldPathCost >= minColdRegionCost_) {
            float savings = (float)candidate.coldPathCost / candidate.totalCost;
            if (savings >= minCostSavingsRatio_) {
                return true;
            }
        }
    }
    
    return false;
}

bool PartialInliningPass::hasEarlyReturnPattern(FnDecl* fn, PartialInlineCandidate& candidate) {
    // Pattern: if (cond) { return X; } <rest of function>
    auto* block = dynamic_cast<Block*>(fn->body.get());
    if (!block || block->statements.size() < 2) return false;
    
    // First statement should be an if with early return
    auto* ifStmt = dynamic_cast<IfStmt*>(block->statements[0].get());
    if (!ifStmt) return false;
    
    // Check if the then branch is a simple return
    auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get());
    ReturnStmt* earlyReturn = nullptr;
    
    if (thenBlock && thenBlock->statements.size() == 1) {
        earlyReturn = dynamic_cast<ReturnStmt*>(thenBlock->statements[0].get());
    } else {
        earlyReturn = dynamic_cast<ReturnStmt*>(ifStmt->thenBranch.get());
    }
    
    if (!earlyReturn) return false;
    
    // No else branch for early return pattern
    if (ifStmt->elseBranch) return false;
    
    // Calculate costs
    candidate.hasEarlyReturn = true;
    candidate.guardCondition = ifStmt->condition.get();
    candidate.earlyReturnStmt = earlyReturn;
    
    // Hot path = condition check + early return
    candidate.hotPathCost = estimateExprCost(ifStmt->condition.get()) + 1;
    
    // Cold path = rest of the function
    candidate.coldPathCost = 0;
    for (size_t i = 1; i < block->statements.size(); ++i) {
        candidate.coldPathCost += estimateCost(block->statements[i].get());
    }
    
    // Store the main body (cold path)
    if (block->statements.size() == 2) {
        candidate.mainBody = block->statements[1].get();
    }
    
    return candidate.coldPathCost > 0;
}

bool PartialInliningPass::hasColdBranchPattern(FnDecl* fn, PartialInlineCandidate& candidate) {
    // Pattern: if (likely_cond) { <hot> } else { <cold> }
    // We look for if-else where one branch is significantly larger
    
    auto* block = dynamic_cast<Block*>(fn->body.get());
    Statement* bodyStmt = block ? 
        (block->statements.size() == 1 ? block->statements[0].get() : nullptr) :
        fn->body.get();
    
    auto* ifStmt = dynamic_cast<IfStmt*>(bodyStmt);
    if (!ifStmt || !ifStmt->elseBranch) return false;
    
    size_t thenCost = estimateCost(ifStmt->thenBranch.get());
    size_t elseCost = estimateCost(ifStmt->elseBranch.get());
    
    // Determine which branch is hot and which is cold
    // Assume the smaller branch is hot (more likely to be taken)
    if (thenCost < elseCost && elseCost >= minColdRegionCost_) {
        candidate.hasColdBranch = true;
        candidate.hotPathCost = thenCost + estimateExprCost(ifStmt->condition.get());
        candidate.coldPathCost = elseCost;
        candidate.guardCondition = ifStmt->condition.get();
        candidate.mainBody = ifStmt->elseBranch.get();
        return true;
    }
    else if (elseCost < thenCost && thenCost >= minColdRegionCost_) {
        candidate.hasColdBranch = true;
        candidate.hotPathCost = elseCost + estimateExprCost(ifStmt->condition.get());
        candidate.coldPathCost = thenCost;
        candidate.guardCondition = ifStmt->condition.get();
        candidate.mainBody = ifStmt->thenBranch.get();
        return true;
    }
    
    return false;
}

size_t PartialInliningPass::estimateCost(Statement* stmt) {
    if (!stmt) return 0;
    
    size_t cost = 1;  // Base cost for any statement
    
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        cost = 0;
        for (auto& s : block->statements) {
            cost += estimateCost(s.get());
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        cost += estimateExprCost(ifStmt->condition.get());
        cost += estimateCost(ifStmt->thenBranch.get());
        for (auto& elif : ifStmt->elifBranches) {
            cost += estimateExprCost(elif.first.get());
            cost += estimateCost(elif.second.get());
        }
        cost += estimateCost(ifStmt->elseBranch.get());
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        cost += estimateExprCost(whileStmt->condition.get());
        cost += estimateCost(whileStmt->body.get()) * 3;  // Loops are expensive
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
        cost += estimateExprCost(forStmt->iterable.get());
        cost += estimateCost(forStmt->body.get()) * 3;
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        cost += estimateExprCost(exprStmt->expr.get());
    }
    else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        cost += estimateExprCost(varDecl->initializer.get());
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        cost += estimateExprCost(returnStmt->value.get());
    }
    
    return cost;
}

size_t PartialInliningPass::estimateExprCost(Expression* expr) {
    if (!expr) return 0;
    
    size_t cost = 1;
    
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        cost += estimateExprCost(binary->left.get());
        cost += estimateExprCost(binary->right.get());
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        cost += estimateExprCost(unary->operand.get());
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        cost += 5;  // Function calls are expensive
        for (auto& arg : call->args) {
            cost += estimateExprCost(arg.get());
        }
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        cost += estimateExprCost(ternary->condition.get());
        cost += estimateExprCost(ternary->thenExpr.get());
        cost += estimateExprCost(ternary->elseExpr.get());
    }
    
    return cost;
}

void PartialInliningPass::applyTransformations(Program& ast) {
    // For each candidate, create cold function and inline hot paths
    for (auto& [name, candidate] : candidates_) {
        if (candidate.hasEarlyReturn || candidate.hasColdBranch) {
            // Create cold function for the main body
            auto* coldFn = createColdFunction(name, candidate.decl, candidate.mainBody);
            if (coldFn) {
                coldFunctions_[name] = coldFn;
                // Add cold function to AST
                ast.statements.push_back(StmtPtr(coldFn));
                stats_.coldRegionsOutlined++;
            }
        }
    }
    
    // Inline hot paths at call sites
    inlineHotPaths(ast);
}

FnDecl* PartialInliningPass::createColdFunction(const std::string& baseName, 
                                                 FnDecl* original,
                                                 Statement* coldBody) {
    if (!original || !coldBody) return nullptr;
    
    std::string coldName = generateUniqueName(baseName);
    
    // Create new function declaration
    auto* coldFn = new FnDecl(coldName, original->location);
    coldFn->params = original->params;  // Same parameters
    coldFn->returnType = original->returnType;
    coldFn->isExtern = false;
    coldFn->isAsync = original->isAsync;
    coldFn->isPublic = false;  // Cold functions are internal
    coldFn->isCold = true;     // Mark as cold for further optimization hints
    
    // Clone the cold body
    std::map<std::string, Expression*> emptyArgMap;
    coldFn->body = cloneStmt(coldBody, emptyArgMap);
    
    return coldFn;
}

void PartialInliningPass::inlineHotPaths(Program& ast) {
    for (auto& stmt : ast.statements) {
        inlineHotPathsInStmt(stmt);
    }
}

void PartialInliningPass::inlineHotPathsInStmt(StmtPtr& stmt) {
    if (!stmt) return;
    
    if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
        if (fn->body) {
            inlineHotPathsInStmt(fn->body);
        }
    }
    else if (auto* block = dynamic_cast<Block*>(stmt.get())) {
        for (auto& s : block->statements) {
            inlineHotPathsInStmt(s);
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
        inlineHotPathsInExpr(ifStmt->condition);
        inlineHotPathsInStmt(ifStmt->thenBranch);
        for (auto& elif : ifStmt->elifBranches) {
            inlineHotPathsInExpr(elif.first);
            inlineHotPathsInStmt(elif.second);
        }
        inlineHotPathsInStmt(ifStmt->elseBranch);
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt.get())) {
        inlineHotPathsInExpr(whileStmt->condition);
        inlineHotPathsInStmt(whileStmt->body);
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt.get())) {
        inlineHotPathsInExpr(forStmt->iterable);
        inlineHotPathsInStmt(forStmt->body);
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt.get())) {
        // NOTE: We only inline calls that are standalone expression statements
        // (i.e., calls whose return value is not used). Calls used as values
        // in variable declarations or other expressions are NOT transformed
        // because the current transformation doesn't properly capture the result.
        // This is a conservative approach to avoid incorrect code generation.
        //
        // TODO: To support inlining calls used as values, we would need to:
        // 1. Create a temporary variable to hold the result
        // 2. Transform: let x = foo(args) 
        //    Into: let x; if (guard) { x = early_value; } else { x = foo_cold(args); }
        //
        // For now, we skip this transformation entirely to avoid bugs.
        inlineHotPathsInExpr(exprStmt->expr);
    }
    else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt.get())) {
        // Don't transform calls in variable initializers - the result is used
        inlineHotPathsInExpr(varDecl->initializer);
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt.get())) {
        inlineHotPathsInExpr(returnStmt->value);
    }
}

void PartialInliningPass::inlineHotPathsInExpr(ExprPtr& expr) {
    if (!expr) return;
    
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr.get())) {
        inlineHotPathsInExpr(binary->left);
        inlineHotPathsInExpr(binary->right);
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr.get())) {
        inlineHotPathsInExpr(unary->operand);
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr.get())) {
        for (auto& arg : call->args) {
            inlineHotPathsInExpr(arg);
        }
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr.get())) {
        inlineHotPathsInExpr(ternary->condition);
        inlineHotPathsInExpr(ternary->thenExpr);
        inlineHotPathsInExpr(ternary->elseExpr);
    }
}

StmtPtr PartialInliningPass::createInlinedHotPath(CallExpr* call, 
                                                   PartialInlineCandidate& candidate) {
    if (!call || !candidate.decl) return nullptr;
    
    // Build argument map
    std::map<std::string, Expression*> argMap;
    for (size_t i = 0; i < candidate.decl->params.size() && i < call->args.size(); ++i) {
        argMap[candidate.decl->params[i].first] = call->args[i].get();
    }
    
    // For early return pattern:
    // if (guard_condition) { return early_value; }
    // else { call cold_function(args); }
    if (candidate.hasEarlyReturn && candidate.guardCondition && candidate.earlyReturnStmt) {
        // Clone the guard condition with argument substitution
        auto condition = cloneExpr(candidate.guardCondition, argMap);
        
        // Then branch: the early return (inlined)
        auto thenBranch = cloneStmt(candidate.earlyReturnStmt, argMap);
        
        // Create IfStmt with proper constructor
        auto ifStmt = std::make_unique<IfStmt>(std::move(condition), std::move(thenBranch), call->location);
        
        // Else branch: call the cold function
        auto coldCall = std::make_unique<CallExpr>(
            std::make_unique<Identifier>(coldFunctions_[candidate.name]->name, call->location),
            call->location
        );
        for (auto& arg : call->args) {
            coldCall->args.push_back(cloneExpr(arg.get(), {}));
        }
        ifStmt->elseBranch = std::make_unique<ExprStmt>(std::move(coldCall), call->location);
        
        return ifStmt;
    }
    
    return nullptr;
}

ExprPtr PartialInliningPass::cloneExpr(Expression* expr, 
                                        const std::map<std::string, Expression*>& argMap) {
    if (!expr) return nullptr;
    
    if (auto* intLit = dynamic_cast<IntegerLiteral*>(expr)) {
        return std::make_unique<IntegerLiteral>(intLit->value, intLit->location);
    }
    else if (auto* floatLit = dynamic_cast<FloatLiteral*>(expr)) {
        return std::make_unique<FloatLiteral>(floatLit->value, floatLit->location);
    }
    else if (auto* boolLit = dynamic_cast<BoolLiteral*>(expr)) {
        return std::make_unique<BoolLiteral>(boolLit->value, boolLit->location);
    }
    else if (auto* strLit = dynamic_cast<StringLiteral*>(expr)) {
        return std::make_unique<StringLiteral>(strLit->value, strLit->location);
    }
    else if (auto* nilLit = dynamic_cast<NilLiteral*>(expr)) {
        return std::make_unique<NilLiteral>(nilLit->location);
    }
    else if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        // Check if this is a parameter - substitute with argument
        auto it = argMap.find(ident->name);
        if (it != argMap.end()) {
            return cloneExpr(it->second, {});
        }
        return std::make_unique<Identifier>(ident->name, ident->location);
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        return std::make_unique<BinaryExpr>(
            cloneExpr(binary->left.get(), argMap),
            binary->op,
            cloneExpr(binary->right.get(), argMap),
            binary->location
        );
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        return std::make_unique<UnaryExpr>(
            unary->op,
            cloneExpr(unary->operand.get(), argMap),
            unary->location
        );
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        auto newCall = std::make_unique<CallExpr>(
            cloneExpr(call->callee.get(), argMap),
            call->location
        );
        for (auto& arg : call->args) {
            newCall->args.push_back(cloneExpr(arg.get(), argMap));
        }
        return newCall;
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        return std::make_unique<TernaryExpr>(
            cloneExpr(ternary->condition.get(), argMap),
            cloneExpr(ternary->thenExpr.get(), argMap),
            cloneExpr(ternary->elseExpr.get(), argMap),
            ternary->location
        );
    }
    else if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        return std::make_unique<MemberExpr>(
            cloneExpr(member->object.get(), argMap),
            member->member,
            member->location
        );
    }
    else if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        return std::make_unique<IndexExpr>(
            cloneExpr(index->object.get(), argMap),
            cloneExpr(index->index.get(), argMap),
            index->location
        );
    }
    
    return nullptr;
}

StmtPtr PartialInliningPass::cloneStmt(Statement* stmt,
                                        const std::map<std::string, Expression*>& argMap) {
    if (!stmt) return nullptr;
    
    if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        ExprPtr value = returnStmt->value ? cloneExpr(returnStmt->value.get(), argMap) : nullptr;
        return std::make_unique<ReturnStmt>(std::move(value), returnStmt->location);
    }
    else if (auto* block = dynamic_cast<Block*>(stmt)) {
        auto newBlock = std::make_unique<Block>(block->location);
        for (auto& s : block->statements) {
            auto cloned = cloneStmt(s.get(), argMap);
            if (cloned) {
                newBlock->statements.push_back(std::move(cloned));
            }
        }
        return newBlock;
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        return std::make_unique<ExprStmt>(
            cloneExpr(exprStmt->expr.get(), argMap),
            exprStmt->location
        );
    }
    else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        ExprPtr init = varDecl->initializer ? cloneExpr(varDecl->initializer.get(), argMap) : nullptr;
        auto newDecl = std::make_unique<VarDecl>(varDecl->name, varDecl->typeName, std::move(init), varDecl->location);
        newDecl->isConst = varDecl->isConst;
        newDecl->isMutable = varDecl->isMutable;
        return newDecl;
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        auto condition = cloneExpr(ifStmt->condition.get(), argMap);
        auto thenBranch = cloneStmt(ifStmt->thenBranch.get(), argMap);
        auto newIf = std::make_unique<IfStmt>(std::move(condition), std::move(thenBranch), ifStmt->location);
        for (auto& elif : ifStmt->elifBranches) {
            newIf->elifBranches.push_back({
                cloneExpr(elif.first.get(), argMap),
                cloneStmt(elif.second.get(), argMap)
            });
        }
        if (ifStmt->elseBranch) {
            newIf->elseBranch = cloneStmt(ifStmt->elseBranch.get(), argMap);
        }
        return newIf;
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        auto condition = cloneExpr(whileStmt->condition.get(), argMap);
        auto body = cloneStmt(whileStmt->body.get(), argMap);
        auto newWhile = std::make_unique<WhileStmt>(std::move(condition), std::move(body), whileStmt->location);
        newWhile->label = whileStmt->label;
        return newWhile;
    }
    
    return nullptr;
}

} // namespace tyl
