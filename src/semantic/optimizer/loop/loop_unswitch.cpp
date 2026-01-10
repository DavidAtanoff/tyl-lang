// Tyl Compiler - Loop Unswitching Implementation
#include "loop_unswitch.h"
#include <algorithm>

namespace tyl {

void LoopUnswitchPass::run(Program& ast) {
    transformations_ = 0;
    stats_ = LoopUnswitchStats{};
    
    for (auto& stmt : ast.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            processFunction(fn);
        }
    }
    
    transformations_ = stats_.loopsUnswitched + stats_.conditionsHoisted;
}

void LoopUnswitchPass::processFunction(FnDecl* fn) {
    if (!fn->body) return;
    
    if (auto* block = dynamic_cast<Block*>(fn->body.get())) {
        processStatements(block->statements);
    }
}

void LoopUnswitchPass::processStatements(std::vector<StmtPtr>& stmts) {
    for (size_t i = 0; i < stmts.size(); ++i) {
        auto* stmt = stmts[i].get();
        
        if (auto* forLoop = dynamic_cast<ForStmt*>(stmt)) {
            if (tryUnswitchForLoop(stmts, i, forLoop)) {
                --i;
                continue;
            }
            if (forLoop->body) {
                if (auto* block = dynamic_cast<Block*>(forLoop->body.get())) {
                    processStatements(block->statements);
                }
            }
        }
        else if (auto* whileLoop = dynamic_cast<WhileStmt*>(stmt)) {
            if (tryUnswitchWhileLoop(stmts, i, whileLoop)) {
                --i;
                continue;
            }
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

bool LoopUnswitchPass::tryUnswitchForLoop(std::vector<StmtPtr>& stmts, size_t index, 
                                           ForStmt* loop) {
    if (!loop->body) return false;
    if (countStatements(loop->body.get()) > maxLoopSize_) return false;
    
    std::set<std::string> loopVars;
    loopVars.insert(loop->var);
    collectModifiedVars(loop->body.get(), loopVars);
    
    auto invariantConds = findInvariantConditions(loop->body.get(), loopVars);
    if (invariantConds.empty()) return false;
    
    IfStmt* condToUnswitch = invariantConds[0];
    
    auto thenLoop = createThenVersionLoop(loop, condToUnswitch);
    auto elseLoop = createElseVersionLoop(loop, condToUnswitch);
    
    auto outerIf = std::make_unique<IfStmt>(
        cloneExpression(condToUnswitch->condition.get()),
        std::move(thenLoop),
        loop->location);
    outerIf->elseBranch = std::move(elseLoop);
    
    stmts[index] = std::move(outerIf);
    
    ++stats_.loopsUnswitched;
    ++stats_.conditionsHoisted;
    ++stats_.loopsDuplicated;
    
    return true;
}

bool LoopUnswitchPass::tryUnswitchWhileLoop(std::vector<StmtPtr>& stmts, size_t index,
                                             WhileStmt* loop) {
    if (!loop->body) return false;
    if (countStatements(loop->body.get()) > maxLoopSize_) return false;
    
    std::set<std::string> loopVars;
    collectModifiedVars(loop->body.get(), loopVars);
    collectUsedVars(loop->condition.get(), loopVars);
    
    auto invariantConds = findInvariantConditions(loop->body.get(), loopVars);
    if (invariantConds.empty()) return false;
    
    IfStmt* condToUnswitch = invariantConds[0];
    
    auto thenLoop = createThenVersionLoop(loop, condToUnswitch);
    auto elseLoop = createElseVersionLoop(loop, condToUnswitch);
    
    auto outerIf = std::make_unique<IfStmt>(
        cloneExpression(condToUnswitch->condition.get()),
        std::move(thenLoop),
        loop->location);
    outerIf->elseBranch = std::move(elseLoop);
    
    stmts[index] = std::move(outerIf);
    
    ++stats_.loopsUnswitched;
    ++stats_.conditionsHoisted;
    ++stats_.loopsDuplicated;
    
    return true;
}

std::vector<IfStmt*> LoopUnswitchPass::findInvariantConditions(
    Statement* body, const std::set<std::string>& loopVars) {
    
    std::vector<IfStmt*> result;
    auto* block = dynamic_cast<Block*>(body);
    if (!block) return result;
    
    for (auto& stmt : block->statements) {
        if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
            if (isLoopInvariant(ifStmt->condition.get(), loopVars)) {
                if (ifStmt->thenBranch && ifStmt->elseBranch) {
                    result.push_back(ifStmt);
                }
            }
        }
    }
    return result;
}

bool LoopUnswitchPass::isLoopInvariant(Expression* expr, 
                                        const std::set<std::string>& loopVars) {
    if (!expr) return true;
    
    if (auto* id = dynamic_cast<Identifier*>(expr)) {
        return loopVars.find(id->name) == loopVars.end();
    }
    if (dynamic_cast<IntegerLiteral*>(expr) ||
        dynamic_cast<FloatLiteral*>(expr) ||
        dynamic_cast<BoolLiteral*>(expr) ||
        dynamic_cast<StringLiteral*>(expr)) {
        return true;
    }
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        return isLoopInvariant(binary->left.get(), loopVars) &&
               isLoopInvariant(binary->right.get(), loopVars);
    }
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        return isLoopInvariant(unary->operand.get(), loopVars);
    }
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        return false;
    }
    if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        return isLoopInvariant(member->object.get(), loopVars);
    }
    if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        return isLoopInvariant(index->object.get(), loopVars) &&
               isLoopInvariant(index->index.get(), loopVars);
    }
    if (auto* walrus = dynamic_cast<WalrusExpr*>(expr)) {
        // Walrus creates a variable, so check if the value is invariant
        // The variable itself becomes a loop variable
        return false;  // Walrus modifies state, not invariant
    }
    return false;
}

