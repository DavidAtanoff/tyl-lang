// Tyl Compiler - Loop Peeling Implementation
#include "loop_peeling.h"
#include <algorithm>

namespace tyl {

void LoopPeelingPass::run(Program& ast) {
    transformations_ = 0;
    stats_ = LoopPeelingStats{};
    
    for (auto& stmt : ast.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            processFunction(fn);
        }
    }
    
    transformations_ = stats_.loopsPeeled;
}

void LoopPeelingPass::processFunction(FnDecl* fn) {
    if (!fn->body) return;
    
    if (auto* block = dynamic_cast<Block*>(fn->body.get())) {
        processStatements(block->statements);
    }
}

void LoopPeelingPass::processStatements(std::vector<StmtPtr>& stmts) {
    for (size_t i = 0; i < stmts.size(); ++i) {
        auto* stmt = stmts[i].get();
        
        if (auto* forLoop = dynamic_cast<ForStmt*>(stmt)) {
            if (tryPeelForLoop(stmts, i, forLoop)) {
                // Statements were inserted, adjust index
                continue;
            }
            
            // Process nested loops
            if (forLoop->body) {
                if (auto* block = dynamic_cast<Block*>(forLoop->body.get())) {
                    processStatements(block->statements);
                }
            }
        }
        else if (auto* whileLoop = dynamic_cast<WhileStmt*>(stmt)) {
            if (whileLoop->body) {
                if (auto* block = dynamic_cast<Block*>(whileLoop->body.get())) {
                    processStatements(block->statements);
                }
            }
        }
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

bool LoopPeelingPass::tryPeelForLoop(std::vector<StmtPtr>& stmts, size_t index, 
                                      ForStmt* loop) {
    if (!shouldPeelLoop(loop)) return false;
    
    int64_t start, end, step;
    if (!hasKnownTripCount(loop, start, end, step)) return false;
    
    // Calculate trip count
    int64_t tripCount = (end - start + step - 1) / step;
    if (tripCount <= maxPeelCount_) return false;  // Don't peel if loop is too small
    
    std::vector<StmtPtr> newStmts;
    int peelCount = std::min(maxPeelCount_, static_cast<int>(tripCount - 1));
    
    // Peel first iterations
    if (peelFirst_ && peelCount > 0) {
        for (int i = 0; i < peelCount; ++i) {
            int64_t iterValue = start + i * step;
            auto peeled = createPeeledIteration(loop->body.get(), loop->var, iterValue);
            if (peeled) {
                newStmts.push_back(std::move(peeled));
                ++stats_.firstIterationsPeeled;
            }
        }
        
        // Adjust the loop to start after peeled iterations
        int64_t newStart = start + peelCount * step;
        
        // Create new range expression
        ExprPtr newIterable;
        if (auto* range = dynamic_cast<RangeExpr*>(loop->iterable.get())) {
            auto newRange = std::make_unique<RangeExpr>(
                std::make_unique<IntegerLiteral>(newStart, range->location),
                cloneExpression(range->end.get()),
                range->step ? cloneExpression(range->step.get()) : nullptr,
                range->location);
            newIterable = std::move(newRange);
        }
        else if (auto* call = dynamic_cast<CallExpr*>(loop->iterable.get())) {
            if (auto* callee = dynamic_cast<Identifier*>(call->callee.get())) {
                if (callee->name == "range") {
                    auto newCall = std::make_unique<CallExpr>(
                        std::make_unique<Identifier>("range", call->location),
                        call->location);
                    newCall->args.push_back(
                        std::make_unique<IntegerLiteral>(newStart, call->location));
                    if (call->args.size() >= 2) {
                        newCall->args.push_back(cloneExpression(call->args[1].get()));
                    }
                    if (call->args.size() >= 3) {
                        newCall->args.push_back(cloneExpression(call->args[2].get()));
                    }
                    newIterable = std::move(newCall);
                }
            }
        }
        
        if (newIterable) {
            auto newLoop = std::make_unique<ForStmt>(
                loop->var,
                std::move(newIterable),
                cloneStatement(loop->body.get()),
                loop->location);
            newStmts.push_back(std::move(newLoop));
        }
        
        // Replace original loop with peeled iterations + modified loop
        stmts.erase(stmts.begin() + index);
        stmts.insert(stmts.begin() + index, 
                    std::make_move_iterator(newStmts.begin()),
                    std::make_move_iterator(newStmts.end()));
        
        ++stats_.loopsPeeled;
        stats_.iterationsPeeled += peelCount;
        return true;
    }
    
    return false;
}

bool LoopPeelingPass::shouldPeelLoop(ForStmt* loop) {
    if (!loop || !loop->body) return false;
    
    // Check if the loop body contains operations that would benefit from peeling
    // Examples:
    // - Array bounds checks that can be eliminated for first/last iteration
    // - Conditional checks on the loop variable
    // - Initialization patterns
    
    auto* body = dynamic_cast<Block*>(loop->body.get());
    if (!body) return false;
    
    // Look for patterns that benefit from peeling
    for (auto& stmt : body->statements) {
        // Check for conditionals on the loop variable
        if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
            if (usesLoopVar(ifStmt->condition.get(), loop->var)) {
                return true;  // Peeling can eliminate this check
            }
        }
        
        // Check for array access with loop variable
        if (containsIndexWithVar(stmt.get(), loop->var)) {
            return true;  // Peeling can help with bounds check elimination
        }
    }
    
    return false;
}

bool LoopPeelingPass::hasKnownTripCount(ForStmt* loop, int64_t& start, 
                                         int64_t& end, int64_t& step) {
    step = 1;  // Default step
    
    if (auto* range = dynamic_cast<RangeExpr*>(loop->iterable.get())) {
        if (!evaluateConstant(range->start.get(), start)) return false;
        if (!evaluateConstant(range->end.get(), end)) return false;
        if (range->step) {
            if (!evaluateConstant(range->step.get(), step)) return false;
        }
        return true;
    }
    
    if (auto* call = dynamic_cast<CallExpr*>(loop->iterable.get())) {
        if (auto* callee = dynamic_cast<Identifier*>(call->callee.get())) {
            if (callee->name == "range") {
                if (call->args.size() == 1) {
                    start = 0;
                    if (!evaluateConstant(call->args[0].get(), end)) return false;
                    return true;
                }
                if (call->args.size() >= 2) {
                    if (!evaluateConstant(call->args[0].get(), start)) return false;
                    if (!evaluateConstant(call->args[1].get(), end)) return false;
                    if (call->args.size() >= 3) {
                        if (!evaluateConstant(call->args[2].get(), step)) return false;
                    }
                    return true;
                }
            }
        }
    }
    
    return false;
}

StmtPtr LoopPeelingPass::createPeeledIteration(Statement* body, 
                                                const std::string& loopVar, 
                                                int64_t value) {
    return substituteVarInStmt(body, loopVar, value);
}

ExprPtr LoopPeelingPass::substituteVar(Expression* expr, const std::string& var, 
                                        int64_t value) {
    if (!expr) return nullptr;
    
    if (auto* id = dynamic_cast<Identifier*>(expr)) {
        if (id->name == var) {
            return std::make_unique<IntegerLiteral>(value, id->location);
        }
        return std::make_unique<Identifier>(id->name, id->location);
    }
    
    if (auto* lit = dynamic_cast<IntegerLiteral*>(expr)) {
        return std::make_unique<IntegerLiteral>(lit->value, lit->location, lit->suffix);
    }
    if (auto* lit = dynamic_cast<FloatLiteral*>(expr)) {
        return std::make_unique<FloatLiteral>(lit->value, lit->location, lit->suffix);
    }
    if (auto* lit = dynamic_cast<BoolLiteral*>(expr)) {
        return std::make_unique<BoolLiteral>(lit->value, lit->location);
    }
    if (auto* lit = dynamic_cast<StringLiteral*>(expr)) {
        return std::make_unique<StringLiteral>(lit->value, lit->location);
    }
    
    if (auto* bin = dynamic_cast<BinaryExpr*>(expr)) {
        return std::make_unique<BinaryExpr>(
            substituteVar(bin->left.get(), var, value),
            bin->op,
            substituteVar(bin->right.get(), var, value),
            bin->location);
    }
    
    if (auto* un = dynamic_cast<UnaryExpr*>(expr)) {
        return std::make_unique<UnaryExpr>(
            un->op,
            substituteVar(un->operand.get(), var, value),
            un->location);
    }
    
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        auto newCall = std::make_unique<CallExpr>(
            substituteVar(call->callee.get(), var, value),
            call->location);
        for (auto& arg : call->args) {
            newCall->args.push_back(substituteVar(arg.get(), var, value));
        }
        return newCall;
    }
    
    if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        return std::make_unique<IndexExpr>(
            substituteVar(index->object.get(), var, value),
            substituteVar(index->index.get(), var, value),
            index->location);
    }
    
    if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        return std::make_unique<MemberExpr>(
            substituteVar(member->object.get(), var, value),
            member->member,
            member->location);
    }
    
    if (auto* assign = dynamic_cast<AssignExpr*>(expr)) {
        return std::make_unique<AssignExpr>(
            substituteVar(assign->target.get(), var, value),
            assign->op,
            substituteVar(assign->value.get(), var, value),
            assign->location);
    }
    
    if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        return std::make_unique<TernaryExpr>(
            substituteVar(ternary->condition.get(), var, value),
            substituteVar(ternary->thenExpr.get(), var, value),
            substituteVar(ternary->elseExpr.get(), var, value),
            ternary->location);
    }
    
    return cloneExpression(expr);
}

