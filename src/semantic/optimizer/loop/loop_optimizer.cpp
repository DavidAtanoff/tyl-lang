// Tyl Compiler - Loop Optimizer Implementation
// Loop unrolling, LICM, and strength reduction
#include "loop_optimizer.h"
#include <algorithm>
#include <cmath>

namespace tyl {

// Helper function to check if a statement contains break or continue
static bool containsBreakOrContinue(Statement* stmt) {
    if (!stmt) return false;
    
    if (dynamic_cast<BreakStmt*>(stmt)) return true;
    if (dynamic_cast<ContinueStmt*>(stmt)) return true;
    
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            if (containsBreakOrContinue(s.get())) return true;
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        if (containsBreakOrContinue(ifStmt->thenBranch.get())) return true;
        for (auto& elif : ifStmt->elifBranches) {
            if (containsBreakOrContinue(elif.second.get())) return true;
        }
        if (containsBreakOrContinue(ifStmt->elseBranch.get())) return true;
    }
    // Note: We don't recurse into nested loops because break/continue in nested loops
    // don't affect the outer loop
    
    return false;
}

// ============================================
// Loop Unrolling Pass
// ============================================

void LoopUnrollingPass::run(Program& ast) {
    transformations_ = 0;
    processStatements(ast.statements);
}

void LoopUnrollingPass::processStatements(std::vector<StmtPtr>& stmts) {
    for (size_t i = 0; i < stmts.size(); ++i) {
        auto* stmt = stmts[i].get();
        
        // Check for for loops to unroll
        if (auto* forLoop = dynamic_cast<ForStmt*>(stmt)) {
            LoopInfo info;
            bool wasUnrolled = false;
            // Don't unroll labeled loops - they may have break/continue targeting them
            if (forLoop->label.empty() && analyzeLoop(forLoop, info)) {
                if (info.boundsKnown && 
                    info.tripCount >= minTripCount_ && 
                    info.tripCount <= maxTripCount_) {
                    auto unrolled = unrollLoop(forLoop, info);
                    if (unrolled) {
                        stmts[i] = std::move(unrolled);
                        transformations_++;
                        wasUnrolled = true;
                        // Process the unrolled block for any nested loops
                        if (auto* unrolledBlock = dynamic_cast<Block*>(stmts[i].get())) {
                            processStatements(unrolledBlock->statements);
                        }
                    }
                }
            }
            // Only process original loop body if it wasn't unrolled
            if (!wasUnrolled) {
                if (auto* block = dynamic_cast<Block*>(forLoop->body.get())) {
                    processStatements(block->statements);
                }
            }
        }
        else if (auto* block = dynamic_cast<Block*>(stmt)) {
            processStatements(block->statements);
        }
        else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
            if (auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
                processStatements(thenBlock->statements);
            }
            for (auto& elif : ifStmt->elifBranches) {
                if (auto* elifBlock = dynamic_cast<Block*>(elif.second.get())) {
                    processStatements(elifBlock->statements);
                }
            }
            if (auto* elseBlock = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
                processStatements(elseBlock->statements);
            }
        }
        else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
            if (auto* block = dynamic_cast<Block*>(whileStmt->body.get())) {
                processStatements(block->statements);
            }
        }
        else if (auto* fnDecl = dynamic_cast<FnDecl*>(stmt)) {
            if (auto* block = dynamic_cast<Block*>(fnDecl->body.get())) {
                processStatements(block->statements);
            }
        }
    }
}

bool LoopUnrollingPass::analyzeLoop(ForStmt* loop, LoopInfo& info) {
    info.loop = loop;
    info.inductionVar = loop->var;
    
    // Don't unroll loops that contain break or continue
    if (containsBreakOrContinue(loop->body.get())) {
        return false;
    }
    
    // Try to get bounds from range expression
    // RangeExpr (using ..) is INCLUSIVE - includes both start and end
    if (auto* range = dynamic_cast<RangeExpr*>(loop->iterable.get())) {
        auto* startLit = dynamic_cast<IntegerLiteral*>(range->start.get());
        auto* endLit = dynamic_cast<IntegerLiteral*>(range->end.get());
        
        if (startLit && endLit) {
            info.startValue = startLit->value;
            info.endValue = endLit->value;
            info.stepValue = 1;  // Default step
            
            // Check for step value (by keyword)
            if (range->step) {
                if (auto* stepLit = dynamic_cast<IntegerLiteral*>(range->step.get())) {
                    info.stepValue = stepLit->value;
                }
            }
            
            info.boundsKnown = true;
            if (info.stepValue != 0) {
                info.tripCount = (info.endValue - info.startValue) / info.stepValue + 1;  // +1 for inclusive
            } else {
                info.tripCount = 0;
            }
            info.isInclusive = true;  // Mark as inclusive range
            return true;
        }
    }
    
    // Try to get bounds from range() call
    // range() function is EXCLUSIVE (like Python) - does NOT include end value
    if (auto* call = dynamic_cast<CallExpr*>(loop->iterable.get())) {
        if (auto* id = dynamic_cast<Identifier*>(call->callee.get())) {
            if (id->name == "range") {
                if (call->args.size() == 1) {
                    if (auto* endLit = dynamic_cast<IntegerLiteral*>(call->args[0].get())) {
                        info.startValue = 0;
                        info.endValue = endLit->value;
                        info.stepValue = 1;
                        info.boundsKnown = true;
                        info.tripCount = info.endValue;
                        info.isInclusive = false;  // range() is exclusive
                        return true;
                    }
                }
                else if (call->args.size() >= 2) {
                    auto* startLit = dynamic_cast<IntegerLiteral*>(call->args[0].get());
                    auto* endLit = dynamic_cast<IntegerLiteral*>(call->args[1].get());
                    if (startLit && endLit) {
                        info.startValue = startLit->value;
                        info.endValue = endLit->value;
                        info.stepValue = 1;
                        if (call->args.size() >= 3) {
                            if (auto* stepLit = dynamic_cast<IntegerLiteral*>(call->args[2].get())) {
                                info.stepValue = stepLit->value;
                            }
                        }
                        info.boundsKnown = true;
                        info.tripCount = (info.endValue - info.startValue) / info.stepValue;
                        info.isInclusive = false;  // range() is exclusive
                        return true;
                    }
                }
            }
        }
    }
    
    return false;
}

