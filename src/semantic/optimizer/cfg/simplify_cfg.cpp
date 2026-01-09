// Tyl Compiler - SimplifyCFG Implementation
#include "simplify_cfg.h"
#include <algorithm>
#include <iostream>

namespace tyl {

void SimplifyCFGPass::run(Program& ast) {
    transformations_ = 0;
    stats_ = SimplifyCFGStats{};
    
    // Process all functions in the program
    for (auto& stmt : ast.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            processFunction(fn);
        }
    }
    
    // Sum up all transformations
    transformations_ = stats_.constantConditionsSimplified +
                       stats_.emptyBlocksRemoved +
                       stats_.unreachableCodeRemoved +
                       stats_.commonCodeHoisted +
                       stats_.commonCodeSunk +
                       stats_.ifChainsToSwitch +
                       stats_.nestedIfsFlattened +
                       stats_.redundantBranchesRemoved;
}

void SimplifyCFGPass::processFunction(FnDecl* fn) {
    if (!fn->body) return;
    
    // Process the function body
    processStatement(fn->body);
}

void SimplifyCFGPass::processBlock(std::vector<StmtPtr>& stmts) {
    // Process each statement
    for (size_t i = 0; i < stmts.size(); ++i) {
        if (processStatement(stmts[i])) {
            // Statement was modified
        }
        
        // Handle statement that became null (removed)
        if (!stmts[i]) {
            stmts.erase(stmts.begin() + i);
            --i;
        }
    }
    
    // Remove unreachable code after processing
    removeUnreachableCode(stmts);
}

bool SimplifyCFGPass::processStatement(StmtPtr& stmt) {
    if (!stmt) return false;
    
    bool changed = false;
    
    // Try various simplifications
    if (simplifyConstantCondition(stmt)) {
        changed = true;
    }
    
    if (stmt && removeEmptyBlocks(stmt)) {
        changed = true;
    }
    
    if (stmt && flattenNestedIfs(stmt)) {
        changed = true;
    }
    
    if (stmt && removeRedundantBranches(stmt)) {
        changed = true;
    }
    
    // Recurse into nested structures
    if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
        // Process then branch
        if (ifStmt->thenBranch) {
            processStatement(ifStmt->thenBranch);
        }
        // Process else branch
        if (ifStmt->elseBranch) {
            processStatement(ifStmt->elseBranch);
        }
        // Process elif branches
        for (auto& elif : ifStmt->elifBranches) {
            if (elif.second) {
                processStatement(elif.second);
            }
        }
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt.get())) {
        if (whileStmt->body) {
            processStatement(whileStmt->body);
        }
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt.get())) {
        if (forStmt->body) {
            processStatement(forStmt->body);
        }
    }
    else if (auto* matchStmt = dynamic_cast<MatchStmt*>(stmt.get())) {
        for (auto& c : matchStmt->cases) {
            if (c.body) {
                processStatement(c.body);
            }
        }
        if (matchStmt->defaultCase) {
            processStatement(matchStmt->defaultCase);
        }
    }
    else if (auto* block = dynamic_cast<Block*>(stmt.get())) {
        processBlock(block->statements);
    }
    
    return changed;
}

// === Constant Condition Simplification ===

bool SimplifyCFGPass::simplifyConstantCondition(StmtPtr& stmt) {
    if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
        bool condValue;
        if (isConstantBool(ifStmt->condition.get(), condValue)) {
            if (condValue) {
                // if (true) { A } else { B } → A
                stmt = std::move(ifStmt->thenBranch);
            } else {
                // if (false) { A } else { B } → B
                if (!ifStmt->elseBranch) {
                    stmt = nullptr;  // Remove entirely
                } else {
                    stmt = std::move(ifStmt->elseBranch);
                }
            }
            ++stats_.constantConditionsSimplified;
            ++transformations_;
            return true;
        }
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt.get())) {
        bool condValue;
        if (isConstantBool(whileStmt->condition.get(), condValue)) {
            if (!condValue) {
                // while (false) { ... } → remove
                stmt = nullptr;
                ++stats_.constantConditionsSimplified;
                ++transformations_;
                return true;
            }
            // while (true) is an infinite loop - leave it
        }
    }
    
    return false;
}

bool SimplifyCFGPass::isConstantBool(Expression* expr, bool& value) {
    if (auto* lit = dynamic_cast<BoolLiteral*>(expr)) {
        value = lit->value;
        return true;
    }
    
    // Check for negation of constant
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        if (unary->op == TokenType::NOT || unary->op == TokenType::BANG) {
            bool innerValue;
            if (isConstantBool(unary->operand.get(), innerValue)) {
                value = !innerValue;
                return true;
            }
        }
    }
    
    return false;
}

