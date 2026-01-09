// Tyl Compiler - Loop Simplify Implementation
// Canonicalizes loop structure for better optimization
#include "loop_simplify.h"
#include <algorithm>

namespace tyl {

void LoopSimplifyPass::run(Program& ast) {
    transformations_ = 0;
    stats_ = LoopSimplifyStats{};
    uniqueCounter_ = 0;
    
    // Process each function
    for (auto& stmt : ast.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            processFunction(fn);
        }
        else if (auto* module = dynamic_cast<ModuleDecl*>(stmt.get())) {
            for (auto& modStmt : module->body) {
                if (auto* modFn = dynamic_cast<FnDecl*>(modStmt.get())) {
                    processFunction(modFn);
                }
            }
        }
    }
}

void LoopSimplifyPass::processFunction(FnDecl* fn) {
    if (!fn || !fn->body) return;
    
    auto* body = dynamic_cast<Block*>(fn->body.get());
    if (!body) return;
    
    processStatements(body->statements);
}

void LoopSimplifyPass::processStatements(std::vector<StmtPtr>& stmts) {
    for (size_t i = 0; i < stmts.size(); ++i) {
        processStatement(stmts[i], stmts, i);
    }
}

void LoopSimplifyPass::processStatement(StmtPtr& stmt, std::vector<StmtPtr>& stmts, size_t index) {
    if (!stmt) return;
    
    // Process while loops
    if (auto* whileLoop = dynamic_cast<WhileStmt*>(stmt.get())) {
        // First process nested statements
        if (auto* body = dynamic_cast<Block*>(whileLoop->body.get())) {
            processStatements(body->statements);
        }
        
        // Then canonicalize this loop
        if (canonicalizeWhileLoop(whileLoop, stmts, index)) {
            stats_.loopsCanonalized++;
            transformations_++;
        }
    }
    // Process for loops
    else if (auto* forLoop = dynamic_cast<ForStmt*>(stmt.get())) {
        // First process nested statements
        if (auto* body = dynamic_cast<Block*>(forLoop->body.get())) {
            processStatements(body->statements);
        }
        
        // Then canonicalize this loop
        if (canonicalizeForLoop(forLoop, stmts, index)) {
            stats_.loopsCanonalized++;
            transformations_++;
        }
    }
    // Process if statements
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
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
    // Process blocks
    else if (auto* block = dynamic_cast<Block*>(stmt.get())) {
        processStatements(block->statements);
    }
}

bool LoopSimplifyPass::canonicalizeWhileLoop(WhileStmt* loop, std::vector<StmtPtr>& stmts, size_t index) {
    bool changed = false;
    
    // Check if loop needs a preheader
    if (needsPreheader(loop, stmts, index)) {
        insertPreheader(stmts, index);
        stats_.preheadersInserted++;
        changed = true;
    }
    
    // Check for multiple backedges (continue statements)
    if (hasMultipleBackedges(loop->body.get())) {
        simplifyBackedges(loop);
        stats_.latchesSimplified++;
        changed = true;
    }
    
    return changed;
}

bool LoopSimplifyPass::canonicalizeForLoop(ForStmt* loop, std::vector<StmtPtr>& stmts, size_t index) {
    bool changed = false;
    
    // Check if loop needs a preheader
    if (needsPreheader(loop, stmts, index)) {
        insertPreheader(stmts, index);
        stats_.preheadersInserted++;
        changed = true;
    }
    
    // Check for multiple backedges
    if (hasMultipleBackedges(loop->body.get())) {
        simplifyBackedges(loop);
        stats_.latchesSimplified++;
        changed = true;
    }
    
    return changed;
}