StmtPtr LoopUnrollingPass::unrollLoop(ForStmt* loop, const LoopInfo& info) {
    SourceLocation loc = loop->location;  // Use the original loop's location
    
    // For small trip counts, fully unroll
    if (info.tripCount <= unrollFactor_) {
        auto block = std::make_unique<Block>(loc);
        
        // Use <= for inclusive ranges (RangeExpr), < for exclusive (range() function)
        for (int64_t i = info.startValue; 
             info.isInclusive ? (i <= info.endValue) : (i < info.endValue); 
             i += info.stepValue) {
            // Clone the loop body with the induction variable replaced
            auto cloned = cloneStatement(loop->body.get(), info.inductionVar, i);
            if (cloned) {
                if (auto* clonedBlock = dynamic_cast<Block*>(cloned.get())) {
                    for (auto& s : clonedBlock->statements) {
                        block->statements.push_back(std::move(s));
                    }
                } else {
                    block->statements.push_back(std::move(cloned));
                }
            }
        }
        
        return block;
    }
    
    // For larger loops, use partial unrolling with remainder loop
    // This unrolls by unrollFactor_ and handles the remainder iterations
    auto block = std::make_unique<Block>(loc);
    
    // Calculate how many full unrolled iterations we can do
    int64_t unrolledIterations = (info.tripCount / unrollFactor_) * unrollFactor_;
    int64_t remainderIterations = info.tripCount % unrollFactor_;
    
    // Calculate the end value for the unrolled portion
    int64_t unrolledEndValue = info.startValue + (unrolledIterations - 1) * info.stepValue;
    if (!info.isInclusive) {
        unrolledEndValue = info.startValue + unrolledIterations * info.stepValue;
    }
    
    // Generate the main unrolled loop (if there are enough iterations)
    if (unrolledIterations >= unrollFactor_) {
        // Create a new for loop with step = stepValue * unrollFactor_
        int64_t newStep = info.stepValue * unrollFactor_;
        
        // Create the range for the unrolled loop
        ExprPtr newIterable;
        if (info.isInclusive) {
            // For inclusive ranges, use RangeExpr with step
            auto rangeExpr = std::make_unique<RangeExpr>(
                std::make_unique<IntegerLiteral>(info.startValue, loc),
                std::make_unique<IntegerLiteral>(unrolledEndValue - (unrollFactor_ - 1) * info.stepValue, loc),
                std::make_unique<IntegerLiteral>(newStep, loc),
                loc);
            newIterable = std::move(rangeExpr);
        } else {
            // For exclusive ranges, use range() call
            auto rangeCall = std::make_unique<CallExpr>(
                std::make_unique<Identifier>("range", loc), loc);
            rangeCall->args.push_back(std::make_unique<IntegerLiteral>(info.startValue, loc));
            rangeCall->args.push_back(std::make_unique<IntegerLiteral>(
                info.startValue + (unrolledIterations / unrollFactor_) * newStep, loc));
            rangeCall->args.push_back(std::make_unique<IntegerLiteral>(newStep, loc));
            newIterable = std::move(rangeCall);
        }
        
        // Create the unrolled loop body
        auto unrolledBody = std::make_unique<Block>(loc);
        
        // Add unrollFactor_ copies of the original body
        for (int j = 0; j < unrollFactor_; ++j) {
            // Clone body with i + j*step offset
            // We need to create expressions like: inductionVar + j*stepValue
            auto cloned = cloneStatementWithOffset(loop->body.get(), info.inductionVar, 
                                                    j * info.stepValue);
            if (cloned) {
                if (auto* clonedBlock = dynamic_cast<Block*>(cloned.get())) {
                    for (auto& s : clonedBlock->statements) {
                        unrolledBody->statements.push_back(std::move(s));
                    }
                } else {
                    unrolledBody->statements.push_back(std::move(cloned));
                }
            }
        }
        
        // Create the unrolled for loop
        auto unrolledLoop = std::make_unique<ForStmt>(
            info.inductionVar, std::move(newIterable), std::move(unrolledBody), loc);
        block->statements.push_back(std::move(unrolledLoop));
    }
    
    // Generate remainder iterations (fully unrolled)
    if (remainderIterations > 0) {
        int64_t remainderStart = info.startValue + unrolledIterations * info.stepValue;
        
        for (int64_t i = remainderStart; 
             info.isInclusive ? (i <= info.endValue) : (i < info.endValue); 
             i += info.stepValue) {
            auto cloned = cloneStatement(loop->body.get(), info.inductionVar, i);
            if (cloned) {
                if (auto* clonedBlock = dynamic_cast<Block*>(cloned.get())) {
                    for (auto& s : clonedBlock->statements) {
                        block->statements.push_back(std::move(s));
                    }
                } else {
                    block->statements.push_back(std::move(cloned));
                }
            }
        }
    }
    
    return block;
}

