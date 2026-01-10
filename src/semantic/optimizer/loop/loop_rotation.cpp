// Tyl Compiler - Loop Rotation Implementation
#include "loop_rotation.h"
#include <algorithm>
#include <iostream>

namespace tyl {

void LoopRotationPass::run(Program& ast) {
    transformations_ = 0;
    stats_ = LoopRotationStats{};
    
    // Process all statements
    processStatements(ast.statements);
    
    // Sum up transformations
    transformations_ = stats_.whileLoopsRotated + stats_.forLoopsRotated;
}

void LoopRotationPass::processStatements(std::vector<StmtPtr>& stmts) {
    for (size_t i = 0; i < stmts.size(); ++i) {
        processStatement(stmts[i]);
        
        // Try to rotate loops
        if (auto* whileLoop = dynamic_cast<WhileStmt*>(stmts[i].get())) {
            if (shouldRotate(whileLoop)) {
                auto rotated = tryRotateWhileLoop(whileLoop);
                if (rotated) {
                    stmts[i] = std::move(rotated);
                }
            }
        }
        else if (auto* forLoop = dynamic_cast<ForStmt*>(stmts[i].get())) {
            // For loops in Tyl are already in a good form for most cases
            // Only rotate if it would help (e.g., complex condition)
            if (shouldRotate(forLoop)) {
                auto rotated = tryRotateForLoop(forLoop);
                if (rotated) {
                    stmts[i] = std::move(rotated);
                }
            }
        }
    }
}

void LoopRotationPass::processStatement(StmtPtr& stmt) {
    if (!stmt) return;
    
    // Recurse into nested structures
    if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
        if (fn->body) {
            if (auto* block = dynamic_cast<Block*>(fn->body.get())) {
                processStatements(block->statements);
            }
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
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
        for (auto& elif : ifStmt->elifBranches) {
            if (elif.second) {
                if (auto* block = dynamic_cast<Block*>(elif.second.get())) {
                    processStatements(block->statements);
                }
            }
        }
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt.get())) {
        if (whileStmt->body) {
            if (auto* block = dynamic_cast<Block*>(whileStmt->body.get())) {
                processStatements(block->statements);
            }
        }
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt.get())) {
        if (forStmt->body) {
            if (auto* block = dynamic_cast<Block*>(forStmt->body.get())) {
                processStatements(block->statements);
            }
        }
    }
    else if (auto* block = dynamic_cast<Block*>(stmt.get())) {
        processStatements(block->statements);
    }
    else if (auto* matchStmt = dynamic_cast<MatchStmt*>(stmt.get())) {
        for (auto& c : matchStmt->cases) {
            if (c.body) {
                if (auto* block = dynamic_cast<Block*>(c.body.get())) {
                    processStatements(block->statements);
                }
            }
        }
        if (matchStmt->defaultCase) {
            if (auto* block = dynamic_cast<Block*>(matchStmt->defaultCase.get())) {
                processStatements(block->statements);
            }
        }
    }
}

StmtPtr LoopRotationPass::tryRotateWhileLoop(WhileStmt* loop) {
    if (!loop || !loop->body) return nullptr;
    
    // Don't rotate labeled loops (complex control flow)
    if (!loop->label.empty()) {
        ++stats_.loopsSkipped;
        return nullptr;
    }
    
    // Check if already rotated or has complex control flow
    if (isAlreadyRotated(loop) || hasComplexControlFlow(loop->body.get())) {
        ++stats_.loopsSkipped;
        return nullptr;
    }
    
    // Check condition complexity
    if (!isSimpleCondition(loop->condition.get())) {
        ++stats_.loopsSkipped;
        return nullptr;
    }
    
    // Create rotated form: if(cond) { do { body } while(cond) }
    auto rotated = createRotatedWhileLoop(loop);
    if (rotated) {
        ++stats_.whileLoopsRotated;
    }
    
    return rotated;
}

StmtPtr LoopRotationPass::tryRotateForLoop(ForStmt* loop) {
    if (!loop || !loop->body) return nullptr;
    
    // Don't rotate labeled loops
    if (!loop->label.empty()) {
        ++stats_.loopsSkipped;
        return nullptr;
    }
    
    // For loops in Tyl iterate over ranges, which are already well-structured
    // Only rotate if there's a benefit (e.g., the range check is expensive)
    
    // Check for complex control flow
    if (hasComplexControlFlow(loop->body.get())) {
        ++stats_.loopsSkipped;
        return nullptr;
    }
    
    // Create rotated form
    auto rotated = createRotatedForLoop(loop);
    if (rotated) {
        ++stats_.forLoopsRotated;
    }
    
    return rotated;
}

