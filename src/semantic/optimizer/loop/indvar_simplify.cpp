// Tyl Compiler - Induction Variable Simplification Implementation
#include "indvar_simplify.h"
#include <algorithm>
#include <iostream>
#include <cmath>

namespace tyl {

void IndVarSimplifyPass::run(Program& ast) {
    transformations_ = 0;
    stats_ = IndVarSimplifyStats{};
    
    // Process all functions
    for (auto& stmt : ast.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            processFunction(fn);
        }
    }
    
    // Sum up transformations
    transformations_ = stats_.inductionVarsSimplified +
                       stats_.tripCountsComputed +
                       stats_.derivedIVsEliminated +
                       stats_.exitConditionsSimplified +
                       stats_.loopExitsOptimized +
                       stats_.closedFormReductions;
}

void IndVarSimplifyPass::processFunction(FnDecl* fn) {
    if (!fn->body) return;
    
    inductionVars_.clear();
    
    if (auto* block = dynamic_cast<Block*>(fn->body.get())) {
        processStatements(block->statements);
    }
}

void IndVarSimplifyPass::processStatements(std::vector<StmtPtr>& stmts) {
    for (size_t i = 0; i < stmts.size(); ++i) {
        auto* stmt = stmts[i].get();
        
        // Analyze and simplify for loops
        if (auto* forLoop = dynamic_cast<ForStmt*>(stmt)) {
            analyzeForLoop(forLoop);
            
            // Compute bounds and trip count
            LoopBounds bounds = computeForLoopBounds(forLoop);
            if (bounds.tripCountKnown) {
                ++stats_.tripCountsComputed;
                
                // Try closed-form reduction FIRST (most aggressive optimization)
                ClosedFormInfo cfInfo = analyzeClosedForm(forLoop, bounds);
                if (cfInfo.canReduce) {
                    if (reduceToClosedForm(stmts, i, forLoop, cfInfo, bounds)) {
                        ++stats_.closedFormReductions;
                        // Loop was replaced, continue to next statement
                        continue;
                    }
                }
                
                // Try to canonicalize exit condition
                if (canonicalizeExitCondition(forLoop, bounds)) {
                    ++stats_.exitConditionsSimplified;
                }
                
                // Try to replace exit value uses
                if (replaceExitValue(stmts, i, forLoop->var, bounds)) {
                    ++stats_.loopExitsOptimized;
                }
            }
            
            // Find and simplify derived IVs
            auto primaryIV = findPrimaryIV(forLoop);
            if (primaryIV) {
                auto derivedIVs = findDerivedIVs(forLoop->body.get(), primaryIV->name);
                for (auto& derived : derivedIVs) {
                    if (simplifyDerivedIV(stmts, derived, *primaryIV)) {
                        ++stats_.derivedIVsEliminated;
                    }
                }
            }
            
            // Process nested loops
            if (forLoop->body) {
                if (auto* block = dynamic_cast<Block*>(forLoop->body.get())) {
                    processStatements(block->statements);
                }
            }
        }
        // Analyze while loops
        else if (auto* whileLoop = dynamic_cast<WhileStmt*>(stmt)) {
            analyzeWhileLoop(whileLoop);
            
            if (whileLoop->body) {
                if (auto* block = dynamic_cast<Block*>(whileLoop->body.get())) {
                    processStatements(block->statements);
                }
            }
        }
        // Recurse into other structures
        else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
            if (ifStmt->thenBranch) {
                if (auto* block = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
                    processStatements(block->statements);
                }
            }
            if (ifStmt->elseBranch) {
                if (auto* block = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
                    processStatements(block->statements);
                }
            }
        }
        else if (auto* block = dynamic_cast<Block*>(stmt)) {
            processStatements(block->statements);
        }
    }
}

void IndVarSimplifyPass::analyzeForLoop(ForStmt* loop) {
    if (!loop) return;
    
    // Find the primary induction variable
    auto primaryIV = findPrimaryIV(loop);
    if (primaryIV) {
        inductionVars_[primaryIV->name] = *primaryIV;
        ++stats_.inductionVarsSimplified;
    }
}

void IndVarSimplifyPass::analyzeWhileLoop(WhileStmt* loop) {
    if (!loop || !loop->body) return;
    
    // Look for induction variable pattern in while loop:
    // var i = start
    // while (i < end) {
    //     ...
    //     i = i + step
    // }
    
    // This is more complex - we need to find the IV update in the body
    // For now, we'll handle simple cases
    
    auto* body = dynamic_cast<Block*>(loop->body.get());
    if (!body) return;
    
    // Look for IV updates at the end of the loop body
    for (auto& stmt : body->statements) {
        if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt.get())) {
            if (auto* assign = dynamic_cast<AssignExpr*>(exprStmt->expr.get())) {
                std::string var;
                int64_t step;
                if (isIVUpdate(assign, var, step)) {
                    InductionVariable iv;
                    iv.name = var;
                    iv.stepValue = step;
                    iv.stepKnown = true;
                    inductionVars_[var] = iv;
                }
            }
        }
    }
}