StmtPtr LoopPeelingPass::substituteVarInStmt(Statement* stmt, const std::string& var, 
                                              int64_t value) {
    if (!stmt) return nullptr;
    
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        auto clone = std::make_unique<VarDecl>(
            varDecl->name, varDecl->typeName,
            substituteVar(varDecl->initializer.get(), var, value),
            varDecl->location);
        clone->isMutable = varDecl->isMutable;
        clone->isConst = varDecl->isConst;
        return clone;
    }
    
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        return std::make_unique<ExprStmt>(
            substituteVar(exprStmt->expr.get(), var, value),
            exprStmt->location);
    }
    
    if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        return std::make_unique<ReturnStmt>(
            substituteVar(returnStmt->value.get(), var, value),
            returnStmt->location);
    }
    
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        auto clone = std::make_unique<Block>(block->location);
        for (auto& s : block->statements) {
            if (auto subst = substituteVarInStmt(s.get(), var, value)) {
                clone->statements.push_back(std::move(subst));
            }
        }
        return clone;
    }
    
    if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        auto clone = std::make_unique<IfStmt>(
            substituteVar(ifStmt->condition.get(), var, value),
            substituteVarInStmt(ifStmt->thenBranch.get(), var, value),
            ifStmt->location);
        if (ifStmt->elseBranch) {
            clone->elseBranch = substituteVarInStmt(ifStmt->elseBranch.get(), var, value);
        }
        return clone;
    }
    
    if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        return std::make_unique<WhileStmt>(
            substituteVar(whileStmt->condition.get(), var, value),
            substituteVarInStmt(whileStmt->body.get(), var, value),
            whileStmt->location);
    }
    
    if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
        // Don't substitute in nested for loops with the same variable
        if (forStmt->var == var) {
            return cloneStatement(stmt);
        }
        return std::make_unique<ForStmt>(
            forStmt->var,
            substituteVar(forStmt->iterable.get(), var, value),
            substituteVarInStmt(forStmt->body.get(), var, value),
            forStmt->location);
    }
    
    if (auto* breakStmt = dynamic_cast<BreakStmt*>(stmt)) {
        return std::make_unique<BreakStmt>(breakStmt->location);
    }
    
    if (auto* continueStmt = dynamic_cast<ContinueStmt*>(stmt)) {
        return std::make_unique<ContinueStmt>(continueStmt->location);
    }
    
    return cloneStatement(stmt);
}