bool LoopRotationPass::shouldRotate(Statement* loop) {
    if (!loop) return false;
    
    // Check if it's a while loop
    if (auto* whileLoop = dynamic_cast<WhileStmt*>(loop)) {
        // Don't rotate infinite loops (while true)
        if (auto* boolLit = dynamic_cast<BoolLiteral*>(whileLoop->condition.get())) {
            if (boolLit->value) return false;  // while(true) - don't rotate
        }
        
        // Don't rotate if condition is too complex
        if (expressionCost(whileLoop->condition.get()) > maxHeaderSize_) {
            return false;
        }
        
        return true;
    }
    
    // For loops - generally don't need rotation in Tyl
    // The range-based iteration is already efficient
    if (auto* forLoop = dynamic_cast<ForStmt*>(loop)) {
        // Only rotate if there's a specific benefit
        // For now, skip for loop rotation as Tyl's for loops are already optimal
        return false;
    }
    
    return false;
}

bool LoopRotationPass::isSimpleCondition(Expression* cond) {
    return expressionCost(cond) <= maxHeaderSize_;
}

int LoopRotationPass::expressionCost(Expression* expr) {
    if (!expr) return 0;
    
    // Literals and identifiers are cheap
    if (dynamic_cast<IntegerLiteral*>(expr)) return 1;
    if (dynamic_cast<FloatLiteral*>(expr)) return 1;
    if (dynamic_cast<BoolLiteral*>(expr)) return 1;
    if (dynamic_cast<StringLiteral*>(expr)) return 1;
    if (dynamic_cast<Identifier*>(expr)) return 1;
    
    // Binary expressions
    if (auto* bin = dynamic_cast<BinaryExpr*>(expr)) {
        return 1 + expressionCost(bin->left.get()) + expressionCost(bin->right.get());
    }
    
    // Unary expressions
    if (auto* un = dynamic_cast<UnaryExpr*>(expr)) {
        return 1 + expressionCost(un->operand.get());
    }
    
    // Function calls are expensive
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        int cost = 5;  // Base cost for call
        for (auto& arg : call->args) {
            cost += expressionCost(arg.get());
        }
        return cost;
    }
    
    // Member access
    if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        return 2 + expressionCost(member->object.get());
    }
    
    // Index access
    if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        return 3 + expressionCost(index->object.get()) + expressionCost(index->index.get());
    }
    
    // Default: moderate cost
    return 5;
}

bool LoopRotationPass::hasComplexControlFlow(Statement* body) {
    if (!body) return false;
    
    // Check for break/continue with labels (targeting outer loops)
    if (auto* breakStmt = dynamic_cast<BreakStmt*>(body)) {
        return !breakStmt->label.empty();
    }
    if (auto* contStmt = dynamic_cast<ContinueStmt*>(body)) {
        return !contStmt->label.empty();
    }
    
    // Recurse into blocks
    if (auto* block = dynamic_cast<Block*>(body)) {
        for (auto& stmt : block->statements) {
            if (hasComplexControlFlow(stmt.get())) return true;
        }
    }
    
    // Check if statements
    if (auto* ifStmt = dynamic_cast<IfStmt*>(body)) {
        if (hasComplexControlFlow(ifStmt->thenBranch.get())) return true;
        if (hasComplexControlFlow(ifStmt->elseBranch.get())) return true;
        for (auto& elif : ifStmt->elifBranches) {
            if (hasComplexControlFlow(elif.second.get())) return true;
        }
    }
    
    // Nested loops are okay - break/continue target them, not outer loop
    
    return false;
}

bool LoopRotationPass::isAlreadyRotated(Statement* loop) {
    // A loop is "rotated" if the exit test is at the bottom
    // In Tyl's AST, while loops always have the test at the top
    // So they're never "already rotated" in the traditional sense
    
    // However, we can check if the loop body ends with a conditional
    // that effectively moves the test to the bottom
    
    if (auto* whileLoop = dynamic_cast<WhileStmt*>(loop)) {
        auto* body = dynamic_cast<Block*>(whileLoop->body.get());
        if (!body || body->statements.empty()) return false;
        
        // Check if last statement is an if that breaks
        auto* lastStmt = body->statements.back().get();
        if (auto* ifStmt = dynamic_cast<IfStmt*>(lastStmt)) {
            // if (!cond) break; at the end is effectively rotated
            if (auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
                if (thenBlock->statements.size() == 1) {
                    if (dynamic_cast<BreakStmt*>(thenBlock->statements[0].get())) {
                        return true;
                    }
                }
            }
        }
    }
    
    return false;
}

ExprPtr LoopRotationPass::cloneCondition(Expression* cond) {
    return cloneExpression(cond);
}

