// Tyl Compiler - Enhanced LICM Implementation
#include "enhanced_licm.h"
#include <algorithm>

namespace tyl {

// ============================================
// Alias Analysis Implementation
// ============================================

AliasResult AliasAnalysis::alias(const MemoryLocation& loc1, const MemoryLocation& loc2) {
    // Different base variables don't alias (simple model)
    if (loc1.base != loc2.base) {
        return AliasResult::NoAlias;
    }
    
    // Same base, both scalars - must alias
    if (!loc1.isArray && !loc2.isArray) {
        return AliasResult::MustAlias;
    }
    
    // Same base, one or both are arrays
    if (loc1.isArray && loc2.isArray) {
        // If indices are the same constant, must alias
        // Otherwise, may alias
        auto* idx1 = dynamic_cast<IntegerLiteral*>(loc1.index);
        auto* idx2 = dynamic_cast<IntegerLiteral*>(loc2.index);
        
        if (idx1 && idx2) {
            if (idx1->value == idx2->value) {
                return AliasResult::MustAlias;
            } else {
                return AliasResult::NoAlias;
            }
        }
        
        return AliasResult::MayAlias;
    }
    
    // One is array, one is scalar - may alias (conservative)
    return AliasResult::MayAlias;
}

bool AliasAnalysis::mayReadFrom(Expression* expr, const MemoryLocation& loc) {
    auto reads = getReads(expr);
    for (const auto& read : reads) {
        if (alias(read, loc) != AliasResult::NoAlias) {
            return true;
        }
    }
    return false;
}

bool AliasAnalysis::mayWriteTo(Statement* stmt, const MemoryLocation& loc) {
    auto writes = getWrites(stmt);
    for (const auto& write : writes) {
        if (alias(write, loc) != AliasResult::NoAlias) {
            return true;
        }
    }
    return false;
}

std::set<MemoryLocation> AliasAnalysis::getReads(Expression* expr) {
    std::set<MemoryLocation> reads;
    
    if (!expr) return reads;
    
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        MemoryLocation loc;
        loc.base = ident->name;
        loc.isArray = false;
        reads.insert(loc);
    }
    else if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        if (auto* base = dynamic_cast<Identifier*>(index->object.get())) {
            MemoryLocation loc;
            loc.base = base->name;
            loc.isArray = true;
            loc.index = index->index.get();
            reads.insert(loc);
        }
        // Also get reads from index expression
        auto indexReads = getReads(index->index.get());
        reads.insert(indexReads.begin(), indexReads.end());
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        auto left = getReads(binary->left.get());
        auto right = getReads(binary->right.get());
        reads.insert(left.begin(), left.end());
        reads.insert(right.begin(), right.end());
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        reads = getReads(unary->operand.get());
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        for (auto& arg : call->args) {
            auto argReads = getReads(arg.get());
            reads.insert(argReads.begin(), argReads.end());
        }
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        auto cond = getReads(ternary->condition.get());
        auto then_ = getReads(ternary->thenExpr.get());
        auto else_ = getReads(ternary->elseExpr.get());
        reads.insert(cond.begin(), cond.end());
        reads.insert(then_.begin(), then_.end());
        reads.insert(else_.begin(), else_.end());
    }
    
    return reads;
}

std::set<MemoryLocation> AliasAnalysis::getWrites(Statement* stmt) {
    std::set<MemoryLocation> writes;
    
    if (!stmt) return writes;
    
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        MemoryLocation loc;
        loc.base = varDecl->name;
        loc.isArray = false;
        writes.insert(loc);
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        if (auto* ident = dynamic_cast<Identifier*>(assignStmt->target.get())) {
            MemoryLocation loc;
            loc.base = ident->name;
            loc.isArray = false;
            writes.insert(loc);
        }
        else if (auto* index = dynamic_cast<IndexExpr*>(assignStmt->target.get())) {
            if (auto* base = dynamic_cast<Identifier*>(index->object.get())) {
                MemoryLocation loc;
                loc.base = base->name;
                loc.isArray = true;
                loc.index = index->index.get();
                writes.insert(loc);
            }
        }
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        if (auto* assignExpr = dynamic_cast<AssignExpr*>(exprStmt->expr.get())) {
            if (auto* ident = dynamic_cast<Identifier*>(assignExpr->target.get())) {
                MemoryLocation loc;
                loc.base = ident->name;
                loc.isArray = false;
                writes.insert(loc);
            }
        }
    }
    
    return writes;
}

// ============================================
// Enhanced LICM Pass Implementation
// ============================================

void EnhancedLICMPass::run(Program& ast) {
    transformations_ = 0;
    
    // Initialize pure functions set
    pureFunctions_ = {
        "abs", "sqrt", "sin", "cos", "tan", "asin", "acos", "atan",
        "floor", "ceil", "round", "min", "max", "pow", "exp", "log",
        "len", "str", "int", "float", "bool"
    };
    
    processStatements(ast.statements);
}