// === Unreachable Code Removal ===

bool SimplifyCFGPass::removeUnreachableCode(std::vector<StmtPtr>& stmts) {
    bool changed = false;
    
    for (size_t i = 0; i < stmts.size(); ++i) {
        if (alwaysTerminates(stmts[i].get())) {
            // Remove everything after this statement
            if (i + 1 < stmts.size()) {
                size_t removed = stmts.size() - i - 1;
                stmts.erase(stmts.begin() + i + 1, stmts.end());
                stats_.unreachableCodeRemoved += static_cast<int>(removed);
                transformations_ += static_cast<int>(removed);
                changed = true;
            }
            break;
        }
    }
    
    return changed;
}

bool SimplifyCFGPass::alwaysTerminates(Statement* stmt) {
    if (dynamic_cast<ReturnStmt*>(stmt)) return true;
    if (dynamic_cast<BreakStmt*>(stmt)) return true;
    if (dynamic_cast<ContinueStmt*>(stmt)) return true;
    
    // Check if-else where both branches terminate
    if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        if (!ifStmt->elseBranch) return false;
        
        bool thenTerminates = ifStmt->thenBranch && alwaysTerminates(ifStmt->thenBranch.get());
        bool elseTerminates = ifStmt->elseBranch && alwaysTerminates(ifStmt->elseBranch.get());
        
        return thenTerminates && elseTerminates;
    }
    
    // Check block - terminates if last statement terminates
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        if (!block->statements.empty()) {
            return alwaysTerminates(block->statements.back().get());
        }
    }
    
    return false;
}

// === Empty Block Removal ===

bool SimplifyCFGPass::removeEmptyBlocks(StmtPtr& stmt) {
    auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get());
    if (!ifStmt) return false;
    
    bool thenEmpty = !ifStmt->thenBranch || isEmptyStatement(ifStmt->thenBranch.get());
    bool elseEmpty = !ifStmt->elseBranch || isEmptyStatement(ifStmt->elseBranch.get());
    
    if (thenEmpty && elseEmpty) {
        // Both branches empty - remove entire if
        stmt = nullptr;
        ++stats_.emptyBlocksRemoved;
        ++transformations_;
        return true;
    }
    
    if (thenEmpty && !elseEmpty) {
        // if (cond) { } else { B } → if (!cond) { B }
        ifStmt->condition = negateCondition(ifStmt->condition.get());
        ifStmt->thenBranch = std::move(ifStmt->elseBranch);
        ifStmt->elseBranch = nullptr;
        ++stats_.emptyBlocksRemoved;
        ++transformations_;
        return true;
    }
    
    if (!thenEmpty && elseEmpty && ifStmt->elseBranch) {
        // if (cond) { A } else { } → if (cond) { A }
        ifStmt->elseBranch = nullptr;
        ++stats_.emptyBlocksRemoved;
        ++transformations_;
        return true;
    }
    
    return false;
}

bool SimplifyCFGPass::isEmptyBlock(const std::vector<StmtPtr>& stmts) {
    if (stmts.empty()) return true;
    
    for (const auto& stmt : stmts) {
        if (!isEmptyStatement(stmt.get())) return false;
    }
    
    return true;
}

bool SimplifyCFGPass::isEmptyStatement(Statement* stmt) {
    if (!stmt) return true;
    
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        return isEmptyBlock(block->statements);
    }
    
    return false;
}

// === Common Code Hoisting ===

bool SimplifyCFGPass::hoistCommonCode(IfStmt* ifStmt) {
    // Need both branches as blocks to hoist
    auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get());
    auto* elseBlock = dynamic_cast<Block*>(ifStmt->elseBranch.get());
    
    if (!thenBlock || !elseBlock) return false;
    if (thenBlock->statements.empty() || elseBlock->statements.empty()) return false;
    
    // Find common prefix
    size_t commonCount = 0;
    size_t maxCommon = std::min(thenBlock->statements.size(), elseBlock->statements.size());
    
    while (commonCount < maxCommon) {
        if (statementsEqual(thenBlock->statements[commonCount].get(),
                           elseBlock->statements[commonCount].get())) {
            ++commonCount;
        } else {
            break;
        }
    }
    
    if (commonCount == 0) return false;
    
    stats_.commonCodeHoisted += static_cast<int>(commonCount);
    transformations_ += static_cast<int>(commonCount);
    
    // Remove common prefix from both branches
    thenBlock->statements.erase(thenBlock->statements.begin(), 
                                thenBlock->statements.begin() + commonCount);
    elseBlock->statements.erase(elseBlock->statements.begin(), 
                                elseBlock->statements.begin() + commonCount);
    
    return true;
}

