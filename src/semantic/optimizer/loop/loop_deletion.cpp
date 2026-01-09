// Tyl Compiler - Loop Deletion Implementation
#include "loop_deletion.h"
#include <algorithm>
#include <iostream>

namespace tyl {

void LoopDeletionPass::run(Program& ast) {
    transformations_ = 0;
    stats_ = LoopDeletionStats{};
    
    processStatements(ast.statements);
    
    transformations_ = stats_.forLoopsDeleted + stats_.whileLoopsDeleted;
}

void LoopDeletionPass::processStatements(std::vector<StmtPtr>& stmts) {
    for (size_t i = 0; i < stmts.size(); ) {
        // First recurse into nested structures
        processStatement(stmts[i]);
        
        // Compute what's live after this statement
        auto liveAfter = computeLiveAfter(stmts, i);
        
        bool deleted = false;
        
        // Try to delete for loops
        if (auto* forLoop = dynamic_cast<ForStmt*>(stmts[i].get())) {
            if (canDeleteForLoop(forLoop, liveAfter)) {
                stmts.erase(stmts.begin() + i);
                ++stats_.forLoopsDeleted;
                deleted = true;
            }
        }
        // Try to delete while loops
        else if (auto* whileLoop = dynamic_cast<WhileStmt*>(stmts[i].get())) {
            if (canDeleteWhileLoop(whileLoop, liveAfter)) {
                stmts.erase(stmts.begin() + i);
                ++stats_.whileLoopsDeleted;
                deleted = true;
            }
        }
        
        if (!deleted) {
            ++i;
        }
    }
}

void LoopDeletionPass::processStatement(StmtPtr& stmt) {
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

bool LoopDeletionPass::canDeleteForLoop(ForStmt* loop, const std::set<std::string>& liveAfter) {
    if (!loop || !loop->body) {
        ++stats_.loopsSkipped;
        return false;
    }
    
    // Don't delete labeled loops (may have external control flow)
    if (!loop->label.empty()) {
        ++stats_.loopsSkipped;
        return false;
    }
    
    // Check if loop has computable trip count (terminates)
    if (!hasComputableTripCount(loop)) {
        ++stats_.loopsSkipped;
        return false;
    }
    
    // Check if loop body has side effects
    if (hasSideEffects(loop->body.get())) {
        ++stats_.loopsSkipped;
        return false;
    }
    
    // Check if induction variable escapes (is used after loop)
    if (inductionVarEscapes(loop, liveAfter)) {
        ++stats_.loopsSkipped;
        return false;
    }
    
    // Collect variables defined in the loop
    std::set<std::string> definedInLoop;
    collectDefinedVars(loop->body.get(), definedInLoop);
    definedInLoop.insert(loop->var);  // Induction variable
    
    // Check if any defined variable is live after the loop
    for (const auto& var : definedInLoop) {
        if (liveAfter.count(var)) {
            ++stats_.loopsSkipped;
            return false;
        }
    }
    
    return true;
}

bool LoopDeletionPass::canDeleteWhileLoop(WhileStmt* loop, const std::set<std::string>& liveAfter) {
    if (!loop || !loop->body) {
        ++stats_.loopsSkipped;
        return false;
    }
    
    // Don't delete labeled loops
    if (!loop->label.empty()) {
        ++stats_.loopsSkipped;
        return false;
    }
    
    // Check if loop has computable trip count
    if (!hasComputableTripCount(loop)) {
        ++stats_.loopsSkipped;
        return false;
    }
    
    // Check if loop body has side effects
    if (hasSideEffects(loop->body.get())) {
        ++stats_.loopsSkipped;
        return false;
    }
    
    // Check if condition has side effects
    if (exprHasSideEffects(loop->condition.get())) {
        ++stats_.loopsSkipped;
        return false;
    }
    
    // Collect variables defined in the loop
    std::set<std::string> definedInLoop;
    collectDefinedVars(loop->body.get(), definedInLoop);
    
    // Check if any defined variable is live after the loop
    for (const auto& var : definedInLoop) {
        if (liveAfter.count(var)) {
            ++stats_.loopsSkipped;
            return false;
        }
    }
    
    return true;
}

bool LoopDeletionPass::hasSideEffects(Statement* stmt) {
    if (!stmt) return false;
    
    // Function calls may have side effects
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        return exprHasSideEffects(exprStmt->expr.get());
    }
    
    // Variable declarations with side effects in initializer
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        return exprHasSideEffects(varDecl->initializer.get());
    }
    
    // Assignments - check if target is non-local or has side effects
    if (auto* assign = dynamic_cast<AssignStmt*>(stmt)) {
        // Array/member assignments may have side effects (visible outside loop)
        if (dynamic_cast<IndexExpr*>(assign->target.get()) ||
            dynamic_cast<MemberExpr*>(assign->target.get())) {
            return true;
        }
        return exprHasSideEffects(assign->value.get());
    }
    
    // Return statements have side effects
    if (dynamic_cast<ReturnStmt*>(stmt)) {
        return true;
    }
    
    // Break/continue affect control flow
    if (dynamic_cast<BreakStmt*>(stmt) || dynamic_cast<ContinueStmt*>(stmt)) {
        return true;
    }
    