void EnhancedLICMPass::processStatements(std::vector<StmtPtr>& stmts) {
    for (size_t i = 0; i < stmts.size(); ++i) {
        auto* stmt = stmts[i].get();
        
        if (auto* forLoop = dynamic_cast<ForStmt*>(stmt)) {
            // Analyze the loop
            modifiedVars_.clear();
            modifiedLocations_.clear();
            analyzeLoop(forLoop);
            
            // Hoist invariant code
            std::vector<StmtPtr> hoisted;
            hoistFromForLoop(forLoop, hoisted);
            
            if (!hoisted.empty()) {
                for (auto& h : hoisted) {
                    stmts.insert(stmts.begin() + i, std::move(h));
                    i++;
                    transformations_++;
                }
            }
            
            // Process nested loops
            if (auto* body = dynamic_cast<Block*>(forLoop->body.get())) {
                processStatements(body->statements);
            }
        }
        else if (auto* whileLoop = dynamic_cast<WhileStmt*>(stmt)) {
            modifiedVars_.clear();
            modifiedLocations_.clear();
            analyzeLoop(whileLoop);
            
            std::vector<StmtPtr> hoisted;
            hoistFromWhileLoop(whileLoop, hoisted);
            
            if (!hoisted.empty()) {
                for (auto& h : hoisted) {
                    stmts.insert(stmts.begin() + i, std::move(h));
                    i++;
                    transformations_++;
                }
            }
            
            if (auto* body = dynamic_cast<Block*>(whileLoop->body.get())) {
                processStatements(body->statements);
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
            if (auto* body = dynamic_cast<Block*>(fnDecl->body.get())) {
                processStatements(body->statements);
            }
        }
    }
}

void EnhancedLICMPass::analyzeLoop(Statement* loop) {
    if (auto* forLoop = dynamic_cast<ForStmt*>(loop)) {
        modifiedVars_.insert(forLoop->var);
        analyzeModifiedVars(forLoop->body.get());
        analyzeModifiedMemory(forLoop->body.get());
    }
    else if (auto* whileLoop = dynamic_cast<WhileStmt*>(loop)) {
        analyzeModifiedVars(whileLoop->body.get());
        analyzeModifiedMemory(whileLoop->body.get());
    }
}

void EnhancedLICMPass::analyzeModifiedVars(Statement* stmt) {
    if (!stmt) return;
    
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            analyzeModifiedVars(s.get());
        }
    }
    else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        modifiedVars_.insert(varDecl->name);
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        if (auto* ident = dynamic_cast<Identifier*>(assignStmt->target.get())) {
            modifiedVars_.insert(ident->name);
        }
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        if (auto* assignExpr = dynamic_cast<AssignExpr*>(exprStmt->expr.get())) {
            if (auto* ident = dynamic_cast<Identifier*>(assignExpr->target.get())) {
                modifiedVars_.insert(ident->name);
            }
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        analyzeModifiedVars(ifStmt->thenBranch.get());
        for (auto& elif : ifStmt->elifBranches) {
            analyzeModifiedVars(elif.second.get());
        }
        analyzeModifiedVars(ifStmt->elseBranch.get());
    }
    else if (auto* forLoop = dynamic_cast<ForStmt*>(stmt)) {
        modifiedVars_.insert(forLoop->var);
        analyzeModifiedVars(forLoop->body.get());
    }
    else if (auto* whileLoop = dynamic_cast<WhileStmt*>(stmt)) {
        analyzeModifiedVars(whileLoop->body.get());
    }
}

void EnhancedLICMPass::analyzeModifiedMemory(Statement* stmt) {
    if (!stmt) return;
    
    auto writes = aliasAnalysis_.getWrites(stmt);
    modifiedLocations_.insert(writes.begin(), writes.end());
    
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            analyzeModifiedMemory(s.get());
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        analyzeModifiedMemory(ifStmt->thenBranch.get());
        for (auto& elif : ifStmt->elifBranches) {
            analyzeModifiedMemory(elif.second.get());
        }
        analyzeModifiedMemory(ifStmt->elseBranch.get());
    }
    else if (auto* forLoop = dynamic_cast<ForStmt*>(stmt)) {
        analyzeModifiedMemory(forLoop->body.get());
    }
    else if (auto* whileLoop = dynamic_cast<WhileStmt*>(stmt)) {
        analyzeModifiedMemory(whileLoop->body.get());
    }
}