// === Common Code Sinking ===

bool SimplifyCFGPass::sinkCommonCode(IfStmt* ifStmt) {
    auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get());
    auto* elseBlock = dynamic_cast<Block*>(ifStmt->elseBranch.get());
    
    if (!thenBlock || !elseBlock) return false;
    if (thenBlock->statements.empty() || elseBlock->statements.empty()) return false;
    
    // Find common suffix
    size_t commonCount = 0;
    size_t maxCommon = std::min(thenBlock->statements.size(), elseBlock->statements.size());
    
    while (commonCount < maxCommon) {
        size_t thenIdx = thenBlock->statements.size() - 1 - commonCount;
        size_t elseIdx = elseBlock->statements.size() - 1 - commonCount;
        
        if (statementsEqual(thenBlock->statements[thenIdx].get(),
                           elseBlock->statements[elseIdx].get())) {
            ++commonCount;
        } else {
            break;
        }
    }
    
    if (commonCount == 0) return false;
    
    stats_.commonCodeSunk += static_cast<int>(commonCount);
    transformations_ += static_cast<int>(commonCount);
    
    // Remove common suffix from both branches
    thenBlock->statements.erase(thenBlock->statements.end() - commonCount, 
                                thenBlock->statements.end());
    elseBlock->statements.erase(elseBlock->statements.end() - commonCount, 
                                elseBlock->statements.end());
    
    return true;
}

// === Statement/Expression Equality ===

bool SimplifyCFGPass::statementsEqual(Statement* a, Statement* b) {
    if (!a || !b) return a == b;
    
    // Check type match
    if (typeid(*a) != typeid(*b)) return false;
    
    // Compare by type
    if (auto* aExpr = dynamic_cast<ExprStmt*>(a)) {
        auto* bExpr = dynamic_cast<ExprStmt*>(b);
        return expressionsEqual(aExpr->expr.get(), bExpr->expr.get());
    }
    
    if (auto* aVar = dynamic_cast<VarDecl*>(a)) {
        auto* bVar = dynamic_cast<VarDecl*>(b);
        if (aVar->name != bVar->name) return false;
        if (aVar->isMutable != bVar->isMutable) return false;
        return expressionsEqual(aVar->initializer.get(), bVar->initializer.get());
    }
    
    if (auto* aRet = dynamic_cast<ReturnStmt*>(a)) {
        auto* bRet = dynamic_cast<ReturnStmt*>(b);
        return expressionsEqual(aRet->value.get(), bRet->value.get());
    }
    
    // For complex statements, be conservative
    return false;
}

bool SimplifyCFGPass::expressionsEqual(Expression* a, Expression* b) {
    if (!a || !b) return a == b;
    
    if (typeid(*a) != typeid(*b)) return false;
    
    // Literals
    if (auto* aInt = dynamic_cast<IntegerLiteral*>(a)) {
        return aInt->value == dynamic_cast<IntegerLiteral*>(b)->value;
    }
    if (auto* aFloat = dynamic_cast<FloatLiteral*>(a)) {
        return aFloat->value == dynamic_cast<FloatLiteral*>(b)->value;
    }
    if (auto* aBool = dynamic_cast<BoolLiteral*>(a)) {
        return aBool->value == dynamic_cast<BoolLiteral*>(b)->value;
    }
    if (auto* aStr = dynamic_cast<StringLiteral*>(a)) {
        return aStr->value == dynamic_cast<StringLiteral*>(b)->value;
    }
    
    // Identifiers
    if (auto* aId = dynamic_cast<Identifier*>(a)) {
        return aId->name == dynamic_cast<Identifier*>(b)->name;
    }
    
    // Binary expressions
    if (auto* aBin = dynamic_cast<BinaryExpr*>(a)) {
        auto* bBin = dynamic_cast<BinaryExpr*>(b);
        if (aBin->op != bBin->op) return false;
        return expressionsEqual(aBin->left.get(), bBin->left.get()) &&
               expressionsEqual(aBin->right.get(), bBin->right.get());
    }
    
    // Unary expressions
    if (auto* aUn = dynamic_cast<UnaryExpr*>(a)) {
        auto* bUn = dynamic_cast<UnaryExpr*>(b);
        if (aUn->op != bUn->op) return false;
        return expressionsEqual(aUn->operand.get(), bUn->operand.get());
    }
    
    // Call expressions
    if (auto* aCall = dynamic_cast<CallExpr*>(a)) {
        auto* bCall = dynamic_cast<CallExpr*>(b);
        if (!expressionsEqual(aCall->callee.get(), bCall->callee.get())) return false;
        if (aCall->args.size() != bCall->args.size()) return false;
        for (size_t i = 0; i < aCall->args.size(); ++i) {
            if (!expressionsEqual(aCall->args[i].get(), bCall->args[i].get())) {
                return false;
            }
        }
        return true;
    }
    
    // Member access
    if (auto* aMem = dynamic_cast<MemberExpr*>(a)) {
        auto* bMem = dynamic_cast<MemberExpr*>(b);
        if (aMem->member != bMem->member) return false;
        return expressionsEqual(aMem->object.get(), bMem->object.get());
    }
    
    // Index access
    if (auto* aIdx = dynamic_cast<IndexExpr*>(a)) {
        auto* bIdx = dynamic_cast<IndexExpr*>(b);
        return expressionsEqual(aIdx->object.get(), bIdx->object.get()) &&
               expressionsEqual(aIdx->index.get(), bIdx->index.get());
    }
    
    // Be conservative for other expression types
    return false;
}