StmtPtr LoopRotationPass::cloneStatement(Statement* stmt) {
    if (!stmt) return nullptr;
    
    if (auto* ret = dynamic_cast<ReturnStmt*>(stmt)) {
        return std::make_unique<ReturnStmt>(cloneExpression(ret->value.get()), ret->location);
    }
    
    if (auto* expr = dynamic_cast<ExprStmt*>(stmt)) {
        return std::make_unique<ExprStmt>(cloneExpression(expr->expr.get()), expr->location);
    }
    
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        auto newBlock = std::make_unique<Block>(block->location);
        for (auto& s : block->statements) {
            if (auto cloned = cloneStatement(s.get())) {
                newBlock->statements.push_back(std::move(cloned));
            }
        }
        return newBlock;
    }
    
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        auto newVar = std::make_unique<VarDecl>(
            varDecl->name, varDecl->typeName,
            cloneExpression(varDecl->initializer.get()),
            varDecl->location);
        newVar->isMutable = varDecl->isMutable;
        newVar->isConst = varDecl->isConst;
        return newVar;
    }
    
    if (auto* breakStmt = dynamic_cast<BreakStmt*>(stmt)) {
        auto newBreak = std::make_unique<BreakStmt>(breakStmt->location);
        newBreak->label = breakStmt->label;
        return newBreak;
    }
    
    if (auto* contStmt = dynamic_cast<ContinueStmt*>(stmt)) {
        auto newCont = std::make_unique<ContinueStmt>(contStmt->location);
        newCont->label = contStmt->label;
        return newCont;
    }
    
    if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        auto newIf = std::make_unique<IfStmt>(
            cloneExpression(ifStmt->condition.get()),
            cloneStatement(ifStmt->thenBranch.get()),
            ifStmt->location);
        if (ifStmt->elseBranch) {
            newIf->elseBranch = cloneStatement(ifStmt->elseBranch.get());
        }
        for (auto& elif : ifStmt->elifBranches) {
            newIf->elifBranches.push_back({
                cloneExpression(elif.first.get()),
                cloneStatement(elif.second.get())
            });
        }
        return newIf;
    }
    
    return nullptr;
}

ExprPtr LoopRotationPass::cloneExpression(Expression* expr) {
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
        auto newCall = std::make_unique<CallExpr>(
            cloneExpression(call->callee.get()),
            call->location);
        for (auto& arg : call->args) {
            newCall->args.push_back(cloneExpression(arg.get()));
        }
        return newCall;
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
    if (auto* range = dynamic_cast<RangeExpr*>(expr)) {
        return std::make_unique<RangeExpr>(
            cloneExpression(range->start.get()),
            cloneExpression(range->end.get()),
            cloneExpression(range->step.get()),
            range->location);
    }
    if (auto* assign = dynamic_cast<AssignExpr*>(expr)) {
        return std::make_unique<AssignExpr>(
            cloneExpression(assign->target.get()),
            assign->op,
            cloneExpression(assign->value.get()),
            assign->location);
    }
    if (auto* walrus = dynamic_cast<WalrusExpr*>(expr)) {
        return std::make_unique<WalrusExpr>(
            walrus->varName,
            cloneExpression(walrus->value.get()),
            walrus->location);
    }
    
    return nullptr;
}

StmtPtr LoopRotationPass::createRotatedWhileLoop(WhileStmt* original) {
    // Transform: while(cond) { body }
    // To:        if(cond) { do { body } while(cond) }
    //
    // In Tyl's AST, we don't have a do-while construct directly,
    // so we transform to:
    //   if (cond) {
    //       while (true) {
    //           body
    //           if (!cond) break
    //       }
    //   }
    
    SourceLocation loc = original->location;
    
    // Create the inner while(true) loop
    auto innerWhile = std::make_unique<WhileStmt>(
        std::make_unique<BoolLiteral>(true, loc),
        nullptr,
        loc);
    
    // Create the body block for inner while
    auto innerBody = std::make_unique<Block>(loc);
    
    // Clone the original body statements
    if (auto* origBody = dynamic_cast<Block*>(original->body.get())) {
        for (auto& stmt : origBody->statements) {
            if (auto cloned = cloneStatement(stmt.get())) {
                innerBody->statements.push_back(std::move(cloned));
            }
        }
    } else if (original->body) {
        if (auto cloned = cloneStatement(original->body.get())) {
            innerBody->statements.push_back(std::move(cloned));
        }
    }
    
    // Add: if (!cond) break
    auto negatedCond = std::make_unique<UnaryExpr>(
        TokenType::NOT,
        cloneCondition(original->condition.get()),
        loc);
    
    auto breakBlock = std::make_unique<Block>(loc);
    breakBlock->statements.push_back(std::make_unique<BreakStmt>(loc));
    
    auto exitCheck = std::make_unique<IfStmt>(
        std::move(negatedCond),
        std::move(breakBlock),
        loc);
    
    innerBody->statements.push_back(std::move(exitCheck));
    
    innerWhile->body = std::move(innerBody);
    
    // Create the outer if(cond) { ... }
    auto outerBody = std::make_unique<Block>(loc);
    outerBody->statements.push_back(std::move(innerWhile));
    
    auto outerIf = std::make_unique<IfStmt>(
        cloneCondition(original->condition.get()),
        std::move(outerBody),
        loc);
    
    return outerIf;
}

StmtPtr LoopRotationPass::createRotatedForLoop(ForStmt* original) {
    // For loops in Tyl are range-based and already efficient
    // Rotation would require extracting the range bounds and creating
    // explicit counter management, which is complex and often not beneficial
    
    // For now, return nullptr to indicate no rotation
    // This can be enhanced later if specific patterns benefit from rotation
    
    return nullptr;
}

} // namespace tyl