bool EnhancedLICMPass::isLoopInvariant(Expression* expr, const std::string& inductionVar) {
    if (!expr) return true;
    
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        if (ident->name == inductionVar) return false;
        if (modifiedVars_.count(ident->name)) return false;
        return true;
    }
    
    if (dynamic_cast<IntegerLiteral*>(expr)) return true;
    if (dynamic_cast<FloatLiteral*>(expr)) return true;
    if (dynamic_cast<BoolLiteral*>(expr)) return true;
    if (dynamic_cast<StringLiteral*>(expr)) return true;
    if (dynamic_cast<NilLiteral*>(expr)) return true;
    
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        return isLoopInvariant(binary->left.get(), inductionVar) &&
               isLoopInvariant(binary->right.get(), inductionVar);
    }
    
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        return isLoopInvariant(unary->operand.get(), inductionVar);
    }
    
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        // Only pure functions with invariant arguments are invariant
        if (auto* callee = dynamic_cast<Identifier*>(call->callee.get())) {
            if (!isPureFunction(callee->name)) {
                return false;
            }
        } else {
            return false;  // Unknown callee
        }
        
        for (auto& arg : call->args) {
            if (!isLoopInvariant(arg.get(), inductionVar)) {
                return false;
            }
        }
        return true;
    }
    
    if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        // Array access is invariant if base and index are invariant
        // AND the array is not modified in the loop
        if (!isLoopInvariant(index->object.get(), inductionVar)) return false;
        if (!isLoopInvariant(index->index.get(), inductionVar)) return false;
        
        // Check if array is modified
        if (auto* base = dynamic_cast<Identifier*>(index->object.get())) {
            for (const auto& loc : modifiedLocations_) {
                if (loc.base == base->name) {
                    return false;  // Array is modified
                }
            }
        }
        return true;
    }
    
    if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        return isLoopInvariant(ternary->condition.get(), inductionVar) &&
               isLoopInvariant(ternary->thenExpr.get(), inductionVar) &&
               isLoopInvariant(ternary->elseExpr.get(), inductionVar);
    }
    
    if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        return isLoopInvariant(member->object.get(), inductionVar);
    }
    
    return false;
}

bool EnhancedLICMPass::isSafeToHoist(Statement* stmt, const std::string& inductionVar) {
    if (!stmt) return false;
    
    // Variable declarations should NOT be hoisted
    // They create new bindings each iteration
    if (dynamic_cast<VarDecl*>(stmt)) {
        return false;
    }
    
    // Assignments can be hoisted if:
    // 1. The value is loop invariant
    // 2. The target is not used before this point in the loop
    // 3. The assignment has no side effects
    if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        if (!isLoopInvariant(assignStmt->value.get(), inductionVar)) {
            return false;
        }
        // For safety, don't hoist assignments
        return false;
    }
    
    // Expression statements with pure, invariant expressions can be hoisted
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        if (hasSideEffects(exprStmt->expr.get())) {
            return false;
        }
        return isLoopInvariant(exprStmt->expr.get(), inductionVar);
    }
    
    return false;
}

bool EnhancedLICMPass::hasSideEffects(Expression* expr) {
    if (!expr) return false;
    
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        if (auto* callee = dynamic_cast<Identifier*>(call->callee.get())) {
            if (!isPureFunction(callee->name)) {
                return true;
            }
        } else {
            return true;  // Unknown callee
        }
        
        for (auto& arg : call->args) {
            if (hasSideEffects(arg.get())) return true;
        }
        return false;
    }
    
    if (auto* assignExpr = dynamic_cast<AssignExpr*>(expr)) {
        return true;  // Assignments have side effects
    }
    
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        return hasSideEffects(binary->left.get()) || hasSideEffects(binary->right.get());
    }
    
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        return hasSideEffects(unary->operand.get());
    }
    
    if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        return hasSideEffects(ternary->condition.get()) ||
               hasSideEffects(ternary->thenExpr.get()) ||
               hasSideEffects(ternary->elseExpr.get());
    }
    
    return false;
}

bool EnhancedLICMPass::isPureFunction(const std::string& name) {
    return pureFunctions_.count(name) > 0;
}

void EnhancedLICMPass::hoistFromForLoop(ForStmt* loop, std::vector<StmtPtr>& hoisted) {
    auto* body = dynamic_cast<Block*>(loop->body.get());
    if (!body) return;
    
    auto it = body->statements.begin();
    while (it != body->statements.end()) {
        if (isSafeToHoist(it->get(), loop->var)) {
            hoisted.push_back(std::move(*it));
            it = body->statements.erase(it);
        } else {
            ++it;
        }
    }
}

void EnhancedLICMPass::hoistFromWhileLoop(WhileStmt* loop, std::vector<StmtPtr>& hoisted) {
    auto* body = dynamic_cast<Block*>(loop->body.get());
    if (!body) return;
    
    auto it = body->statements.begin();
    while (it != body->statements.end()) {
        if (isSafeToHoist(it->get(), "")) {
            hoisted.push_back(std::move(*it));
            it = body->statements.erase(it);
        } else {
            ++it;
        }
    }
}