// Helper function to clone a statement with an offset expression instead of constant
StmtPtr LoopUnrollingPass::cloneStatementWithOffset(Statement* stmt, const std::string& inductionVar, int64_t offset) {
    if (!stmt) return nullptr;
    SourceLocation loc = stmt->location;
    
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        auto newBlock = std::make_unique<Block>(loc);
        for (auto& s : block->statements) {
            auto cloned = cloneStatementWithOffset(s.get(), inductionVar, offset);
            if (cloned) {
                newBlock->statements.push_back(std::move(cloned));
            }
        }
        return newBlock;
    }
    
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        return std::make_unique<ExprStmt>(
            cloneExpressionWithOffset(exprStmt->expr.get(), inductionVar, offset), loc);
    }
    
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        auto newDecl = std::make_unique<VarDecl>(
            varDecl->name, varDecl->typeName,
            varDecl->initializer ? cloneExpressionWithOffset(varDecl->initializer.get(), inductionVar, offset) : nullptr,
            loc);
        newDecl->isMutable = varDecl->isMutable;
        newDecl->isConst = varDecl->isConst;
        return newDecl;
    }
    
    if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        return std::make_unique<AssignStmt>(
            cloneExpressionWithOffset(assignStmt->target.get(), inductionVar, offset),
            assignStmt->op,
            cloneExpressionWithOffset(assignStmt->value.get(), inductionVar, offset),
            loc);
    }
    
    if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        auto newIf = std::make_unique<IfStmt>(
            cloneExpressionWithOffset(ifStmt->condition.get(), inductionVar, offset),
            cloneStatementWithOffset(ifStmt->thenBranch.get(), inductionVar, offset),
            loc);
        for (auto& elif : ifStmt->elifBranches) {
            auto cond = cloneExpressionWithOffset(elif.first.get(), inductionVar, offset);
            auto body = cloneStatementWithOffset(elif.second.get(), inductionVar, offset);
            newIf->elifBranches.push_back({std::move(cond), std::move(body)});
        }
        if (ifStmt->elseBranch) {
            newIf->elseBranch = cloneStatementWithOffset(ifStmt->elseBranch.get(), inductionVar, offset);
        }
        return newIf;
    }
    
    if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        return std::make_unique<ReturnStmt>(
            returnStmt->value ? cloneExpressionWithOffset(returnStmt->value.get(), inductionVar, offset) : nullptr,
            loc);
    }
    
    // For other statements, fall back to regular clone
    return cloneStatement(stmt, inductionVar, 0);
}

ExprPtr LoopUnrollingPass::cloneExpressionWithOffset(Expression* expr, const std::string& inductionVar, int64_t offset) {
    if (!expr) return nullptr;
    SourceLocation loc = expr->location;
    
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        if (ident->name == inductionVar) {
            if (offset == 0) {
                // Just return the identifier
                return std::make_unique<Identifier>(ident->name, loc);
            }
            // Return inductionVar + offset
            return std::make_unique<BinaryExpr>(
                std::make_unique<Identifier>(ident->name, loc),
                TokenType::PLUS,
                std::make_unique<IntegerLiteral>(offset, loc),
                loc);
        }
        return std::make_unique<Identifier>(ident->name, loc);
    }
    
    if (auto* intLit = dynamic_cast<IntegerLiteral*>(expr)) {
        return std::make_unique<IntegerLiteral>(intLit->value, loc);
    }
    
    if (auto* floatLit = dynamic_cast<FloatLiteral*>(expr)) {
        return std::make_unique<FloatLiteral>(floatLit->value, loc);
    }
    
    if (auto* boolLit = dynamic_cast<BoolLiteral*>(expr)) {
        return std::make_unique<BoolLiteral>(boolLit->value, loc);
    }
    
    if (auto* strLit = dynamic_cast<StringLiteral*>(expr)) {
        return std::make_unique<StringLiteral>(strLit->value, loc);
    }
    
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        return std::make_unique<BinaryExpr>(
            cloneExpressionWithOffset(binary->left.get(), inductionVar, offset),
            binary->op,
            cloneExpressionWithOffset(binary->right.get(), inductionVar, offset),
            loc);
    }
    
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        return std::make_unique<UnaryExpr>(
            unary->op,
            cloneExpressionWithOffset(unary->operand.get(), inductionVar, offset),
            loc);
    }
    
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        auto newCall = std::make_unique<CallExpr>(
            cloneExpressionWithOffset(call->callee.get(), inductionVar, offset), loc);
        for (auto& arg : call->args) {
            newCall->args.push_back(cloneExpressionWithOffset(arg.get(), inductionVar, offset));
        }
        return newCall;
    }
    
    if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        return std::make_unique<IndexExpr>(
            cloneExpressionWithOffset(index->object.get(), inductionVar, offset),
            cloneExpressionWithOffset(index->index.get(), inductionVar, offset),
            loc);
    }
    
    if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        return std::make_unique<TernaryExpr>(
            cloneExpressionWithOffset(ternary->condition.get(), inductionVar, offset),
            cloneExpressionWithOffset(ternary->thenExpr.get(), inductionVar, offset),
            cloneExpressionWithOffset(ternary->elseExpr.get(), inductionVar, offset),
            loc);
    }
    
    if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        return std::make_unique<MemberExpr>(
            cloneExpressionWithOffset(member->object.get(), inductionVar, offset),
            member->member,
            loc);
    }
    
    if (auto* assignExpr = dynamic_cast<AssignExpr*>(expr)) {
        return std::make_unique<AssignExpr>(
            cloneExpressionWithOffset(assignExpr->target.get(), inductionVar, offset),
            assignExpr->op,
            cloneExpressionWithOffset(assignExpr->value.get(), inductionVar, offset),
            loc);
    }
    
    // Fall back to regular clone for other expressions
    return cloneExpression(expr, inductionVar, 0);
}