void LoopUnswitchPass::collectModifiedVars(Statement* stmt, std::set<std::string>& vars) {
    if (!stmt) return;
    
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        vars.insert(varDecl->name);
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        if (auto* assign = dynamic_cast<AssignExpr*>(exprStmt->expr.get())) {
            if (auto* target = dynamic_cast<Identifier*>(assign->target.get())) {
                vars.insert(target->name);
            }
        }
    }
    else if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            collectModifiedVars(s.get(), vars);
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        collectModifiedVars(ifStmt->thenBranch.get(), vars);
        collectModifiedVars(ifStmt->elseBranch.get(), vars);
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        collectModifiedVars(whileStmt->body.get(), vars);
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
        vars.insert(forStmt->var);
        collectModifiedVars(forStmt->body.get(), vars);
    }
}

void LoopUnswitchPass::collectUsedVars(Expression* expr, std::set<std::string>& vars) {
    if (!expr) return;
    
    if (auto* id = dynamic_cast<Identifier*>(expr)) {
        vars.insert(id->name);
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        collectUsedVars(binary->left.get(), vars);
        collectUsedVars(binary->right.get(), vars);
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        collectUsedVars(unary->operand.get(), vars);
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        for (auto& arg : call->args) {
            collectUsedVars(arg.get(), vars);
        }
    }
    else if (auto* walrus = dynamic_cast<WalrusExpr*>(expr)) {
        // Walrus creates a variable and uses the value expression
        vars.insert(walrus->varName);
        collectUsedVars(walrus->value.get(), vars);
    }
}

size_t LoopUnswitchPass::countStatements(Statement* stmt) {
    if (!stmt) return 0;
    
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        size_t count = 0;
        for (auto& s : block->statements) {
            count += countStatements(s.get());
        }
        return count;
    }
    if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        return 1 + countStatements(ifStmt->thenBranch.get()) +
               countStatements(ifStmt->elseBranch.get());
    }
    if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        return 1 + countStatements(whileStmt->body.get());
    }
    if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
        return 1 + countStatements(forStmt->body.get());
    }
    return 1;
}

StmtPtr LoopUnswitchPass::cloneStatement(Statement* stmt) {
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

ExprPtr LoopUnswitchPass::cloneExpression(Expression* expr) {
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
        return std::make_unique<RangeExpr>(
            cloneExpression(range->start.get()),
            cloneExpression(range->end.get()),
            range->step ? cloneExpression(range->step.get()) : nullptr,
            range->location);
    }
    if (auto* walrus = dynamic_cast<WalrusExpr*>(expr)) {
        return std::make_unique<WalrusExpr>(
            walrus->varName,
            cloneExpression(walrus->value.get()),
            walrus->location);
    }
    return nullptr;
}