std::optional<InductionVariable> IndVarSimplifyPass::findPrimaryIV(ForStmt* loop) {
    if (!loop) return std::nullopt;
    
    InductionVariable iv;
    iv.name = loop->var;
    iv.stepValue = 1;  // Default step
    iv.stepKnown = true;
    
    // Analyze the iterable to get start/end/step
    if (auto* range = dynamic_cast<RangeExpr*>(loop->iterable.get())) {
        // RangeExpr: start..end or start..end..step
        if (evaluateConstant(range->start.get(), iv.startValue)) {
            iv.startKnown = true;
        }
        if (range->step) {
            if (evaluateConstant(range->step.get(), iv.stepValue)) {
                iv.stepKnown = true;
            }
        }
    }
    else if (auto* call = dynamic_cast<CallExpr*>(loop->iterable.get())) {
        // range(start, end) or range(start, end, step)
        if (auto* callee = dynamic_cast<Identifier*>(call->callee.get())) {
            if (callee->name == "range") {
                if (call->args.size() >= 1) {
                    // range(end) - start is 0
                    if (call->args.size() == 1) {
                        iv.startValue = 0;
                        iv.startKnown = true;
                    }
                    // range(start, end) or range(start, end, step)
                    else if (call->args.size() >= 2) {
                        if (evaluateConstant(call->args[0].get(), iv.startValue)) {
                            iv.startKnown = true;
                        }
                    }
                    if (call->args.size() >= 3) {
                        if (evaluateConstant(call->args[2].get(), iv.stepValue)) {
                            iv.stepKnown = true;
                        }
                    }
                }
            }
        }
    }
    
    return iv;
}

std::vector<InductionVariable> IndVarSimplifyPass::findDerivedIVs(
    Statement* body, const std::string& primaryIV) {
    
    std::vector<InductionVariable> derived;
    if (!body) return derived;
    
    auto* block = dynamic_cast<Block*>(body);
    if (!block) return derived;
    
    for (auto& stmt : block->statements) {
        // Look for: var j = i * scale + offset
        if (auto* varDecl = dynamic_cast<VarDecl*>(stmt.get())) {
            if (varDecl->initializer) {
                int64_t scale, offset;
                if (isAffineInIV(varDecl->initializer.get(), primaryIV, scale, offset)) {
                    InductionVariable div;
                    div.name = varDecl->name;
                    div.isDerived = true;
                    div.baseVar = primaryIV;
                    div.scale = scale;
                    div.offset = offset;
                    derived.push_back(div);
                }
            }
        }
        
        // Look for: j = i * scale + offset (assignment)
        if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt.get())) {
            if (auto* assign = dynamic_cast<AssignExpr*>(exprStmt->expr.get())) {
                if (assign->op == TokenType::ASSIGN) {
                    if (auto* target = dynamic_cast<Identifier*>(assign->target.get())) {
                        int64_t scale, offset;
                        if (isAffineInIV(assign->value.get(), primaryIV, scale, offset)) {
                            InductionVariable div;
                            div.name = target->name;
                            div.isDerived = true;
                            div.baseVar = primaryIV;
                            div.scale = scale;
                            div.offset = offset;
                            derived.push_back(div);
                        }
                    }
                }
            }
        }
    }
    
    return derived;
}

bool IndVarSimplifyPass::isIVUpdate(Expression* expr, std::string& var, int64_t& step) {
    auto* assign = dynamic_cast<AssignExpr*>(expr);
    if (!assign) return false;
    
    auto* target = dynamic_cast<Identifier*>(assign->target.get());
    if (!target) return false;
    
    var = target->name;
    
    // i += step
    if (assign->op == TokenType::PLUS_ASSIGN) {
        if (evaluateConstant(assign->value.get(), step)) {
            return true;
        }
    }
    
    // i -= step
    if (assign->op == TokenType::MINUS_ASSIGN) {
        if (evaluateConstant(assign->value.get(), step)) {
            step = -step;
            return true;
        }
    }
    
    // i = i + step
    if (assign->op == TokenType::ASSIGN) {
        if (auto* bin = dynamic_cast<BinaryExpr*>(assign->value.get())) {
            if (bin->op == TokenType::PLUS) {
                if (auto* leftId = dynamic_cast<Identifier*>(bin->left.get())) {
                    if (leftId->name == var) {
                        if (evaluateConstant(bin->right.get(), step)) {
                            return true;
                        }
                    }
                }
                if (auto* rightId = dynamic_cast<Identifier*>(bin->right.get())) {
                    if (rightId->name == var) {
                        if (evaluateConstant(bin->left.get(), step)) {
                            return true;
                        }
                    }
                }
            }
            if (bin->op == TokenType::MINUS) {
                if (auto* leftId = dynamic_cast<Identifier*>(bin->left.get())) {
                    if (leftId->name == var) {
                        if (evaluateConstant(bin->right.get(), step)) {
                            step = -step;
                            return true;
                        }
                    }
                }
            }
        }
    }
    
    return false;
}