StmtPtr LoopUnrollingPass::cloneStatement(Statement* stmt, const std::string& inductionVar, int64_t offset) {
    if (!stmt) return nullptr;
    SourceLocation loc = stmt->location;
    
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        auto newBlock = std::make_unique<Block>(loc);
        for (auto& s : block->statements) {
            auto cloned = cloneStatement(s.get(), inductionVar, offset);
            if (cloned) {
                newBlock->statements.push_back(std::move(cloned));
            }
        }
        return newBlock;
    }
    
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        return std::make_unique<ExprStmt>(
            cloneExpression(exprStmt->expr.get(), inductionVar, offset), loc);
    }
    
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        auto newDecl = std::make_unique<VarDecl>(
            varDecl->name, varDecl->typeName,
            varDecl->initializer ? cloneExpression(varDecl->initializer.get(), inductionVar, offset) : nullptr,
            loc);
        newDecl->isMutable = varDecl->isMutable;
        newDecl->isConst = varDecl->isConst;
        return newDecl;
    }
    
    if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        return std::make_unique<AssignStmt>(
            cloneExpression(assignStmt->target.get(), inductionVar, offset),
            assignStmt->op,
            cloneExpression(assignStmt->value.get(), inductionVar, offset),
            loc);
    }
    
    if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        auto newIf = std::make_unique<IfStmt>(
            cloneExpression(ifStmt->condition.get(), inductionVar, offset),
            cloneStatement(ifStmt->thenBranch.get(), inductionVar, offset),
            loc);
        for (auto& elif : ifStmt->elifBranches) {
            auto cond = cloneExpression(elif.first.get(), inductionVar, offset);
            auto body = cloneStatement(elif.second.get(), inductionVar, offset);
            newIf->elifBranches.push_back({std::move(cond), std::move(body)});
        }
        if (ifStmt->elseBranch) {
            newIf->elseBranch = cloneStatement(ifStmt->elseBranch.get(), inductionVar, offset);
        }
        return newIf;
    }
    
    if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        return std::make_unique<ReturnStmt>(
            returnStmt->value ? cloneExpression(returnStmt->value.get(), inductionVar, offset) : nullptr,
            loc);
    }
    
    if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
        // Clone the for loop - don't replace the inner loop's induction variable
        return std::make_unique<ForStmt>(
            forStmt->var,
            cloneExpression(forStmt->iterable.get(), inductionVar, offset),
            cloneStatement(forStmt->body.get(), inductionVar, offset),
            loc);
    }
    
    if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        return std::make_unique<WhileStmt>(
            cloneExpression(whileStmt->condition.get(), inductionVar, offset),
            cloneStatement(whileStmt->body.get(), inductionVar, offset),
            loc);
    }
    
    if (dynamic_cast<BreakStmt*>(stmt)) {
        return std::make_unique<BreakStmt>(loc);
    }
    
    if (dynamic_cast<ContinueStmt*>(stmt)) {
        return std::make_unique<ContinueStmt>(loc);
    }
    
    // For other statements, return nullptr (skip)
    return nullptr;
}