bool LoopSimplifyPass::needsPreheader(Statement* loop, std::vector<StmtPtr>& stmts, size_t index) {
    // A preheader is needed if:
    // 1. There are loop-invariant computations that could be hoisted
    // 2. The loop is the target of multiple branches (not applicable in AST form)
    
    // In AST form, we mainly check if there are invariant initializations
    // that should be hoisted before the loop
    
    if (index == 0) return false;  // Already at start, no preheader needed
    
    // Check if previous statement is already a "preheader" (variable declarations
    // that are only used in the loop)
    auto* prevStmt = stmts[index - 1].get();
    if (dynamic_cast<VarDecl*>(prevStmt)) {
        return false;  // Already has declarations before loop
    }
    
    // Check if loop has invariant expressions that could benefit from preheader
    std::set<std::string> loopVars;
    if (auto* whileLoop = dynamic_cast<WhileStmt*>(loop)) {
        loopVars = collectModifiedVars(whileLoop->body.get());
    } else if (auto* forLoop = dynamic_cast<ForStmt*>(loop)) {
        loopVars.insert(forLoop->var);
        auto bodyVars = collectModifiedVars(forLoop->body.get());
        loopVars.insert(bodyVars.begin(), bodyVars.end());
    }
    
    // For now, we don't insert preheaders in AST form
    // This is more relevant for CFG/IR form
    return false;
}

void LoopSimplifyPass::insertPreheader(std::vector<StmtPtr>& stmts, size_t loopIndex) {
    // Create an empty block as preheader
    auto preheader = std::make_unique<Block>(SourceLocation{});
    
    // Insert before the loop
    stmts.insert(stmts.begin() + loopIndex, std::move(preheader));
}

bool LoopSimplifyPass::hasMultipleBackedges(Statement* body) {
    // Count continue statements - each is a potential backedge
    return countContinues(body) > 1;
}

void LoopSimplifyPass::simplifyBackedges(Statement* loop) {
    // In AST form, multiple continues are fine
    // This transformation is more relevant for CFG form
    // where we'd create a single latch block
    
    // For AST, we could potentially restructure nested continues
    // but this is complex and rarely beneficial
}

bool LoopSimplifyPass::hasDedicatedExits(Statement* loop) {
    // Check if all break statements go to a single exit point
    // In AST form, breaks always exit to after the loop
    return true;  // AST form always has dedicated exits
}

void LoopSimplifyPass::createDedicatedExits(Statement* loop) {
    // Not needed in AST form
}