bool IndVarSimplifyPass::isModifiedIn(const std::string& var, Statement* stmt) {
    if (!stmt) return false;
    
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        if (auto* assign = dynamic_cast<AssignExpr*>(exprStmt->expr.get())) {
            if (auto* target = dynamic_cast<Identifier*>(assign->target.get())) {
                if (target->name == var) return true;
            }
        }
    }
    
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        if (varDecl->name == var) return true;
    }
    
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            if (isModifiedIn(var, s.get())) return true;
        }
    }
    
    if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        if (isModifiedIn(var, ifStmt->thenBranch.get())) return true;
        if (isModifiedIn(var, ifStmt->elseBranch.get())) return true;
    }
    
    return false;
}

LoopBounds IndVarSimplifyPass::computeForLoopBounds(ForStmt* loop) {
    LoopBounds bounds;
    if (!loop) return bounds;
    
    // Analyze the iterable
    if (auto* range = dynamic_cast<RangeExpr*>(loop->iterable.get())) {
        // RangeExpr uses .. which is inclusive
        bounds.isInclusive = true;
        
        if (evaluateConstant(range->start.get(), bounds.start) &&
            evaluateConstant(range->end.get(), bounds.end)) {
            bounds.boundsKnown = true;
            
            if (range->step) {
                evaluateConstant(range->step.get(), bounds.step);
            }
            
            bounds.tripCount = computeTripCount(bounds.start, bounds.end, 
                                                bounds.step, bounds.isInclusive);
            bounds.tripCountKnown = true;
        }
    }
    else if (auto* call = dynamic_cast<CallExpr*>(loop->iterable.get())) {
        if (auto* callee = dynamic_cast<Identifier*>(call->callee.get())) {
            if (callee->name == "range") {
                // range() is exclusive (like Python)
                bounds.isInclusive = false;
                
                if (call->args.size() == 1) {
                    // range(end) - start is 0
                    bounds.start = 0;
                    if (evaluateConstant(call->args[0].get(), bounds.end)) {
                        bounds.boundsKnown = true;
                    }
                }
                else if (call->args.size() >= 2) {
                    // range(start, end) or range(start, end, step)
                    if (evaluateConstant(call->args[0].get(), bounds.start) &&
                        evaluateConstant(call->args[1].get(), bounds.end)) {
                        bounds.boundsKnown = true;
                    }
                    if (call->args.size() >= 3) {
                        evaluateConstant(call->args[2].get(), bounds.step);
                    }
                }
                
                if (bounds.boundsKnown) {
                    bounds.tripCount = computeTripCount(bounds.start, bounds.end,
                                                        bounds.step, bounds.isInclusive);
                    bounds.tripCountKnown = true;
                }
            }
        }
    }
    
    return bounds;
}

LoopBounds IndVarSimplifyPass::computeWhileLoopBounds(WhileStmt* loop, 
                                                       const InductionVariable& iv) {
    LoopBounds bounds;
    if (!loop) return bounds;
    
    // Try to extract bounds from condition: i < end, i <= end, etc.
    auto* cond = dynamic_cast<BinaryExpr*>(loop->condition.get());
    if (!cond) return bounds;
    
    // Check for: iv < end or iv <= end
    if (auto* leftId = dynamic_cast<Identifier*>(cond->left.get())) {
        if (leftId->name == iv.name) {
            if (cond->op == TokenType::LT || cond->op == TokenType::LE) {
                if (evaluateConstant(cond->right.get(), bounds.end)) {
                    bounds.isInclusive = (cond->op == TokenType::LE);
                    bounds.start = iv.startValue;
                    bounds.step = iv.stepValue;
                    bounds.boundsKnown = iv.startKnown;
                    
                    if (bounds.boundsKnown) {
                        bounds.tripCount = computeTripCount(bounds.start, bounds.end,
                                                            bounds.step, bounds.isInclusive);
                        bounds.tripCountKnown = true;
                    }
                }
            }
        }
    }
    
    return bounds;
}

int64_t IndVarSimplifyPass::computeTripCount(int64_t start, int64_t end, 
                                              int64_t step, bool inclusive) {
    if (step == 0) return 0;  // Infinite loop
    
    int64_t range = end - start;
    if (inclusive) range += (step > 0 ? 1 : -1);
    
    if (step > 0) {
        if (range <= 0) return 0;
        return (range + step - 1) / step;
    } else {
        if (range >= 0) return 0;
        return (-range - step - 1) / (-step);
    }
}