ExprPtr LoopUnrollingPass::cloneExpression(Expression* expr, const std::string& inductionVar, int64_t offset) {
    if (!expr) return nullptr;
    SourceLocation loc = expr->location;
    
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        if (ident->name == inductionVar) {
            // Replace induction variable with constant + offset
            return std::make_unique<IntegerLiteral>(offset, loc);
        }
        return std::make_unique<Identifier>(ident->name, loc);
    }
    
    if (auto* intLit = dynamic_cast<IntegerLiteral*>(expr)) {
        return std::make_unique<IntegerLiteral>(intLit->value, loc);
    }
    
    if (auto* floatLit = dynamic_cast<FloatLiteral*>(expr)) {
        return std::make_unique<FloatLiteral>(floatLit->value, loc);
    }
    
    if (auto* boolLit = dynamic_cast<BoolLiteral*>(expr)) {
        return std::make_unique<BoolLiteral>(boolLit->value, loc);
    }
    
    if (auto* strLit = dynamic_cast<StringLiteral*>(expr)) {
        return std::make_unique<StringLiteral>(strLit->value, loc);
    }
    
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        return std::make_unique<BinaryExpr>(
            cloneExpression(binary->left.get(), inductionVar, offset),
            binary->op,
            cloneExpression(binary->right.get(), inductionVar, offset),
            loc);
    }
    
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        return std::make_unique<UnaryExpr>(
            unary->op,
            cloneExpression(unary->operand.get(), inductionVar, offset),
            loc);
    }
    
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        auto newCall = std::make_unique<CallExpr>(
            cloneExpression(call->callee.get(), inductionVar, offset), loc);
        for (auto& arg : call->args) {
            newCall->args.push_back(cloneExpression(arg.get(), inductionVar, offset));
        }
        return newCall;
    }
    
    if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        return std::make_unique<IndexExpr>(
            cloneExpression(index->object.get(), inductionVar, offset),
            cloneExpression(index->index.get(), inductionVar, offset),
            loc);
    }
    
    if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        return std::make_unique<TernaryExpr>(
            cloneExpression(ternary->condition.get(), inductionVar, offset),
            cloneExpression(ternary->thenExpr.get(), inductionVar, offset),
            cloneExpression(ternary->elseExpr.get(), inductionVar, offset),
            loc);
    }
    
    if (auto* range = dynamic_cast<RangeExpr*>(expr)) {
        return std::make_unique<RangeExpr>(
            cloneExpression(range->start.get(), inductionVar, offset),
            cloneExpression(range->end.get(), inductionVar, offset),
            range->step ? cloneExpression(range->step.get(), inductionVar, offset) : nullptr,
            loc);
    }
    
    if (auto* interp = dynamic_cast<InterpolatedString*>(expr)) {
        auto newInterp = std::make_unique<InterpolatedString>(loc);
        for (auto& part : interp->parts) {
            if (std::holds_alternative<std::string>(part)) {
                newInterp->parts.push_back(std::get<std::string>(part));
            } else if (std::holds_alternative<ExprPtr>(part)) {
                auto cloned = cloneExpression(std::get<ExprPtr>(part).get(), inductionVar, offset);
                if (cloned) {
                    newInterp->parts.push_back(std::move(cloned));
                } else {
                    // If we can't clone the expression, return nullptr to prevent partial cloning
                    return nullptr;
                }
            }
        }
        return newInterp;
    }
    
    if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        return std::make_unique<MemberExpr>(
            cloneExpression(member->object.get(), inductionVar, offset),
            member->member,
            loc);
    }
    
    if (auto* list = dynamic_cast<ListExpr*>(expr)) {
        auto newList = std::make_unique<ListExpr>(loc);
        for (auto& elem : list->elements) {
            auto cloned = cloneExpression(elem.get(), inductionVar, offset);
            if (cloned) {
                newList->elements.push_back(std::move(cloned));
            } else {
                return nullptr;
            }
        }
        return newList;
    }
    
    if (auto* record = dynamic_cast<RecordExpr*>(expr)) {
        auto newRecord = std::make_unique<RecordExpr>(loc);
        for (auto& field : record->fields) {
            auto cloned = cloneExpression(field.second.get(), inductionVar, offset);
            if (cloned) {
                newRecord->fields.push_back({field.first, std::move(cloned)});
            } else {
                return nullptr;
            }
        }
        return newRecord;
    }
    
    if (auto* map = dynamic_cast<MapExpr*>(expr)) {
        auto newMap = std::make_unique<MapExpr>(loc);
        for (auto& entry : map->entries) {
            auto clonedKey = cloneExpression(entry.first.get(), inductionVar, offset);
            auto clonedVal = cloneExpression(entry.second.get(), inductionVar, offset);
            if (clonedKey && clonedVal) {
                newMap->entries.push_back({std::move(clonedKey), std::move(clonedVal)});
            } else {
                return nullptr;
            }
        }
        return newMap;
    }
    
    if (auto* nilLit = dynamic_cast<NilLiteral*>(expr)) {
        return std::make_unique<NilLiteral>(loc);
    }
    
    if (auto* assignExpr = dynamic_cast<AssignExpr*>(expr)) {
        return std::make_unique<AssignExpr>(
            cloneExpression(assignExpr->target.get(), inductionVar, offset),
            assignExpr->op,
            cloneExpression(assignExpr->value.get(), inductionVar, offset),
            loc);
    }
    
    if (auto* propagate = dynamic_cast<PropagateExpr*>(expr)) {
        return std::make_unique<PropagateExpr>(
            cloneExpression(propagate->operand.get(), inductionVar, offset),
            loc);
    }
    
    // Default: return nullptr
    return nullptr;
}