int LoopSimplifyPass::countBreaks(Statement* body) {
    if (!body) return 0;
    
    int count = 0;
    
    if (dynamic_cast<BreakStmt*>(body)) {
        return 1;
    }
    
    if (auto* block = dynamic_cast<Block*>(body)) {
        for (auto& stmt : block->statements) {
            count += countBreaks(stmt.get());
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(body)) {
        if (ifStmt->thenBranch) count += countBreaks(ifStmt->thenBranch.get());
        for (auto& elif : ifStmt->elifBranches) {
            if (elif.second) count += countBreaks(elif.second.get());
        }
        if (ifStmt->elseBranch) count += countBreaks(ifStmt->elseBranch.get());
    }
    // Don't count breaks in nested loops - they exit the inner loop
    
    return count;
}

int LoopSimplifyPass::countContinues(Statement* body) {
    if (!body) return 0;
    
    int count = 0;
    
    if (dynamic_cast<ContinueStmt*>(body)) {
        return 1;
    }
    
    if (auto* block = dynamic_cast<Block*>(body)) {
        for (auto& stmt : block->statements) {
            count += countContinues(stmt.get());
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(body)) {
        if (ifStmt->thenBranch) count += countContinues(ifStmt->thenBranch.get());
        for (auto& elif : ifStmt->elifBranches) {
            if (elif.second) count += countContinues(elif.second.get());
        }
        if (ifStmt->elseBranch) count += countContinues(ifStmt->elseBranch.get());
    }
    // Don't count continues in nested loops
    
    return count;
}

bool LoopSimplifyPass::hasLoopExitingFlow(Statement* stmt) {
    if (!stmt) return false;
    
    if (dynamic_cast<BreakStmt*>(stmt) || dynamic_cast<ReturnStmt*>(stmt)) {
        return true;
    }
    
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            if (hasLoopExitingFlow(s.get())) return true;
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        if (hasLoopExitingFlow(ifStmt->thenBranch.get())) return true;
        for (auto& elif : ifStmt->elifBranches) {
            if (hasLoopExitingFlow(elif.second.get())) return true;
        }
        if (hasLoopExitingFlow(ifStmt->elseBranch.get())) return true;
    }
    
    return false;
}

bool LoopSimplifyPass::isLoopInvariant(Expression* expr, const std::set<std::string>& loopVars) {
    if (!expr) return true;
    
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        return loopVars.find(ident->name) == loopVars.end();
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
    
    // Function calls are not loop invariant (may have side effects)
    if (dynamic_cast<CallExpr*>(expr)) {
        return false;
    }
    
    return false;
}

std::set<std::string> LoopSimplifyPass::collectModifiedVars(Statement* body) {
    std::set<std::string> vars;
    
    if (!body) return vars;
    
    if (auto* assign = dynamic_cast<AssignStmt*>(body)) {
        if (auto* ident = dynamic_cast<Identifier*>(assign->target.get())) {
            vars.insert(ident->name);
        }
    }
    else if (auto* varDecl = dynamic_cast<VarDecl*>(body)) {
        vars.insert(varDecl->name);
    }
    else if (auto* block = dynamic_cast<Block*>(body)) {
        for (auto& stmt : block->statements) {
            auto stmtVars = collectModifiedVars(stmt.get());
            vars.insert(stmtVars.begin(), stmtVars.end());
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(body)) {
        auto thenVars = collectModifiedVars(ifStmt->thenBranch.get());
        vars.insert(thenVars.begin(), thenVars.end());
        for (auto& elif : ifStmt->elifBranches) {
            auto elifVars = collectModifiedVars(elif.second.get());
            vars.insert(elifVars.begin(), elifVars.end());
        }
        auto elseVars = collectModifiedVars(ifStmt->elseBranch.get());
        vars.insert(elseVars.begin(), elseVars.end());
    }
    else if (auto* whileLoop = dynamic_cast<WhileStmt*>(body)) {
        auto loopVars = collectModifiedVars(whileLoop->body.get());
        vars.insert(loopVars.begin(), loopVars.end());
    }
    else if (auto* forLoop = dynamic_cast<ForStmt*>(body)) {
        vars.insert(forLoop->var);
        auto loopVars = collectModifiedVars(forLoop->body.get());
        vars.insert(loopVars.begin(), loopVars.end());
    }
    
    return vars;
}

std::string LoopSimplifyPass::generateUniqueName(const std::string& base) {
    return base + "_ls_" + std::to_string(uniqueCounter_++);
}

ExprPtr LoopSimplifyPass::cloneExpression(Expression* expr) {
    if (!expr) return nullptr;
    
    if (auto* intLit = dynamic_cast<IntegerLiteral*>(expr)) {
        return std::make_unique<IntegerLiteral>(intLit->value, intLit->location);
    }
    if (auto* floatLit = dynamic_cast<FloatLiteral*>(expr)) {
        return std::make_unique<FloatLiteral>(floatLit->value, floatLit->location);
    }
    if (auto* boolLit = dynamic_cast<BoolLiteral*>(expr)) {
        return std::make_unique<BoolLiteral>(boolLit->value, boolLit->location);
    }
    if (auto* strLit = dynamic_cast<StringLiteral*>(expr)) {
        return std::make_unique<StringLiteral>(strLit->value, strLit->location);
    }
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        return std::make_unique<Identifier>(ident->name, ident->location);
    }
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        return std::make_unique<BinaryExpr>(
            cloneExpression(binary->left.get()),
            binary->op,
            cloneExpression(binary->right.get()),
            binary->location
        );
    }
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        return std::make_unique<UnaryExpr>(
            unary->op,
            cloneExpression(unary->operand.get()),
            unary->location
        );
    }
    
    return nullptr;
}

StmtPtr LoopSimplifyPass::cloneStatement(Statement* stmt) {
    if (!stmt) return nullptr;
    
    // Basic cloning - extend as needed
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        return std::make_unique<ExprStmt>(
            cloneExpression(exprStmt->expr.get()),
            exprStmt->location
        );
    }
    
    return nullptr;
}

void LoopSimplifyPass::hoistInvariantInits(std::vector<StmtPtr>& preheader, Statement* loop,
                                           const std::set<std::string>& loopVars) {
    // This would hoist loop-invariant variable initializations to the preheader
    // For now, this is a placeholder for future enhancement
}

} // namespace tyl