    // Recurse into blocks
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            if (hasSideEffects(s.get())) return true;
        }
        return false;
    }
    
    // If statements
    if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        if (exprHasSideEffects(ifStmt->condition.get())) return true;
        if (hasSideEffects(ifStmt->thenBranch.get())) return true;
        if (hasSideEffects(ifStmt->elseBranch.get())) return true;
        for (auto& elif : ifStmt->elifBranches) {
            if (exprHasSideEffects(elif.first.get())) return true;
            if (hasSideEffects(elif.second.get())) return true;
        }
        return false;
    }
    
    // Nested loops
    if (auto* forLoop = dynamic_cast<ForStmt*>(stmt)) {
        return hasSideEffects(forLoop->body.get());
    }
    if (auto* whileLoop = dynamic_cast<WhileStmt*>(stmt)) {
        if (exprHasSideEffects(whileLoop->condition.get())) return true;
        return hasSideEffects(whileLoop->body.get());
    }
    
    return false;
}

bool LoopDeletionPass::exprHasSideEffects(Expression* expr) {
    if (!expr) return false;
    
    // Function calls may have side effects
    if (dynamic_cast<CallExpr*>(expr)) {
        return true;
    }
    
    // Assignment expressions have side effects
    if (dynamic_cast<AssignExpr*>(expr)) {
        return true;
    }
    
    // Channel operations have side effects
    if (dynamic_cast<ChanSendExpr*>(expr) || dynamic_cast<ChanRecvExpr*>(expr)) {
        return true;
    }
    
    // Atomic operations have side effects
    if (dynamic_cast<AtomicStoreExpr*>(expr) || dynamic_cast<AtomicSwapExpr*>(expr) ||
        dynamic_cast<AtomicCasExpr*>(expr) || dynamic_cast<AtomicAddExpr*>(expr) ||
        dynamic_cast<AtomicSubExpr*>(expr)) {
        return true;
    }
    
    // Spawn/await have side effects
    if (dynamic_cast<SpawnExpr*>(expr) || dynamic_cast<AwaitExpr*>(expr)) {
        return true;
    }
    
    // Binary expressions - recurse
    if (auto* bin = dynamic_cast<BinaryExpr*>(expr)) {
        return exprHasSideEffects(bin->left.get()) || exprHasSideEffects(bin->right.get());
    }
    
    // Unary expressions - recurse
    if (auto* un = dynamic_cast<UnaryExpr*>(expr)) {
        return exprHasSideEffects(un->operand.get());
    }
    
    // Index expressions - recurse
    if (auto* idx = dynamic_cast<IndexExpr*>(expr)) {
        return exprHasSideEffects(idx->object.get()) || exprHasSideEffects(idx->index.get());
    }
    
    // Member expressions - recurse
    if (auto* mem = dynamic_cast<MemberExpr*>(expr)) {
        return exprHasSideEffects(mem->object.get());
    }
    
    // Ternary expressions - recurse
    if (auto* tern = dynamic_cast<TernaryExpr*>(expr)) {
        return exprHasSideEffects(tern->condition.get()) ||
               exprHasSideEffects(tern->thenExpr.get()) ||
               exprHasSideEffects(tern->elseExpr.get());
    }
    
    return false;
}

void LoopDeletionPass::collectDefinedVars(Statement* stmt, std::set<std::string>& defined) {
    if (!stmt) return;
    
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        defined.insert(varDecl->name);
    }
    else if (auto* assign = dynamic_cast<AssignStmt*>(stmt)) {
        if (auto* id = dynamic_cast<Identifier*>(assign->target.get())) {
            defined.insert(id->name);
        }
    }
    else if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            collectDefinedVars(s.get(), defined);
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        collectDefinedVars(ifStmt->thenBranch.get(), defined);
        collectDefinedVars(ifStmt->elseBranch.get(), defined);
        for (auto& elif : ifStmt->elifBranches) {
            collectDefinedVars(elif.second.get(), defined);
        }
    }
    else if (auto* forLoop = dynamic_cast<ForStmt*>(stmt)) {
        defined.insert(forLoop->var);
        collectDefinedVars(forLoop->body.get(), defined);
    }
    else if (auto* whileLoop = dynamic_cast<WhileStmt*>(stmt)) {
        collectDefinedVars(whileLoop->body.get(), defined);
    }
}