// ============================================
// LICM (Loop Invariant Code Motion) Pass
// ============================================

void LICMPass::run(Program& ast) {
    transformations_ = 0;
    processStatements(ast.statements);
}

void LICMPass::processStatements(std::vector<StmtPtr>& stmts) {
    for (size_t i = 0; i < stmts.size(); ++i) {
        auto* stmt = stmts[i].get();
        
        if (auto* forLoop = dynamic_cast<ForStmt*>(stmt)) {
            std::vector<StmtPtr> hoisted;
            hoistInvariantCode(forLoop, hoisted);
            
            if (!hoisted.empty()) {
                // Insert hoisted statements before the loop
                for (auto& h : hoisted) {
                    stmts.insert(stmts.begin() + i, std::move(h));
                    i++;
                    transformations_++;
                }
            }
            
            // Process loop body recursively
            if (auto* block = dynamic_cast<Block*>(forLoop->body.get())) {
                processStatements(block->statements);
            }
        }
        else if (auto* whileLoop = dynamic_cast<WhileStmt*>(stmt)) {
            std::vector<StmtPtr> hoisted;
            hoistInvariantCode(whileLoop, hoisted);
            
            if (!hoisted.empty()) {
                for (auto& h : hoisted) {
                    stmts.insert(stmts.begin() + i, std::move(h));
                    i++;
                    transformations_++;
                }
            }
            
            if (auto* block = dynamic_cast<Block*>(whileLoop->body.get())) {
                processStatements(block->statements);
            }
        }
        else if (auto* block = dynamic_cast<Block*>(stmt)) {
            processStatements(block->statements);
        }
        else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
            if (auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
                processStatements(thenBlock->statements);
            }
            for (auto& elif : ifStmt->elifBranches) {
                if (auto* elifBlock = dynamic_cast<Block*>(elif.second.get())) {
                    processStatements(elifBlock->statements);
                }
            }
            if (auto* elseBlock = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
                processStatements(elseBlock->statements);
            }
        }
        else if (auto* fnDecl = dynamic_cast<FnDecl*>(stmt)) {
            if (auto* block = dynamic_cast<Block*>(fnDecl->body.get())) {
                processStatements(block->statements);
            }
        }
    }
}

void LICMPass::analyzeModifiedVars(Statement* stmt, std::set<std::string>& modified) {
    if (!stmt) return;
    
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            analyzeModifiedVars(s.get(), modified);
        }
    }
    else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        modified.insert(varDecl->name);
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        if (auto* ident = dynamic_cast<Identifier*>(assignStmt->target.get())) {
            modified.insert(ident->name);
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        analyzeModifiedVars(ifStmt->thenBranch.get(), modified);
        for (auto& elif : ifStmt->elifBranches) {
            analyzeModifiedVars(elif.second.get(), modified);
        }
        analyzeModifiedVars(ifStmt->elseBranch.get(), modified);
    }
    else if (auto* forLoop = dynamic_cast<ForStmt*>(stmt)) {
        modified.insert(forLoop->var);
        analyzeModifiedVars(forLoop->body.get(), modified);
    }
    else if (auto* whileLoop = dynamic_cast<WhileStmt*>(stmt)) {
        analyzeModifiedVars(whileLoop->body.get(), modified);
    }
}

bool LICMPass::isLoopInvariant(Expression* expr, const std::set<std::string>& modified,
                               const std::string& inductionVar) {
    if (!expr) return true;
    
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        // Induction variable is not invariant
        if (ident->name == inductionVar) return false;
        // Variables modified in the loop are not invariant
        if (modified.count(ident->name)) return false;
        return true;
    }
    
    if (dynamic_cast<IntegerLiteral*>(expr)) return true;
    if (dynamic_cast<FloatLiteral*>(expr)) return true;
    if (dynamic_cast<BoolLiteral*>(expr)) return true;
    if (dynamic_cast<StringLiteral*>(expr)) return true;
    
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        return isLoopInvariant(binary->left.get(), modified, inductionVar) &&
               isLoopInvariant(binary->right.get(), modified, inductionVar);
    }
    
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        return isLoopInvariant(unary->operand.get(), modified, inductionVar);
    }
    
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        // Function calls are generally not invariant (may have side effects)
        // Exception: pure functions with invariant arguments
        if (auto* callee = dynamic_cast<Identifier*>(call->callee.get())) {
            // Known pure functions
            if (callee->name == "abs" || callee->name == "sqrt" || 
                callee->name == "sin" || callee->name == "cos" ||
                callee->name == "min" || callee->name == "max") {
                for (auto& arg : call->args) {
                    if (!isLoopInvariant(arg.get(), modified, inductionVar)) {
                        return false;
                    }
                }
                return true;
            }
        }
        return false;
    }
    
    if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        // Array access is invariant only if both object and index are invariant
        return isLoopInvariant(index->object.get(), modified, inductionVar) &&
               isLoopInvariant(index->index.get(), modified, inductionVar);
    }
    
    return false;
}