StmtPtr LoopPeelingPass::cloneStatement(Statement* stmt) {
    if (!stmt) return nullptr;
    
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        auto clone = std::make_unique<VarDecl>(
            varDecl->name, varDecl->typeName,
            cloneExpression(varDecl->initializer.get()),
            varDecl->location);
        clone->isMutable = varDecl->isMutable;
        clone->isConst = varDecl->isConst;
        return clone;
    }
    
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        return std::make_unique<ExprStmt>(
            cloneExpression(exprStmt->expr.get()),
            exprStmt->location);
    }
    
    if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        return std::make_unique<AssignStmt>(
            cloneExpression(assignStmt->target.get()),
            assignStmt->op,
            cloneExpression(assignStmt->value.get()),
            assignStmt->location);
    }
    
    if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        return std::make_unique<ReturnStmt>(
            cloneExpression(returnStmt->value.get()),
            returnStmt->location);
    }
    
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        auto clone = std::make_unique<Block>(block->location);
        for (auto& s : block->statements) {
            if (auto cloned = cloneStatement(s.get())) {
                clone->statements.push_back(std::move(cloned));
            }
        }
        return clone;
    }
    
    if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        auto clone = std::make_unique<IfStmt>(
            cloneExpression(ifStmt->condition.get()),
            cloneStatement(ifStmt->thenBranch.get()),
            ifStmt->location);
        clone->elseBranch = cloneStatement(ifStmt->elseBranch.get());
        return clone;
    }
    
    if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        return std::make_unique<WhileStmt>(
            cloneExpression(whileStmt->condition.get()),
            cloneStatement(whileStmt->body.get()),
            whileStmt->location);
    }
    
    if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
        return std::make_unique<ForStmt>(
            forStmt->var,
            cloneExpression(forStmt->iterable.get()),
            cloneStatement(forStmt->body.get()),
            forStmt->location);
    }
    
    if (auto* breakStmt = dynamic_cast<BreakStmt*>(stmt)) {
        return std::make_unique<BreakStmt>(breakStmt->location);
    }
    
    if (auto* continueStmt = dynamic_cast<ContinueStmt*>(stmt)) {
        return std::make_unique<ContinueStmt>(continueStmt->location);
    }
    
    return nullptr;
}