std::string EnhancedLICMPass::generateTempName() {
    return "$licm_temp_" + std::to_string(tempCounter_++);
}

// ============================================
// Invariant Expression Hoisting Pass
// ============================================

void InvariantExpressionHoistingPass::run(Program& ast) {
    transformations_ = 0;
    processStatements(ast.statements);
}

void InvariantExpressionHoistingPass::processStatements(std::vector<StmtPtr>& stmts) {
    for (size_t i = 0; i < stmts.size(); ++i) {
        auto* stmt = stmts[i].get();
        
        if (auto* forLoop = dynamic_cast<ForStmt*>(stmt)) {
            modifiedVars_.clear();
            analyzeModifiedVars(forLoop);
            modifiedVars_.insert(forLoop->var);
            
            std::vector<StmtPtr> insertBefore;
            processLoop(forLoop, insertBefore);
            
            for (auto& h : insertBefore) {
                stmts.insert(stmts.begin() + i, std::move(h));
                i++;
                transformations_++;
            }
            
            if (auto* body = dynamic_cast<Block*>(forLoop->body.get())) {
                processStatements(body->statements);
            }
        }
        else if (auto* whileLoop = dynamic_cast<WhileStmt*>(stmt)) {
            modifiedVars_.clear();
            analyzeModifiedVars(whileLoop);
            
            std::vector<StmtPtr> insertBefore;
            processLoop(whileLoop, insertBefore);
            
            for (auto& h : insertBefore) {
                stmts.insert(stmts.begin() + i, std::move(h));
                i++;
                transformations_++;
            }
            
            if (auto* body = dynamic_cast<Block*>(whileLoop->body.get())) {
                processStatements(body->statements);
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
            if (auto* body = dynamic_cast<Block*>(fnDecl->body.get())) {
                processStatements(body->statements);
            }
        }
    }
}

void InvariantExpressionHoistingPass::processLoop(ForStmt* loop, std::vector<StmtPtr>& insertBefore) {
    // This pass focuses on hoisting invariant sub-expressions
    // For now, delegate to the main LICM pass
}

void InvariantExpressionHoistingPass::processLoop(WhileStmt* loop, std::vector<StmtPtr>& insertBefore) {
    // This pass focuses on hoisting invariant sub-expressions
}

void InvariantExpressionHoistingPass::analyzeModifiedVars(Statement* stmt) {
    if (!stmt) return;
    
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            analyzeModifiedVars(s.get());
        }
    }
    else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        modifiedVars_.insert(varDecl->name);
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        if (auto* ident = dynamic_cast<Identifier*>(assignStmt->target.get())) {
            modifiedVars_.insert(ident->name);
        }
    }
    else if (auto* forLoop = dynamic_cast<ForStmt*>(stmt)) {
        modifiedVars_.insert(forLoop->var);
        analyzeModifiedVars(forLoop->body.get());
    }
    else if (auto* whileLoop = dynamic_cast<WhileStmt*>(stmt)) {
        analyzeModifiedVars(whileLoop->body.get());
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        analyzeModifiedVars(ifStmt->thenBranch.get());
        for (auto& elif : ifStmt->elifBranches) {
            analyzeModifiedVars(elif.second.get());
        }
        analyzeModifiedVars(ifStmt->elseBranch.get());
    }
}

bool InvariantExpressionHoistingPass::isInvariant(Expression* expr, const std::string& inductionVar) {
    if (!expr) return true;
    
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        if (ident->name == inductionVar) return false;
        if (modifiedVars_.count(ident->name)) return false;
        return true;
    }
    
    if (dynamic_cast<IntegerLiteral*>(expr)) return true;
    if (dynamic_cast<FloatLiteral*>(expr)) return true;
    if (dynamic_cast<BoolLiteral*>(expr)) return true;
    if (dynamic_cast<StringLiteral*>(expr)) return true;
    
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        return isInvariant(binary->left.get(), inductionVar) &&
               isInvariant(binary->right.get(), inductionVar);
    }
    
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        return isInvariant(unary->operand.get(), inductionVar);
    }
    
    return false;
}

std::string InvariantExpressionHoistingPass::generateTempName() {
    return "$inv_temp_" + std::to_string(tempCounter_++);
}

// Factory functions
std::unique_ptr<EnhancedLICMPass> createEnhancedLICMPass() {
    return std::make_unique<EnhancedLICMPass>();
}

std::unique_ptr<InvariantExpressionHoistingPass> createInvariantExpressionHoistingPass() {
    return std::make_unique<InvariantExpressionHoistingPass>();
}

} // namespace tyl