// === Nested If Flattening ===

bool SimplifyCFGPass::flattenNestedIfs(StmtPtr& stmt) {
    auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get());
    if (!ifStmt) return false;
    
    // Check: if (a) { if (b) { X } }
    // No else on outer, then branch is a block with single if statement with no else
    if (ifStmt->elseBranch) return false;
    
    // Get the then branch - could be a block or directly an if
    IfStmt* innerIf = nullptr;
    
    if (auto* block = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
        if (block->statements.size() != 1) return false;
        innerIf = dynamic_cast<IfStmt*>(block->statements[0].get());
    } else {
        innerIf = dynamic_cast<IfStmt*>(ifStmt->thenBranch.get());
    }
    
    if (!innerIf) return false;
    if (innerIf->elseBranch) return false;
    if (!innerIf->elifBranches.empty()) return false;
    
    // Transform: if (a && b) { X }
    auto combined = createAnd(std::move(ifStmt->condition), 
                              std::move(innerIf->condition));
    
    auto newIf = std::make_unique<IfStmt>(std::move(combined), 
                                          std::move(innerIf->thenBranch),
                                          ifStmt->location);
    
    stmt = std::move(newIf);
    ++stats_.nestedIfsFlattened;
    ++transformations_;
    return true;
}

// === Redundant Branch Removal ===

bool SimplifyCFGPass::removeRedundantBranches(StmtPtr& stmt) {
    auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get());
    if (!ifStmt) return false;
    if (!ifStmt->thenBranch || !ifStmt->elseBranch) return false;
    
    // Check: if (c) { X } else { X } → X
    if (statementsEqual(ifStmt->thenBranch.get(), ifStmt->elseBranch.get())) {
        stmt = std::move(ifStmt->thenBranch);
        ++stats_.redundantBranchesRemoved;
        ++transformations_;
        return true;
    }
    
    return false;
}

// === If-Chain to Switch Conversion ===

bool SimplifyCFGPass::convertIfChainToSwitch(StmtPtr& stmt) {
    auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get());
    if (!ifStmt) return false;
    
    std::string switchVar;
    std::vector<IfChainCase> cases;
    std::vector<StmtPtr> defaultBody;
    
    if (!analyzeIfChain(ifStmt, switchVar, cases, defaultBody)) {
        return false;
    }
    
    // Need at least 3 cases to be worth converting
    if (cases.size() < 3) return false;
    
    // Create match statement
    auto match = std::make_unique<MatchStmt>(
        std::make_unique<Identifier>(switchVar, ifStmt->location),
        ifStmt->location);
    
    for (auto& c : cases) {
        MatchCase mc(
            std::make_unique<IntegerLiteral>(c.value, ifStmt->location),
            nullptr,  // no guard
            std::move(c.body)
        );
        match->cases.push_back(std::move(mc));
    }
    
    // Add default case if present
    if (!defaultBody.empty()) {
        auto defaultBlock = std::make_unique<Block>(ifStmt->location);
        defaultBlock->statements = std::move(defaultBody);
        match->defaultCase = std::move(defaultBlock);
    }
    
    stmt = std::move(match);
    ++stats_.ifChainsToSwitch;
    ++transformations_;
    return true;
}