StmtPtr LoopUnswitchPass::createThenVersionLoop(ForStmt* loop, IfStmt* cond) {
    // Clone the loop body - this gives us a fresh copy to modify
    auto clonedBody = cloneStatement(loop->body.get());
    
    if (auto* block = dynamic_cast<Block*>(clonedBody.get())) {
        for (size_t i = 0; i < block->statements.size(); ++i) {
            if (auto* ifStmt = dynamic_cast<IfStmt*>(block->statements[i].get())) {
                if (!conditionsMatch(ifStmt->condition.get(), cond->condition.get())) {
                    continue;
                }
                
                // Found the matching if statement - replace it with its then branch contents
                if (ifStmt->thenBranch) {
                    if (auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
                        // Clone each statement from the then branch
                        std::vector<StmtPtr> stmtsToInsert;
                        for (auto& s : thenBlock->statements) {
                            stmtsToInsert.push_back(cloneStatement(s.get()));
                        }
                        // Remove the if statement
                        block->statements.erase(block->statements.begin() + i);
                        // Insert the cloned statements
                        for (size_t j = 0; j < stmtsToInsert.size(); ++j) {
                            block->statements.insert(
                                block->statements.begin() + i + j,
                                std::move(stmtsToInsert[j]));
                        }
                    } else {
                        // Then branch is a single statement, clone it
                        block->statements[i] = cloneStatement(ifStmt->thenBranch.get());
                    }
                } else {
                    // No then branch - just remove the if
                    block->statements.erase(block->statements.begin() + i);
                }
                break;
            }
        }
    }
    
    return std::make_unique<ForStmt>(
        loop->var,
        cloneExpression(loop->iterable.get()),
        std::move(clonedBody),
        loop->location);
}

StmtPtr LoopUnswitchPass::createThenVersionLoop(WhileStmt* loop, IfStmt* cond) {
    auto clonedBody = cloneStatement(loop->body.get());
    
    if (auto* block = dynamic_cast<Block*>(clonedBody.get())) {
        for (size_t i = 0; i < block->statements.size(); ++i) {
            if (auto* ifStmt = dynamic_cast<IfStmt*>(block->statements[i].get())) {
                if (!conditionsMatch(ifStmt->condition.get(), cond->condition.get())) {
                    continue;
                }
                
                if (ifStmt->thenBranch) {
                    if (auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
                        std::vector<StmtPtr> stmtsToInsert;
                        for (auto& s : thenBlock->statements) {
                            stmtsToInsert.push_back(cloneStatement(s.get()));
                        }
                        block->statements.erase(block->statements.begin() + i);
                        for (size_t j = 0; j < stmtsToInsert.size(); ++j) {
                            block->statements.insert(
                                block->statements.begin() + i + j,
                                std::move(stmtsToInsert[j]));
                        }
                    } else {
                        block->statements[i] = cloneStatement(ifStmt->thenBranch.get());
                    }
                } else {
                    block->statements.erase(block->statements.begin() + i);
                }
                break;
            }
        }
    }
    
    return std::make_unique<WhileStmt>(
        cloneExpression(loop->condition.get()),
        std::move(clonedBody),
        loop->location);
}

StmtPtr LoopUnswitchPass::createElseVersionLoop(ForStmt* loop, IfStmt* cond) {
    auto clonedBody = cloneStatement(loop->body.get());
    
    if (auto* block = dynamic_cast<Block*>(clonedBody.get())) {
        for (size_t i = 0; i < block->statements.size(); ++i) {
            if (auto* ifStmt = dynamic_cast<IfStmt*>(block->statements[i].get())) {
                if (!conditionsMatch(ifStmt->condition.get(), cond->condition.get())) {
                    continue;
                }
                
                if (ifStmt->elseBranch) {
                    if (auto* elseBlock = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
                        std::vector<StmtPtr> stmtsToInsert;
                        for (auto& s : elseBlock->statements) {
                            stmtsToInsert.push_back(cloneStatement(s.get()));
                        }
                        block->statements.erase(block->statements.begin() + i);
                        for (size_t j = 0; j < stmtsToInsert.size(); ++j) {
                            block->statements.insert(
                                block->statements.begin() + i + j,
                                std::move(stmtsToInsert[j]));
                        }
                    } else {
                        block->statements[i] = cloneStatement(ifStmt->elseBranch.get());
                    }
                } else {
                    block->statements.erase(block->statements.begin() + i);
                }
                break;
            }
        }
    }
    
    return std::make_unique<ForStmt>(
        loop->var,
        cloneExpression(loop->iterable.get()),
        std::move(clonedBody),
        loop->location);
}