bool LICMPass::isStatementInvariant(Statement* stmt, const std::set<std::string>& modified,
                                     const std::string& inductionVar) {
    if (!stmt) return false;
    
    // IMPORTANT: Variable declarations inside loops should NEVER be hoisted!
    // Even if the initializer is loop-invariant, the declaration creates a new
    // binding on each iteration. Hoisting it would change the semantics.
    // For example:
    //   while i <= n:
    //       temp := a + b   // Creates new 'temp' each iteration
    //       a = b
    //       b = temp
    // If we hoist 'temp := a + b', it would only be computed once with initial values.
    
    // Only expression statements with invariant expressions can be hoisted
    // (and even then, only if they have no side effects)
    // For now, we disable hoisting of variable declarations entirely.
    
    return false;
}

void LICMPass::hoistInvariantCode(ForStmt* loop, std::vector<StmtPtr>& hoisted) {
    std::set<std::string> modified;
    analyzeModifiedVars(loop->body.get(), modified);
    modified.insert(loop->var);  // Induction variable is modified
    
    auto* body = dynamic_cast<Block*>(loop->body.get());
    if (!body) return;
    
    // Find invariant statements
    auto it = body->statements.begin();
    while (it != body->statements.end()) {
        if (isStatementInvariant(it->get(), modified, loop->var)) {
            hoisted.push_back(std::move(*it));
            it = body->statements.erase(it);
        } else {
            ++it;
        }
    }
}

void LICMPass::hoistInvariantCode(WhileStmt* loop, std::vector<StmtPtr>& hoisted) {
    std::set<std::string> modified;
    analyzeModifiedVars(loop->body.get(), modified);
    
    auto* body = dynamic_cast<Block*>(loop->body.get());
    if (!body) return;
    
    auto it = body->statements.begin();
    while (it != body->statements.end()) {
        if (isStatementInvariant(it->get(), modified, "")) {
            hoisted.push_back(std::move(*it));
            it = body->statements.erase(it);
        } else {
            ++it;
        }
    }
}

// ============================================
// Strength Reduction Pass
// ============================================

void StrengthReductionPass::run(Program& ast) {
    transformations_ = 0;
    processStatements(ast.statements);
}

void StrengthReductionPass::processStatements(std::vector<StmtPtr>& stmts) {
    for (auto& stmt : stmts) {
        processStatement(stmt.get());
    }
}

void StrengthReductionPass::processStatement(Statement* stmt) {
    if (!stmt) return;
    
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        processStatements(block->statements);
    }
    else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        if (varDecl->initializer) {
            auto transformed = transformExpression(varDecl->initializer.get());
            if (transformed) {
                varDecl->initializer = std::move(transformed);
                transformations_++;
            }
        }
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        auto transformed = transformExpression(assignStmt->value.get());
        if (transformed) {
            assignStmt->value = std::move(transformed);
            transformations_++;
        }
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        auto transformed = transformExpression(exprStmt->expr.get());
        if (transformed) {
            exprStmt->expr = std::move(transformed);
            transformations_++;
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        auto transformed = transformExpression(ifStmt->condition.get());
        if (transformed) {
            ifStmt->condition = std::move(transformed);
            transformations_++;
        }
        processStatement(ifStmt->thenBranch.get());
        for (auto& elif : ifStmt->elifBranches) {
            transformed = transformExpression(elif.first.get());
            if (transformed) {
                elif.first = std::move(transformed);
                transformations_++;
            }
            processStatement(elif.second.get());
        }
        processStatement(ifStmt->elseBranch.get());
    }
    else if (auto* forLoop = dynamic_cast<ForStmt*>(stmt)) {
        processStatement(forLoop->body.get());
    }
    else if (auto* whileLoop = dynamic_cast<WhileStmt*>(stmt)) {
        auto transformed = transformExpression(whileLoop->condition.get());
        if (transformed) {
            whileLoop->condition = std::move(transformed);
            transformations_++;
        }
        processStatement(whileLoop->body.get());
    }
    else if (auto* fnDecl = dynamic_cast<FnDecl*>(stmt)) {
        processStatement(fnDecl->body.get());
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        if (returnStmt->value) {
            auto transformed = transformExpression(returnStmt->value.get());
            if (transformed) {
                returnStmt->value = std::move(transformed);
                transformations_++;
            }
        }
    }
}

bool StrengthReductionPass::isPowerOf2(int64_t value) {
    return value > 0 && (value & (value - 1)) == 0;
}

int StrengthReductionPass::log2(int64_t value) {
    int result = 0;
    while (value > 1) {
        value >>= 1;
        result++;
    }
    return result;
}