bool SimplifyCFGPass::analyzeIfChain(IfStmt* ifStmt, std::string& switchVar,
                                      std::vector<IfChainCase>& cases,
                                      std::vector<StmtPtr>& defaultBody) {
    // Check if condition is: var == constant
    auto* cond = dynamic_cast<BinaryExpr*>(ifStmt->condition.get());
    if (!cond || cond->op != TokenType::EQ) return false;
    
    auto* varExpr = dynamic_cast<Identifier*>(cond->left.get());
    auto* constExpr = dynamic_cast<IntegerLiteral*>(cond->right.get());
    
    if (!varExpr || !constExpr) {
        // Try swapped order
        varExpr = dynamic_cast<Identifier*>(cond->right.get());
        constExpr = dynamic_cast<IntegerLiteral*>(cond->left.get());
        if (!varExpr || !constExpr) return false;
    }
    
    if (switchVar.empty()) {
        switchVar = varExpr->name;
    } else if (switchVar != varExpr->name) {
        return false;  // Different variable
    }
    
    // Add this case
    IfChainCase c;
    c.value = constExpr->value;
    c.body = cloneStatement(ifStmt->thenBranch.get());
    cases.push_back(std::move(c));
    
    // Check else branch
    if (!ifStmt->elseBranch) {
        return true;
    }
    
    // Check if else is another if
    if (auto* nextIf = dynamic_cast<IfStmt*>(ifStmt->elseBranch.get())) {
        return analyzeIfChain(nextIf, switchVar, cases, defaultBody);
    }
    
    // Check if else is a block containing a single if
    if (auto* block = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
        if (block->statements.size() == 1) {
            if (auto* nextIf = dynamic_cast<IfStmt*>(block->statements[0].get())) {
                return analyzeIfChain(nextIf, switchVar, cases, defaultBody);
            }
        }
        // Else block is the default case
        for (auto& s : block->statements) {
            defaultBody.push_back(cloneStatement(s.get()));
        }
    }
    
    return true;
}

// === Utility Functions ===

ExprPtr SimplifyCFGPass::negateCondition(Expression* cond) {
    // If already negated, remove negation
    if (auto* unary = dynamic_cast<UnaryExpr*>(cond)) {
        if (unary->op == TokenType::NOT || unary->op == TokenType::BANG) {
            return cloneExpression(unary->operand.get());
        }
    }
    
    // Create negation using proper constructor
    auto cloned = cloneExpression(cond);
    return std::make_unique<UnaryExpr>(TokenType::NOT, std::move(cloned), cond->location);
}

ExprPtr SimplifyCFGPass::createAnd(ExprPtr left, ExprPtr right) {
    SourceLocation loc = left ? left->location : SourceLocation{};
    return std::make_unique<BinaryExpr>(std::move(left), TokenType::AND, std::move(right), loc);
}

StmtPtr SimplifyCFGPass::cloneStatement(Statement* stmt) {
    if (!stmt) return nullptr;
    
    // Clone return statement
    if (auto* ret = dynamic_cast<ReturnStmt*>(stmt)) {
        return std::make_unique<ReturnStmt>(cloneExpression(ret->value.get()), ret->location);
    }
    
    // Clone expression statement
    if (auto* expr = dynamic_cast<ExprStmt*>(stmt)) {
        return std::make_unique<ExprStmt>(cloneExpression(expr->expr.get()), expr->location);
    }
    
    // Clone block
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        auto newBlock = std::make_unique<Block>(block->location);
        for (auto& s : block->statements) {
            if (auto cloned = cloneStatement(s.get())) {
                newBlock->statements.push_back(std::move(cloned));
            }
        }
        return newBlock;
    }
    
    // For other statements, return nullptr (can't clone)
    return nullptr;
}

ExprPtr SimplifyCFGPass::cloneExpression(Expression* expr) {
    if (!expr) return nullptr;
    
    if (auto* lit = dynamic_cast<IntegerLiteral*>(expr)) {
        return std::make_unique<IntegerLiteral>(lit->value, lit->location);
    }
    if (auto* lit = dynamic_cast<FloatLiteral*>(expr)) {
        return std::make_unique<FloatLiteral>(lit->value, lit->location);
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
    
    // For complex expressions, return nullptr
    return nullptr;
}

} // namespace tyl