bool IndVarSimplifyPass::simplifyDerivedIV(std::vector<StmtPtr>& stmts,
                                            const InductionVariable& derived,
                                            const InductionVariable& primary) {
    // For derived IVs like j = i * 4, we can:
    // 1. Replace uses of j with (i * scale + offset) if j is only used for reading
    // 2. Or strength reduce: j_next = j + (primary.step * scale)
    
    // Strategy: Convert multiplication to addition (strength reduction)
    // j = i * 4 becomes: j_init = start * 4, then j += 4 each iteration
    
    bool changed = false;
    
    // Find the definition of the derived IV in the loop body
    for (size_t i = 0; i < stmts.size(); ++i) {
        auto* stmt = stmts[i].get();
        
        // Look for: var j = i * scale + offset
        if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
            if (varDecl->name == derived.name && varDecl->initializer) {
                // Check if this is inside a loop - we need to strength reduce
                // Replace: j = i * scale + offset
                // With: j = j_prev + (step * scale)
                // But we need to initialize j before the loop
                
                // For now, we'll replace the expression with a simpler form
                // Use multiplication by scale (strength reduction to addition happens elsewhere)
                if (derived.scale != 0) {
                    // Create: j = i * scale + offset
                    auto baseId = std::make_unique<Identifier>(primary.name, varDecl->location);
                    auto scaleLit = std::make_unique<IntegerLiteral>(derived.scale, varDecl->location);
                    auto mulExpr = std::make_unique<BinaryExpr>(
                        std::move(baseId), TokenType::STAR, std::move(scaleLit), varDecl->location);
                    
                    if (derived.offset != 0) {
                        auto offsetLit = std::make_unique<IntegerLiteral>(derived.offset, varDecl->location);
                        varDecl->initializer = std::make_unique<BinaryExpr>(
                            std::move(mulExpr), TokenType::PLUS, std::move(offsetLit), varDecl->location);
                    } else {
                        varDecl->initializer = std::move(mulExpr);
                    }
                    changed = true;
                }
            }
        }
        
        // Look for: j = i * scale + offset (assignment)
        if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
            if (auto* assign = dynamic_cast<AssignExpr*>(exprStmt->expr.get())) {
                if (auto* target = dynamic_cast<Identifier*>(assign->target.get())) {
                    if (target->name == derived.name && assign->op == TokenType::ASSIGN) {
                        // Same transformation as above - use multiplication by scale
                        if (derived.scale != 0) {
                            auto baseId = std::make_unique<Identifier>(primary.name, exprStmt->location);
                            auto scaleLit = std::make_unique<IntegerLiteral>(derived.scale, exprStmt->location);
                            auto mulExpr = std::make_unique<BinaryExpr>(
                                std::move(baseId), TokenType::STAR, std::move(scaleLit), exprStmt->location);
                            
                            if (derived.offset != 0) {
                                auto offsetLit = std::make_unique<IntegerLiteral>(derived.offset, exprStmt->location);
                                assign->value = std::make_unique<BinaryExpr>(
                                    std::move(mulExpr), TokenType::PLUS, std::move(offsetLit), exprStmt->location);
                            } else {
                                assign->value = std::move(mulExpr);
                            }
                            changed = true;
                        }
                    }
                }
            }
        }
        
        // Recurse into nested structures
        if (auto* forLoop = dynamic_cast<ForStmt*>(stmt)) {
            if (forLoop->body) {
                if (auto* block = dynamic_cast<Block*>(forLoop->body.get())) {
                    if (simplifyDerivedIV(block->statements, derived, primary)) {
                        changed = true;
                    }
                }
            }
        }
        else if (auto* whileLoop = dynamic_cast<WhileStmt*>(stmt)) {
            if (whileLoop->body) {
                if (auto* block = dynamic_cast<Block*>(whileLoop->body.get())) {
                    if (simplifyDerivedIV(block->statements, derived, primary)) {
                        changed = true;
                    }
                }
            }
        }
        else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
            if (ifStmt->thenBranch) {
                if (auto* block = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
                    if (simplifyDerivedIV(block->statements, derived, primary)) {
                        changed = true;
                    }
                }
            }
            if (ifStmt->elseBranch) {
                if (auto* block = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
                    if (simplifyDerivedIV(block->statements, derived, primary)) {
                        changed = true;
                    }
                }
            }
        }
        else if (auto* block = dynamic_cast<Block*>(stmt)) {
            if (simplifyDerivedIV(block->statements, derived, primary)) {
                changed = true;
            }
        }
    }
    
    return changed;
}