ExprPtr StrengthReductionPass::transformExpression(Expression* expr) {
    if (!expr) return nullptr;
    SourceLocation loc = expr->location;
    
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        // Transform multiplication by power of 2 to addition chain
        // x * 2 -> x + x (simpler case)
        // Note: Full shift optimization would require adding shift operators to the language
        if (binary->op == TokenType::STAR) {
            if (auto* rightLit = dynamic_cast<IntegerLiteral*>(binary->right.get())) {
                // x * 2 -> x + x
                if (rightLit->value == 2) {
                    if (auto* leftIdent = dynamic_cast<Identifier*>(binary->left.get())) {
                        return std::make_unique<BinaryExpr>(
                            std::make_unique<Identifier>(leftIdent->name, loc),
                            TokenType::PLUS,
                            std::make_unique<Identifier>(leftIdent->name, loc),
                            loc);
                    }
                }
                // x * 0 -> 0
                if (rightLit->value == 0) {
                    return std::make_unique<IntegerLiteral>(0, loc);
                }
                // x * 1 -> x
                if (rightLit->value == 1) {
                    if (auto* leftIdent = dynamic_cast<Identifier*>(binary->left.get())) {
                        return std::make_unique<Identifier>(leftIdent->name, loc);
                    }
                    if (auto* leftLit = dynamic_cast<IntegerLiteral*>(binary->left.get())) {
                        return std::make_unique<IntegerLiteral>(leftLit->value, loc);
                    }
                }
            }
            // Also check left operand for 2 * x -> x + x
            if (auto* leftLit = dynamic_cast<IntegerLiteral*>(binary->left.get())) {
                if (leftLit->value == 2) {
                    if (auto* rightIdent = dynamic_cast<Identifier*>(binary->right.get())) {
                        return std::make_unique<BinaryExpr>(
                            std::make_unique<Identifier>(rightIdent->name, loc),
                            TokenType::PLUS,
                            std::make_unique<Identifier>(rightIdent->name, loc),
                            loc);
                    }
                }
                // 0 * x -> 0
                if (leftLit->value == 0) {
                    return std::make_unique<IntegerLiteral>(0, loc);
                }
                // 1 * x -> x
                if (leftLit->value == 1) {
                    if (auto* rightIdent = dynamic_cast<Identifier*>(binary->right.get())) {
                        return std::make_unique<Identifier>(rightIdent->name, loc);
                    }
                    if (auto* rightLitVal = dynamic_cast<IntegerLiteral*>(binary->right.get())) {
                        return std::make_unique<IntegerLiteral>(rightLitVal->value, loc);
                    }
                }
            }
        }
        
        // Transform division by 1 -> identity
        // x / 1 -> x
        if (binary->op == TokenType::SLASH) {
            if (auto* rightLit = dynamic_cast<IntegerLiteral*>(binary->right.get())) {
                if (rightLit->value == 1) {
                    if (auto* leftIdent = dynamic_cast<Identifier*>(binary->left.get())) {
                        return std::make_unique<Identifier>(leftIdent->name, loc);
                    }
                    if (auto* leftLit = dynamic_cast<IntegerLiteral*>(binary->left.get())) {
                        return std::make_unique<IntegerLiteral>(leftLit->value, loc);
                    }
                }
            }
        }
        
        // Transform modulo optimizations
        // x % 1 -> 0
        if (binary->op == TokenType::PERCENT) {
            if (auto* rightLit = dynamic_cast<IntegerLiteral*>(binary->right.get())) {
                if (rightLit->value == 1) {
                    return std::make_unique<IntegerLiteral>(0, loc);
                }
            }
        }
        
        // Addition/subtraction with 0
        // x + 0 -> x, x - 0 -> x
        if (binary->op == TokenType::PLUS || binary->op == TokenType::MINUS) {
            if (auto* rightLit = dynamic_cast<IntegerLiteral*>(binary->right.get())) {
                if (rightLit->value == 0) {
                    if (auto* leftIdent = dynamic_cast<Identifier*>(binary->left.get())) {
                        return std::make_unique<Identifier>(leftIdent->name, loc);
                    }
                    if (auto* leftLit = dynamic_cast<IntegerLiteral*>(binary->left.get())) {
                        return std::make_unique<IntegerLiteral>(leftLit->value, loc);
                    }
                }
            }
            // 0 + x -> x
            if (binary->op == TokenType::PLUS) {
                if (auto* leftLit = dynamic_cast<IntegerLiteral*>(binary->left.get())) {
                    if (leftLit->value == 0) {
                        if (auto* rightIdent = dynamic_cast<Identifier*>(binary->right.get())) {
                            return std::make_unique<Identifier>(rightIdent->name, loc);
                        }
                        if (auto* rightLitVal = dynamic_cast<IntegerLiteral*>(binary->right.get())) {
                            return std::make_unique<IntegerLiteral>(rightLitVal->value, loc);
                        }
                    }
                }
            }
        }
    }
    
    return nullptr;
}

// ============================================
// Combined Loop Optimization Pass
// ============================================

void LoopOptimizationPass::run(Program& ast) {
    transformations_ = 0;
    
    // Run strength reduction first (simplifies expressions)
    if (strengthReductionEnabled_) {
        StrengthReductionPass sr;
        sr.run(ast);
        transformations_ += sr.transformations();
    }
    
    // Run LICM (hoist invariant code)
    if (licmEnabled_) {
        LICMPass licm;
        licm.run(ast);
        transformations_ += licm.transformations();
    }
    
    // Run loop unrolling
    if (unrollingEnabled_) {
        LoopUnrollingPass unroll;
        unroll.run(ast);
        transformations_ += unroll.transformations();
    }
}

} // namespace tyl