StmtPtr LoopUnswitchPass::createElseVersionLoop(WhileStmt* loop, IfStmt* cond) {
    auto clonedBody = cloneStatement(loop->body.get());
    
    if (auto* block = dynamic_cast<Block*>(clonedBody.get())) {
        for (size_t i = 0; i < block->statements.size(); ++i) {
            if (auto* ifStmt = dynamic_cast<IfStmt*>(block->statements[i].get())) {
                if (!conditionsMatch(ifStmt->condition.get(), cond->condition.get())) {
                    continue;
                }
                
                if (ifStmt->elseBranch) {
                    if (auto* elseBlock = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
                        std::vector<StmtPtr> stmtsToInsert;
                        for (auto& s : elseBlock->statements) {
                            stmtsToInsert.push_back(std::move(s));
                        }
                        block->statements.erase(block->statements.begin() + i);
                        for (size_t j = 0; j < stmtsToInsert.size(); ++j) {
                            block->statements.insert(
                                block->statements.begin() + i + j,
                                std::move(stmtsToInsert[j]));
                        }
                    } else {
                        block->statements[i] = std::move(ifStmt->elseBranch);
                    }
                } else {
                    block->statements.erase(block->statements.begin() + i);
                }
                break;
            }
        }
    }
    
    return std::make_unique<WhileStmt>(
        cloneExpression(loop->condition.get()),
        std::move(clonedBody),
        loop->location);
}

bool LoopUnswitchPass::conditionsMatch(Expression* a, Expression* b) {
    if (!a && !b) return true;
    if (!a || !b) return false;
    
    if (typeid(*a) != typeid(*b)) return false;
    
    if (auto* idA = dynamic_cast<Identifier*>(a)) {
        auto* idB = dynamic_cast<Identifier*>(b);
        return idA->name == idB->name;
    }
    if (auto* litA = dynamic_cast<IntegerLiteral*>(a)) {
        auto* litB = dynamic_cast<IntegerLiteral*>(b);
        return litA->value == litB->value;
    }
    if (auto* litA = dynamic_cast<FloatLiteral*>(a)) {
        auto* litB = dynamic_cast<FloatLiteral*>(b);
        return litA->value == litB->value;
    }
    if (auto* litA = dynamic_cast<BoolLiteral*>(a)) {
        auto* litB = dynamic_cast<BoolLiteral*>(b);
        return litA->value == litB->value;
    }
    if (auto* litA = dynamic_cast<StringLiteral*>(a)) {
        auto* litB = dynamic_cast<StringLiteral*>(b);
        return litA->value == litB->value;
    }
    if (auto* binA = dynamic_cast<BinaryExpr*>(a)) {
        auto* binB = dynamic_cast<BinaryExpr*>(b);
        return binA->op == binB->op &&
               conditionsMatch(binA->left.get(), binB->left.get()) &&
               conditionsMatch(binA->right.get(), binB->right.get());
    }
    if (auto* unA = dynamic_cast<UnaryExpr*>(a)) {
        auto* unB = dynamic_cast<UnaryExpr*>(b);
        return unA->op == unB->op &&
               conditionsMatch(unA->operand.get(), unB->operand.get());
    }
    if (auto* memberA = dynamic_cast<MemberExpr*>(a)) {
        auto* memberB = dynamic_cast<MemberExpr*>(b);
        return memberA->member == memberB->member &&
               conditionsMatch(memberA->object.get(), memberB->object.get());
    }
    if (auto* indexA = dynamic_cast<IndexExpr*>(a)) {
        auto* indexB = dynamic_cast<IndexExpr*>(b);
        return conditionsMatch(indexA->object.get(), indexB->object.get()) &&
               conditionsMatch(indexA->index.get(), indexB->index.get());
    }
    return false;
}

void LoopUnswitchPass::replaceIfWithBranch(Block* block, IfStmt* ifStmt, bool useThenBranch) {
    for (auto& stmt : block->statements) {
        if (stmt.get() == ifStmt) {
            if (useThenBranch) {
                stmt = cloneStatement(ifStmt->thenBranch.get());
            } else {
                stmt = cloneStatement(ifStmt->elseBranch.get());
            }
            return;
        }
    }
}

} // namespace tyl