bool IndVarSimplifyPass::canonicalizeExitCondition(ForStmt* loop, const LoopBounds& bounds) {
    // For loops in Tyl use range expressions, so the exit condition is implicit
    // We can potentially simplify the range if we know the trip count
    
    // For example: for i in 0..n*n where n=10 could become for i in 0..100
    
    if (!bounds.tripCountKnown) return false;
    
    // Check if we can simplify the end bound
    if (auto* range = dynamic_cast<RangeExpr*>(loop->iterable.get())) {
        // If end is a complex expression but we know its value, simplify
        if (!dynamic_cast<IntegerLiteral*>(range->end.get())) {
            int64_t endVal;
            if (evaluateConstant(range->end.get(), endVal)) {
                range->end = makeIntLiteral(endVal, range->location);
                return true;
            }
        }
    }
    
    return false;
}

bool IndVarSimplifyPass::replaceExitValue(std::vector<StmtPtr>& stmts, size_t loopIndex,
                                           const std::string& iv, const LoopBounds& bounds) {
    // After a loop, the IV has a known final value
    // We can replace uses of the IV after the loop with this constant
    
    if (!bounds.tripCountKnown || bounds.tripCount == 0) return false;
    
    // Compute final value: start + tripCount * step
    int64_t finalValue;
    if (bounds.isInclusive) {
        // For inclusive range (a..b), final value is b
        finalValue = bounds.end;
    } else {
        // For exclusive range (range(a, b)), final value is a + (tripCount - 1) * step + step
        // which simplifies to a + tripCount * step, but we need the last value used
        // Actually, after the loop, i would be the first value >= end
        finalValue = bounds.start + bounds.tripCount * bounds.step;
    }
    
    // Look for uses of IV after the loop and replace them
    bool replaced = false;
    for (size_t i = loopIndex + 1; i < stmts.size(); ++i) {
        if (replaceIVUsesInStatement(stmts[i].get(), iv, finalValue)) {
            replaced = true;
        }
    }
    
    return replaced;
}

bool IndVarSimplifyPass::replaceIVUsesInStatement(Statement* stmt, const std::string& iv, int64_t value) {
    if (!stmt) return false;
    
    bool replaced = false;
    
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        if (varDecl->initializer) {
            if (replaceIVUsesInExpr(varDecl->initializer, iv, value)) {
                replaced = true;
            }
        }
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        if (replaceIVUsesInExpr(exprStmt->expr, iv, value)) {
            replaced = true;
        }
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        if (returnStmt->value) {
            if (replaceIVUsesInExpr(returnStmt->value, iv, value)) {
                replaced = true;
            }
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        if (replaceIVUsesInExpr(ifStmt->condition, iv, value)) {
            replaced = true;
        }
        // Don't recurse into branches - they have their own control flow
    }
    else if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            if (replaceIVUsesInStatement(s.get(), iv, value)) {
                replaced = true;
            }
        }
    }
    
    return replaced;
}

bool IndVarSimplifyPass::replaceIVUsesInExpr(ExprPtr& expr, const std::string& iv, int64_t value) {
    if (!expr) return false;
    
    // Check if this is the IV identifier
    if (auto* id = dynamic_cast<Identifier*>(expr.get())) {
        if (id->name == iv) {
            expr = std::make_unique<IntegerLiteral>(value, id->location);
            return true;
        }
        return false;
    }
    
    bool replaced = false;
    
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr.get())) {
        if (replaceIVUsesInExpr(binary->left, iv, value)) replaced = true;
        if (replaceIVUsesInExpr(binary->right, iv, value)) replaced = true;
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr.get())) {
        if (replaceIVUsesInExpr(unary->operand, iv, value)) replaced = true;
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr.get())) {
        for (auto& arg : call->args) {
            if (replaceIVUsesInExpr(arg, iv, value)) replaced = true;
        }
    }
    else if (auto* index = dynamic_cast<IndexExpr*>(expr.get())) {
        if (replaceIVUsesInExpr(index->index, iv, value)) replaced = true;
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr.get())) {
        if (replaceIVUsesInExpr(ternary->condition, iv, value)) replaced = true;
        if (replaceIVUsesInExpr(ternary->thenExpr, iv, value)) replaced = true;
        if (replaceIVUsesInExpr(ternary->elseExpr, iv, value)) replaced = true;
    }
    else if (auto* assign = dynamic_cast<AssignExpr*>(expr.get())) {
        if (replaceIVUsesInExpr(assign->value, iv, value)) replaced = true;
    }
    
    return replaced;
}

bool IndVarSimplifyPass::widenInductionVariable(ForStmt* loop, InductionVariable& iv) {
    // Widening converts narrow IVs (e.g., i8) to wider types (e.g., i64)
    // to eliminate sign/zero extension operations
    
    // In Tyl, types are inferred, so this is less relevant
    // But we could add type hints for optimization
    
    return false;
}