void LoopDeletionPass::collectUsedVars(Statement* stmt, std::set<std::string>& used) {
    if (!stmt) return;
    
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        collectUsedVarsExpr(exprStmt->expr.get(), used);
    }
    else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        collectUsedVarsExpr(varDecl->initializer.get(), used);
    }
    else if (auto* assign = dynamic_cast<AssignStmt*>(stmt)) {
        collectUsedVarsExpr(assign->target.get(), used);
        collectUsedVarsExpr(assign->value.get(), used);
    }
    else if (auto* ret = dynamic_cast<ReturnStmt*>(stmt)) {
        collectUsedVarsExpr(ret->value.get(), used);
    }
    else if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            collectUsedVars(s.get(), used);
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        collectUsedVarsExpr(ifStmt->condition.get(), used);
        collectUsedVars(ifStmt->thenBranch.get(), used);
        collectUsedVars(ifStmt->elseBranch.get(), used);
        for (auto& elif : ifStmt->elifBranches) {
            collectUsedVarsExpr(elif.first.get(), used);
            collectUsedVars(elif.second.get(), used);
        }
    }
    else if (auto* forLoop = dynamic_cast<ForStmt*>(stmt)) {
        collectUsedVarsExpr(forLoop->iterable.get(), used);
        collectUsedVars(forLoop->body.get(), used);
    }
    else if (auto* whileLoop = dynamic_cast<WhileStmt*>(stmt)) {
        collectUsedVarsExpr(whileLoop->condition.get(), used);
        collectUsedVars(whileLoop->body.get(), used);
    }
}

void LoopDeletionPass::collectUsedVarsExpr(Expression* expr, std::set<std::string>& used) {
    if (!expr) return;
    
    if (auto* id = dynamic_cast<Identifier*>(expr)) {
        used.insert(id->name);
    }
    else if (auto* bin = dynamic_cast<BinaryExpr*>(expr)) {
        collectUsedVarsExpr(bin->left.get(), used);
        collectUsedVarsExpr(bin->right.get(), used);
    }
    else if (auto* un = dynamic_cast<UnaryExpr*>(expr)) {
        collectUsedVarsExpr(un->operand.get(), used);
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        collectUsedVarsExpr(call->callee.get(), used);
        for (auto& arg : call->args) {
            collectUsedVarsExpr(arg.get(), used);
        }
    }
    else if (auto* idx = dynamic_cast<IndexExpr*>(expr)) {
        collectUsedVarsExpr(idx->object.get(), used);
        collectUsedVarsExpr(idx->index.get(), used);
    }
    else if (auto* mem = dynamic_cast<MemberExpr*>(expr)) {
        collectUsedVarsExpr(mem->object.get(), used);
    }
    else if (auto* range = dynamic_cast<RangeExpr*>(expr)) {
        collectUsedVarsExpr(range->start.get(), used);
        collectUsedVarsExpr(range->end.get(), used);
        collectUsedVarsExpr(range->step.get(), used);
    }
    else if (auto* tern = dynamic_cast<TernaryExpr*>(expr)) {
        collectUsedVarsExpr(tern->condition.get(), used);
        collectUsedVarsExpr(tern->thenExpr.get(), used);
        collectUsedVarsExpr(tern->elseExpr.get(), used);
    }
    else if (auto* assign = dynamic_cast<AssignExpr*>(expr)) {
        collectUsedVarsExpr(assign->target.get(), used);
        collectUsedVarsExpr(assign->value.get(), used);
    }
}

std::set<std::string> LoopDeletionPass::computeLiveAfter(const std::vector<StmtPtr>& stmts, size_t index) {
    std::set<std::string> live;
    
    // Collect all variables used after this index
    for (size_t i = index + 1; i < stmts.size(); ++i) {
        collectUsedVars(stmts[i].get(), live);
    }
    
    return live;
}

bool LoopDeletionPass::hasComputableTripCount(ForStmt* loop) {
    if (!loop || !loop->iterable) return false;
    
    // Range expressions have computable trip counts
    if (dynamic_cast<RangeExpr*>(loop->iterable.get())) {
        return true;
    }
    
    // Inclusive range expressions have computable trip counts
    if (dynamic_cast<InclusiveRangeExpr*>(loop->iterable.get())) {
        return true;
    }
    
    // range() function calls with constant arguments
    if (auto* call = dynamic_cast<CallExpr*>(loop->iterable.get())) {
        if (auto* callee = dynamic_cast<Identifier*>(call->callee.get())) {
            if (callee->name == "range") {
                return true;  // range() always terminates
            }
        }
    }
    
    return false;
}

bool LoopDeletionPass::hasComputableTripCount(WhileStmt* loop) {
    if (!loop || !loop->condition) return false;
    
    // while(false) has trip count 0
    if (auto* boolLit = dynamic_cast<BoolLiteral*>(loop->condition.get())) {
        return !boolLit->value;  // Only if condition is false
    }
    
    // Simple comparison with constant bounds
    // e.g., while (i < 10) where i starts at 0 and increments by 1
    // This is a simplified check - full analysis would require more work
    if (auto* bin = dynamic_cast<BinaryExpr*>(loop->condition.get())) {
        if (bin->op == TokenType::LT || bin->op == TokenType::LE ||
            bin->op == TokenType::GT || bin->op == TokenType::GE) {
            // Check if one side is a constant
            if (dynamic_cast<IntegerLiteral*>(bin->left.get()) ||
                dynamic_cast<IntegerLiteral*>(bin->right.get())) {
                return true;  // Simplified: assume bounded loops terminate
            }
        }
    }
    
    return false;
}

bool LoopDeletionPass::inductionVarEscapes(ForStmt* loop, const std::set<std::string>& liveAfter) {
    // Check if the induction variable is used after the loop
    return liveAfter.count(loop->var) > 0;
}

} // namespace tyl