ExprPtr LoopPeelingPass::cloneExpression(Expression* expr) {
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
    if (auto* lit = dynamic_cast<StringLiteral*>(expr)) {
        return std::make_unique<StringLiteral>(lit->value, lit->location);
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
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        auto clone = std::make_unique<CallExpr>(
            cloneExpression(call->callee.get()),
            call->location);
        for (auto& arg : call->args) {
            clone->args.push_back(cloneExpression(arg.get()));
        }
        return clone;
    }
    if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        return std::make_unique<MemberExpr>(
            cloneExpression(member->object.get()),
            member->member,
            member->location);
    }
    if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        return std::make_unique<IndexExpr>(
            cloneExpression(index->object.get()),
            cloneExpression(index->index.get()),
            index->location);
    }
    if (auto* assign = dynamic_cast<AssignExpr*>(expr)) {
        return std::make_unique<AssignExpr>(
            cloneExpression(assign->target.get()),
            assign->op,
            cloneExpression(assign->value.get()),
            assign->location);
    }
    if (auto* range = dynamic_cast<RangeExpr*>(expr)) {
        auto clone = std::make_unique<RangeExpr>(
            cloneExpression(range->start.get()),
            cloneExpression(range->end.get()),
            range->step ? cloneExpression(range->step.get()) : nullptr,
            range->location);
        return clone;
    }
    
    return nullptr;
}

bool LoopPeelingPass::evaluateConstant(Expression* expr, int64_t& value) {
    if (!expr) return false;
    
    if (auto* lit = dynamic_cast<IntegerLiteral*>(expr)) {
        value = lit->value;
        return true;
    }
    
    if (auto* un = dynamic_cast<UnaryExpr*>(expr)) {
        if (un->op == TokenType::MINUS) {
            int64_t inner;
            if (evaluateConstant(un->operand.get(), inner)) {
                value = -inner;
                return true;
            }
        }
    }
    
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
                default: return false;
            }
        }
    }
    
    return false;
}

// Helper functions
bool LoopPeelingPass::usesLoopVar(Expression* expr, const std::string& var) {
    if (!expr) return false;
    
    if (auto* id = dynamic_cast<Identifier*>(expr)) {
        return id->name == var;
    }
    if (auto* bin = dynamic_cast<BinaryExpr*>(expr)) {
        return usesLoopVar(bin->left.get(), var) || usesLoopVar(bin->right.get(), var);
    }
    if (auto* un = dynamic_cast<UnaryExpr*>(expr)) {
        return usesLoopVar(un->operand.get(), var);
    }
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        for (auto& arg : call->args) {
            if (usesLoopVar(arg.get(), var)) return true;
        }
    }
    if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        return usesLoopVar(index->object.get(), var) || usesLoopVar(index->index.get(), var);
    }
    
    return false;
}

bool LoopPeelingPass::containsIndexWithVar(Statement* stmt, const std::string& var) {
    if (!stmt) return false;
    
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        return containsIndexExprWithVar(exprStmt->expr.get(), var);
    }
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        return containsIndexExprWithVar(varDecl->initializer.get(), var);
    }
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            if (containsIndexWithVar(s.get(), var)) return true;
        }
    }
    if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        return containsIndexWithVar(ifStmt->thenBranch.get(), var) ||
               containsIndexWithVar(ifStmt->elseBranch.get(), var);
    }
    
    return false;
}

bool LoopPeelingPass::containsIndexExprWithVar(Expression* expr, const std::string& var) {
    if (!expr) return false;
    
    if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        if (usesLoopVar(index->index.get(), var)) return true;
    }
    if (auto* bin = dynamic_cast<BinaryExpr*>(expr)) {
        return containsIndexExprWithVar(bin->left.get(), var) ||
               containsIndexExprWithVar(bin->right.get(), var);
    }
    if (auto* un = dynamic_cast<UnaryExpr*>(expr)) {
        return containsIndexExprWithVar(un->operand.get(), var);
    }
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        for (auto& arg : call->args) {
            if (containsIndexExprWithVar(arg.get(), var)) return true;
        }
    }
    
    return false;
}

} // namespace tyl