bool IndVarSimplifyPass::isAffineInIV(Expression* expr, const std::string& iv,
                                       int64_t& scale, int64_t& offset) {
    if (!expr) return false;
    
    // Direct use of IV: scale=1, offset=0
    if (auto* id = dynamic_cast<Identifier*>(expr)) {
        if (id->name == iv) {
            scale = 1;
            offset = 0;
            return true;
        }
        return false;
    }
    
    // Constant: scale=0, offset=value
    int64_t constVal;
    if (evaluateConstant(expr, constVal)) {
        scale = 0;
        offset = constVal;
        return true;
    }
    
    // Binary expression
    if (auto* bin = dynamic_cast<BinaryExpr*>(expr)) {
        int64_t leftScale, leftOffset, rightScale, rightOffset;
        
        bool leftAffine = isAffineInIV(bin->left.get(), iv, leftScale, leftOffset);
        bool rightAffine = isAffineInIV(bin->right.get(), iv, rightScale, rightOffset);
        
        if (!leftAffine || !rightAffine) return false;
        
        switch (bin->op) {
            case TokenType::PLUS:
                scale = leftScale + rightScale;
                offset = leftOffset + rightOffset;
                return true;
                
            case TokenType::MINUS:
                scale = leftScale - rightScale;
                offset = leftOffset - rightOffset;
                return true;
                
            case TokenType::STAR:
                // (a*iv + b) * (c*iv + d) is only affine if one side is constant
                if (leftScale == 0) {
                    scale = leftOffset * rightScale;
                    offset = leftOffset * rightOffset;
                    return true;
                }
                if (rightScale == 0) {
                    scale = rightOffset * leftScale;
                    offset = rightOffset * leftOffset;
                    return true;
                }
                return false;  // Quadratic in IV
                
            default:
                return false;
        }
    }
    
    // Unary minus
    if (auto* un = dynamic_cast<UnaryExpr*>(expr)) {
        if (un->op == TokenType::MINUS) {
            if (isAffineInIV(un->operand.get(), iv, scale, offset)) {
                scale = -scale;
                offset = -offset;
                return true;
            }
        }
    }
    
    return false;
}

bool IndVarSimplifyPass::evaluateConstant(Expression* expr, int64_t& value) {
    if (!expr) return false;
    
    if (auto* lit = dynamic_cast<IntegerLiteral*>(expr)) {
        value = lit->value;
        return true;
    }
    
    // Unary minus
    if (auto* un = dynamic_cast<UnaryExpr*>(expr)) {
        if (un->op == TokenType::MINUS) {
            int64_t inner;
            if (evaluateConstant(un->operand.get(), inner)) {
                value = -inner;
                return true;
            }
        }
    }
    
    // Binary operations on constants
    if (auto* bin = dynamic_cast<BinaryExpr*>(expr)) {
        int64_t left, right;
        if (evaluateConstant(bin->left.get(), left) && 
            evaluateConstant(bin->right.get(), right)) {
            switch (bin->op) {
                case TokenType::PLUS: value = left + right; return true;
                case TokenType::MINUS: value = left - right; return true;
                case TokenType::STAR: value = left * right; return true;
                case TokenType::SLASH: 
                    if (right != 0) { value = left / right; return true; }
                    return false;
                case TokenType::PERCENT:
                    if (right != 0) { value = left % right; return true; }
                    return false;
                default: return false;
            }
        }
    }
    
    return false;
}

ExprPtr IndVarSimplifyPass::cloneExpression(Expression* expr) {
    if (!expr) return nullptr;
    
    if (auto* lit = dynamic_cast<IntegerLiteral*>(expr)) {
        return std::make_unique<IntegerLiteral>(lit->value, lit->location, lit->suffix);
    }
    if (auto* lit = dynamic_cast<FloatLiteral*>(expr)) {
        return std::make_unique<FloatLiteral>(lit->value, lit->location, lit->suffix);
    }
    if (auto* lit = dynamic_cast<BoolLiteral*>(expr)) {
        return std::make_unique<BoolLiteral>(lit->value, lit->location);
    }
    if (auto* id = dynamic_cast<Identifier*>(expr)) {
        return std::make_unique<Identifier>(id->name, id->location);
    }
    if (auto* bin = dynamic_cast<BinaryExpr*>(expr)) {
        return std::make_unique<BinaryExpr>(
            cloneExpression(bin->left.get()),
            bin->op,
            cloneExpression(bin->right.get()),
            bin->location);
    }
    if (auto* un = dynamic_cast<UnaryExpr*>(expr)) {
        return std::make_unique<UnaryExpr>(
            un->op,
            cloneExpression(un->operand.get()),
            un->location);
    }
    
    return nullptr;
}

ExprPtr IndVarSimplifyPass::makeIntLiteral(int64_t value, SourceLocation loc) {
    return std::make_unique<IntegerLiteral>(value, loc);
}

ExprPtr IndVarSimplifyPass::makeComparison(ExprPtr left, TokenType op, 
                                            ExprPtr right, SourceLocation loc) {
    return std::make_unique<BinaryExpr>(std::move(left), op, std::move(right), loc);
}

ExprPtr IndVarSimplifyPass::makeBinaryExpr(ExprPtr left, TokenType op,
                                            ExprPtr right, SourceLocation loc) {
    return std::make_unique<BinaryExpr>(std::move(left), op, std::move(right), loc);
}

// ============================================
// Closed-Form Loop Recognition
// ============================================

ClosedFormInfo IndVarSimplifyPass::analyzeClosedForm(ForStmt* loop, const LoopBounds& bounds) {
    ClosedFormInfo info;
    if (!loop || !loop->body || !bounds.tripCountKnown) return info;
    
    auto* body = dynamic_cast<Block*>(loop->body.get());
    if (!body) return info;
    
    // Only handle single-statement loop bodies for now
    if (body->statements.size() != 1) return info;
    
    auto* stmt = body->statements[0].get();
    
    // Check for expression statement (assignment)
    auto* exprStmt = dynamic_cast<ExprStmt*>(stmt);
    if (!exprStmt) return info;
    
    auto* assign = dynamic_cast<AssignExpr*>(exprStmt->expr.get());
    if (!assign) return info;
    
    auto* target = dynamic_cast<Identifier*>(assign->target.get());
    if (!target) return info;
    
    info.accumVar = target->name;
    info.ivName = loop->var;
    
    // Pattern 1: sum += i (triangular sum)
    if (assign->op == TokenType::PLUS_ASSIGN) {
        if (auto* valueId = dynamic_cast<Identifier*>(assign->value.get())) {
            if (valueId->name == loop->var) {
                // sum += i pattern - triangular sum
                info.pattern = ClosedFormPattern::TriangularSum;
                info.coefficient = 1;
                info.constant = 0;
                info.canReduce = true;
                return info;
            }
        }
        
        // Check for sum += constant
        int64_t constVal;
        if (evaluateConstant(assign->value.get(), constVal)) {
            info.pattern = ClosedFormPattern::ArithmeticSum;
            info.coefficient = 0;
            info.constant = constVal;
            info.canReduce = true;
            return info;
        }
        
        // Check for sum += a*i + b (linear accumulation)
        int64_t scale, offset;
        if (isAffineInIV(assign->value.get(), loop->var, scale, offset)) {
            if (scale != 0) {
                info.pattern = ClosedFormPattern::LinearAccum;
                info.coefficient = scale;
                info.constant = offset;
                info.canReduce = true;
                return info;
            }
        }
    }
    
    // Pattern 2: count++ or count += 1 (counting loop)
    if (assign->op == TokenType::PLUS_ASSIGN) {
        int64_t constVal;
        if (evaluateConstant(assign->value.get(), constVal) && constVal == 1) {
            // Check if the value doesn't depend on IV
            int64_t scale, offset;
            if (!isAffineInIV(assign->value.get(), loop->var, scale, offset) || scale == 0) {
                info.pattern = ClosedFormPattern::CountingLoop;
                info.coefficient = 0;
                info.constant = 1;
                info.canReduce = true;
                return info;
            }
        }
    }
    
    // Pattern 3: x = constant (dead store in loop)
    if (assign->op == TokenType::ASSIGN) {
        int64_t constVal;
        if (evaluateConstant(assign->value.get(), constVal)) {
            // Check if value doesn't depend on IV
            int64_t scale, offset;
            if (!isAffineInIV(assign->value.get(), loop->var, scale, offset) || scale == 0) {
                info.pattern = ClosedFormPattern::ConstantAssign;
                info.constant = constVal;
                info.canReduce = true;
                return info;
            }
        }
    }
    
    return info;
}

bool IndVarSimplifyPass::isSimpleAccumulation(Statement* body, const std::string& iv,
                                               std::string& accumVar, TokenType& op,
                                               int64_t& coefficient, int64_t& constant) {
    auto* block = dynamic_cast<Block*>(body);
    if (!block || block->statements.size() != 1) return false;
    
    auto* exprStmt = dynamic_cast<ExprStmt*>(block->statements[0].get());
    if (!exprStmt) return false;
    
    auto* assign = dynamic_cast<AssignExpr*>(exprStmt->expr.get());
    if (!assign) return false;
    
    auto* target = dynamic_cast<Identifier*>(assign->target.get());
    if (!target) return false;
    
    accumVar = target->name;
    op = assign->op;
    
    if (op == TokenType::PLUS_ASSIGN) {
        if (isAffineInIV(assign->value.get(), iv, coefficient, constant)) {
            return true;
        }
    }
    
    return false;
}

bool IndVarSimplifyPass::findAccumulatorInit(std::vector<StmtPtr>& stmts, size_t loopIndex,
                                              const std::string& accumVar, int64_t& initValue) {
    // Search backwards for initialization of accumVar
    for (size_t i = loopIndex; i > 0; --i) {
        auto* stmt = stmts[i - 1].get();
        
        // Check for var accumVar = value
        if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
            if (varDecl->name == accumVar && varDecl->initializer) {
                if (evaluateConstant(varDecl->initializer.get(), initValue)) {
                    return true;
                }
            }
        }
        
        // Check for accumVar = value
        if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
            if (auto* assign = dynamic_cast<AssignExpr*>(exprStmt->expr.get())) {
                if (assign->op == TokenType::ASSIGN) {
                    if (auto* target = dynamic_cast<Identifier*>(assign->target.get())) {
                        if (target->name == accumVar) {
                            if (evaluateConstant(assign->value.get(), initValue)) {
                                return true;
                            }
                        }
                    }
                }
            }
        }
    }
    
    initValue = 0;  // Default to 0 if not found
    return true;
}

bool IndVarSimplifyPass::reduceToClosedForm(std::vector<StmtPtr>& stmts, size_t loopIndex,
                                             ForStmt* loop, const ClosedFormInfo& info,
                                             const LoopBounds& bounds) {
    if (!info.canReduce) return false;
    
    // Find initial value of accumulator
    int64_t initValue = 0;
    findAccumulatorInit(stmts, loopIndex, info.accumVar, initValue);
    
    int64_t result = initValue;
    int64_t n = bounds.tripCount;
    
    SourceLocation loc = loop->location;
    
    switch (info.pattern) {
        case ClosedFormPattern::TriangularSum:
            // sum(0..n-1) = n*(n-1)/2
            // But we need to account for the actual range
            // For range(0, n): sum = 0 + 1 + 2 + ... + (n-1) = n*(n-1)/2
            // For range(a, b): sum = a + (a+1) + ... + (b-1)
            if (bounds.start == 0 && !bounds.isInclusive) {
                result = initValue + computeTriangularSum(n);
            } else {
                // General case: sum from start to end-1
                // = sum(0..end-1) - sum(0..start-1)
                int64_t sumToEnd = computeTriangularSum(bounds.end);
                int64_t sumToStart = computeTriangularSum(bounds.start);
                result = initValue + sumToEnd - sumToStart;
            }
            break;
            
        case ClosedFormPattern::ArithmeticSum:
            // sum += c for n iterations = n * c
            result = initValue + n * info.constant;
            break;
            
        case ClosedFormPattern::LinearAccum:
            // sum += a*i + b for i in range(start, end)
            // = sum of (a*i + b) for i = start to end-1
            // = a * sum(i) + n * b
            // = a * (sum(0..end-1) - sum(0..start-1)) + n * b
            result = initValue + computeLinearAccum(n, info.coefficient, info.constant, 0);
            if (bounds.start != 0) {
                // Adjust for non-zero start
                int64_t sumToEnd = computeTriangularSum(bounds.end);
                int64_t sumToStart = computeTriangularSum(bounds.start);
                result = initValue + info.coefficient * (sumToEnd - sumToStart) + n * info.constant;
            }
            break;
            
        case ClosedFormPattern::CountingLoop:
            // count++ for n iterations = n
            result = initValue + n;
            break;
            
        case ClosedFormPattern::ConstantAssign:
            // x = c in loop - final value is just c (if loop executes at least once)
            if (n > 0) {
                result = info.constant;
            }
            break;
            
        default:
            return false;
    }
    
    // Replace the loop with an assignment: accumVar = result
    auto newAssign = std::make_unique<AssignExpr>(
        std::make_unique<Identifier>(info.accumVar, loc),
        TokenType::ASSIGN,
        std::make_unique<IntegerLiteral>(result, loc),
        loc);
    
    stmts[loopIndex] = std::make_unique<ExprStmt>(std::move(newAssign), loc);
    
    return true;
}

int64_t IndVarSimplifyPass::computeTriangularSum(int64_t n) {
    // sum(0..n-1) = n*(n-1)/2
    if (n <= 0) return 0;
    return n * (n - 1) / 2;
}

int64_t IndVarSimplifyPass::computeArithmeticSum(int64_t n, int64_t constant) {
    return n * constant;
}

int64_t IndVarSimplifyPass::computeLinearAccum(int64_t n, int64_t a, int64_t b, int64_t initial) {
    // sum(a*i + b) for i in 0..n-1
    // = a * sum(0..n-1) + n * b
    // = a * n*(n-1)/2 + n * b
    if (n <= 0) return initial;
    return initial + a * computeTriangularSum(n) + n * b;
}

} // namespace tyl
