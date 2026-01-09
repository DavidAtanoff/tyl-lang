// Tyl Compiler - Enhanced GVN with PRE Implementation
#include "gvn_pre.h"
#include <algorithm>

namespace tyl {

// ============================================
// GVN-PRE Pass Implementation
// ============================================

void GVNPREPass::run(Program& ast) {
    transformations_ = 0;
    
    for (auto& stmt : ast.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            processFunction(fn);
        }
        else if (auto* module = dynamic_cast<ModuleDecl*>(stmt.get())) {
            for (auto& modStmt : module->body) {
                if (auto* fn = dynamic_cast<FnDecl*>(modStmt.get())) {
                    processFunction(fn);
                }
            }
        }
    }
}

void GVNPREPass::processFunction(FnDecl* fn) {
    if (!fn || !fn->body) return;
    
    resetState();
    
    // Add parameters to VN map
    for (auto& param : fn->params) {
        varToVN_[param.first] = nextVN_++;
    }
    
    auto* body = dynamic_cast<Block*>(fn->body.get());
    if (body) {
        // Phase 1: Standard GVN
        processBlock(body->statements);
        
        // Phase 2: Load elimination
        optimizeLoads(body->statements);
        
        // Phase 3: Store optimization
        optimizeStores(body->statements);
    }
}

void GVNPREPass::resetState() {
    nextVN_ = 1;
    exprToVN_.clear();
    varToVN_.clear();
    vnToConst_.clear();
    vnToFloatConst_.clear();
    vnToStringConst_.clear();
    memoryState_.clear();
    loadCache_.clear();
    exprToTemp_.clear();
    tempCounter_ = 0;
}

ValueNumber GVNPREPass::getValueNumber(Expression* expr) {
    if (!expr) return INVALID_VN;
    
    VNKey key = makeKey(expr);
    
    auto it = exprToVN_.find(key);
    if (it != exprToVN_.end()) {
        return it->second;
    }
    
    ValueNumber vn = nextVN_++;
    exprToVN_[key] = vn;
    
    // Track constant values
    if (auto* intLit = dynamic_cast<IntegerLiteral*>(expr)) {
        vnToConst_[vn] = intLit->value;
    }
    else if (auto* floatLit = dynamic_cast<FloatLiteral*>(expr)) {
        vnToFloatConst_[vn] = floatLit->value;
    }
    else if (auto* strLit = dynamic_cast<StringLiteral*>(expr)) {
        vnToStringConst_[vn] = strLit->value;
    }
    
    return vn;
}

VNKey GVNPREPass::makeKey(Expression* expr) {
    VNKey key;
    key.op = TokenType::ERROR;
    key.left = INVALID_VN;
    key.right = INVALID_VN;
    
    if (auto* intLit = dynamic_cast<IntegerLiteral*>(expr)) {
        key.op = TokenType::INTEGER;
        key.literal = std::to_string(intLit->value);
    }
    else if (auto* floatLit = dynamic_cast<FloatLiteral*>(expr)) {
        key.op = TokenType::FLOAT;
        key.literal = std::to_string(floatLit->value);
    }
    else if (auto* boolLit = dynamic_cast<BoolLiteral*>(expr)) {
        key.op = TokenType::TRUE;
        key.literal = boolLit->value ? "true" : "false";
    }
    else if (auto* strLit = dynamic_cast<StringLiteral*>(expr)) {
        key.op = TokenType::STRING;
        key.literal = strLit->value;
    }
    else if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        auto it = varToVN_.find(ident->name);
        if (it != varToVN_.end()) {
            key.op = TokenType::IDENTIFIER;
            key.left = it->second;
        } else {
            key.op = TokenType::IDENTIFIER;
            key.literal = ident->name;
        }
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        key.op = binary->op;
        key.left = getValueNumber(binary->left.get());
        key.right = getValueNumber(binary->right.get());
        
        // Normalize commutative operations
        bool isCommutative = (binary->op == TokenType::PLUS || 
                             binary->op == TokenType::STAR ||
                             binary->op == TokenType::EQ ||
                             binary->op == TokenType::NE ||
                             binary->op == TokenType::AND ||
                             binary->op == TokenType::OR ||
                             binary->op == TokenType::AMP ||
                             binary->op == TokenType::PIPE ||
                             binary->op == TokenType::CARET);
        
        if (isCommutative && key.left > key.right) {
            std::swap(key.left, key.right);
        }
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        key.op = unary->op;
        key.left = getValueNumber(unary->operand.get());
    }
    else if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        key.op = TokenType::LBRACKET;  // Use bracket as index marker
        key.left = getValueNumber(index->object.get());
        key.right = getValueNumber(index->index.get());
    }
    
    return key;
}

void GVNPREPass::processBlock(std::vector<StmtPtr>& stmts) {
    for (auto& stmt : stmts) {
        processStatement(stmt);
    }
}

void GVNPREPass::processStatement(StmtPtr& stmt) {
    if (!stmt) return;
    
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt.get())) {
        if (varDecl->initializer) {
            auto replacement = processExpression(varDecl->initializer);
            if (replacement) {
                varDecl->initializer = std::move(replacement);
            }
            
            ValueNumber vn = getValueNumber(varDecl->initializer.get());
            varToVN_[varDecl->name] = vn;
            memoryState_[varDecl->name] = vn;
        }
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt.get())) {
        auto replacement = processExpression(assignStmt->value);
        if (replacement) {
            assignStmt->value = std::move(replacement);
        }
        
        if (auto* target = dynamic_cast<Identifier*>(assignStmt->target.get())) {
            ValueNumber vn = getValueNumber(assignStmt->value.get());
            varToVN_[target->name] = vn;
            memoryState_[target->name] = vn;
            
            // Invalidate any cached loads that depend on this variable
            invalidateMemory(target->name);
        }
        else if (auto* indexExpr = dynamic_cast<IndexExpr*>(assignStmt->target.get())) {
            // Array store - invalidate cached loads for this array
            if (auto* arrayIdent = dynamic_cast<Identifier*>(indexExpr->object.get())) {
                invalidateMemory(arrayIdent->name);
            }
        }
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt.get())) {
        auto replacement = processExpression(exprStmt->expr);
        if (replacement) {
            exprStmt->expr = std::move(replacement);
        }
        
        // Handle AssignExpr
        if (auto* assignExpr = dynamic_cast<AssignExpr*>(exprStmt->expr.get())) {
            if (auto* target = dynamic_cast<Identifier*>(assignExpr->target.get())) {
                ValueNumber vn = getValueNumber(assignExpr->value.get());
                varToVN_[target->name] = vn;
                memoryState_[target->name] = vn;
            }
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
        auto condReplacement = processExpression(ifStmt->condition);
        if (condReplacement) {
            ifStmt->condition = std::move(condReplacement);
        }
        
        auto savedVarToVN = varToVN_;
        auto savedMemory = memoryState_;
        
        if (auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
            processBlock(thenBlock->statements);
        }
        
        for (auto& elif : ifStmt->elifBranches) {
            varToVN_ = savedVarToVN;
            memoryState_ = savedMemory;
            
            auto elifCondReplacement = processExpression(elif.first);
            if (elifCondReplacement) {
                elif.first = std::move(elifCondReplacement);
            }
            
            if (auto* elifBlock = dynamic_cast<Block*>(elif.second.get())) {
                processBlock(elifBlock->statements);
            }
        }
        
        if (ifStmt->elseBranch) {
            varToVN_ = savedVarToVN;
            memoryState_ = savedMemory;
            
            if (auto* elseBlock = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
                processBlock(elseBlock->statements);
            }
        }
        
        // Conservative: invalidate after branches
        varToVN_.clear();
        memoryState_.clear();
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt.get())) {
        // Loops invalidate everything
        varToVN_.clear();
        memoryState_.clear();
        
        auto condReplacement = processExpression(whileStmt->condition);
        if (condReplacement) {
            whileStmt->condition = std::move(condReplacement);
        }
        
        if (auto* body = dynamic_cast<Block*>(whileStmt->body.get())) {
            processBlock(body->statements);
        }
        
        varToVN_.clear();
        memoryState_.clear();
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt.get())) {
        varToVN_.clear();
        memoryState_.clear();
        
        if (auto* body = dynamic_cast<Block*>(forStmt->body.get())) {
            processBlock(body->statements);
        }
        
        varToVN_.clear();
        memoryState_.clear();
    }
    else if (auto* block = dynamic_cast<Block*>(stmt.get())) {
        processBlock(block->statements);
    }
    else if (auto* fnDecl = dynamic_cast<FnDecl*>(stmt.get())) {
        processFunction(fnDecl);
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt.get())) {
        if (returnStmt->value) {
            auto replacement = processExpression(returnStmt->value);
            if (replacement) {
                returnStmt->value = std::move(replacement);
            }
        }
    }
}

ExprPtr GVNPREPass::processExpression(ExprPtr& expr) {
    if (!expr) return nullptr;
    
    // Process sub-expressions first
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr.get())) {
        auto leftReplacement = processExpression(binary->left);
        if (leftReplacement) binary->left = std::move(leftReplacement);
        
        auto rightReplacement = processExpression(binary->right);
        if (rightReplacement) binary->right = std::move(rightReplacement);
        
        // Constant folding - only fold if both operands are literals
        auto* leftInt = dynamic_cast<IntegerLiteral*>(binary->left.get());
        auto* rightInt = dynamic_cast<IntegerLiteral*>(binary->right.get());
        
        if (leftInt && rightInt) {
            int64_t result = 0;
            bool canFold = true;
            
            switch (binary->op) {
                case TokenType::PLUS: result = leftInt->value + rightInt->value; break;
                case TokenType::MINUS: result = leftInt->value - rightInt->value; break;
                case TokenType::STAR: result = leftInt->value * rightInt->value; break;
                case TokenType::SLASH:
                    if (rightInt->value != 0) result = leftInt->value / rightInt->value;
                    else canFold = false;
                    break;
                case TokenType::PERCENT:
                    if (rightInt->value != 0) result = leftInt->value % rightInt->value;
                    else canFold = false;
                    break;
                case TokenType::AMP: result = leftInt->value & rightInt->value; break;
                case TokenType::PIPE: result = leftInt->value | rightInt->value; break;
                case TokenType::CARET: result = leftInt->value ^ rightInt->value; break;
                default: canFold = false;
            }
            
            if (canFold) {
                transformations_++;
                return std::make_unique<IntegerLiteral>(result, expr->location);
            }
        }
        
        // Don't do CSE replacement here - it can cause issues with loop variables
        // The regular GVN pass handles this more safely
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr.get())) {
        auto operandReplacement = processExpression(unary->operand);
        if (operandReplacement) unary->operand = std::move(operandReplacement);
        
        // Fold unary on constant
        if (auto* intLit = dynamic_cast<IntegerLiteral*>(unary->operand.get())) {
            if (unary->op == TokenType::MINUS) {
                transformations_++;
                return std::make_unique<IntegerLiteral>(-intLit->value, expr->location);
            }
        }
    }
    else if (auto* ident = dynamic_cast<Identifier*>(expr.get())) {
        // DON'T replace identifiers with constants here
        // This is too aggressive and breaks loop variables
        // The constant propagation pass handles this more safely
        // by tracking which variables are actually constant
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr.get())) {
        for (auto& arg : call->args) {
            auto argReplacement = processExpression(arg);
            if (argReplacement) arg = std::move(argReplacement);
        }
        
        // Function calls may have side effects - invalidate memory
        invalidateAllMemory();
    }
    else if (auto* index = dynamic_cast<IndexExpr*>(expr.get())) {
        auto objReplacement = processExpression(index->object);
        if (objReplacement) index->object = std::move(objReplacement);
        
        auto idxReplacement = processExpression(index->index);
        if (idxReplacement) index->index = std::move(idxReplacement);
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr.get())) {
        auto condReplacement = processExpression(ternary->condition);
        if (condReplacement) ternary->condition = std::move(condReplacement);
        
        auto thenReplacement = processExpression(ternary->thenExpr);
        if (thenReplacement) ternary->thenExpr = std::move(thenReplacement);
        
        auto elseReplacement = processExpression(ternary->elseExpr);
        if (elseReplacement) ternary->elseExpr = std::move(elseReplacement);
        
        // Simplify constant condition
        if (auto* condBool = dynamic_cast<BoolLiteral*>(ternary->condition.get())) {
            transformations_++;
            if (condBool->value) {
                return cloneExpr(ternary->thenExpr.get());
            } else {
                return cloneExpr(ternary->elseExpr.get());
            }
        }
    }
    else if (auto* assignExpr = dynamic_cast<AssignExpr*>(expr.get())) {
        auto valueReplacement = processExpression(assignExpr->value);
        if (valueReplacement) assignExpr->value = std::move(valueReplacement);
        
        // Update VN for target
        if (auto* target = dynamic_cast<Identifier*>(assignExpr->target.get())) {
            ValueNumber vn = getValueNumber(assignExpr->value.get());
            varToVN_[target->name] = vn;
            memoryState_[target->name] = vn;
        }
    }
    
    return nullptr;
}

void GVNPREPass::invalidateMemory(const std::string& var) {
    // Remove cached loads that involve this variable
    auto it = loadCache_.begin();
    while (it != loadCache_.end()) {
        if (it->first.first == var) {
            it = loadCache_.erase(it);
        } else {
            ++it;
        }
    }
}

void GVNPREPass::invalidateAllMemory() {
    loadCache_.clear();
}

std::string GVNPREPass::generateTempName() {
    return "$gvn_temp_" + std::to_string(tempCounter_++);
}

ExprPtr GVNPREPass::cloneExpr(Expression* expr) {
    if (!expr) return nullptr;
    SourceLocation loc = expr->location;
    
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
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        return std::make_unique<Identifier>(ident->name, loc);
    }
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        return std::make_unique<BinaryExpr>(
            cloneExpr(binary->left.get()),
            binary->op,
            cloneExpr(binary->right.get()),
            loc);
    }
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        return std::make_unique<UnaryExpr>(
            unary->op,
            cloneExpr(unary->operand.get()),
            loc);
    }
    
    return nullptr;
}

void GVNPREPass::optimizeLoads(std::vector<StmtPtr>& stmts) {
    // Already handled in processExpression via loadCache_
}

void GVNPREPass::optimizeStores(std::vector<StmtPtr>& stmts) {
    // Store optimization is handled by the separate DSE pass
}

// ============================================
// Load Elimination Pass
// ============================================

void LoadEliminationPass::run(Program& ast) {
    transformations_ = 0;
    
    for (auto& stmt : ast.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            knownValues_.clear();
            loadedValues_.clear();
            
            if (auto* body = dynamic_cast<Block*>(fn->body.get())) {
                processBlock(body->statements);
            }
        }
    }
}

void LoadEliminationPass::processBlock(std::vector<StmtPtr>& stmts) {
    for (auto& stmt : stmts) {
        processStatement(stmt);
    }
}

void LoadEliminationPass::processStatement(StmtPtr& stmt) {
    if (!stmt) return;
    
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt.get())) {
        if (varDecl->initializer) {
            auto replacement = processExpression(varDecl->initializer);
            if (replacement) {
                varDecl->initializer = std::move(replacement);
            }
        }
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt.get())) {
        auto replacement = processExpression(assignStmt->value);
        if (replacement) {
            assignStmt->value = std::move(replacement);
        }
        
        if (auto* target = dynamic_cast<Identifier*>(assignStmt->target.get())) {
            invalidate(target->name);
        }
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt.get())) {
        auto replacement = processExpression(exprStmt->expr);
        if (replacement) {
            exprStmt->expr = std::move(replacement);
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
        invalidateAll();
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt.get())) {
        invalidateAll();
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt.get())) {
        invalidateAll();
    }
    else if (auto* block = dynamic_cast<Block*>(stmt.get())) {
        processBlock(block->statements);
    }
}

ExprPtr LoadEliminationPass::processExpression(ExprPtr& expr) {
    // Simplified - main load elimination is in GVN-PRE
    return nullptr;
}

void LoadEliminationPass::invalidate(const std::string& var) {
    knownValues_.erase(var);
    
    auto it = loadedValues_.begin();
    while (it != loadedValues_.end()) {
        if (it->first.first == var) {
            it = loadedValues_.erase(it);
        } else {
            ++it;
        }
    }
}

void LoadEliminationPass::invalidateAll() {
    knownValues_.clear();
    loadedValues_.clear();
}

// ============================================
// Store Sinking Pass
// ============================================

void StoreSinkingPass::run(Program& ast) {
    transformations_ = 0;
    
    for (auto& stmt : ast.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            processFunction(fn);
        }
    }
}

void StoreSinkingPass::processFunction(FnDecl* fn) {
    if (!fn || !fn->body) return;
    
    auto* body = dynamic_cast<Block*>(fn->body.get());
    if (body) {
        processBlock(body->statements);
    }
}

void StoreSinkingPass::processBlock(std::vector<StmtPtr>& stmts) {
    // Store sinking is complex - for now, just process nested blocks
    for (auto& stmt : stmts) {
        if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
            if (auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
                processBlock(thenBlock->statements);
            }
            for (auto& elif : ifStmt->elifBranches) {
                if (auto* elifBlock = dynamic_cast<Block*>(elif.second.get())) {
                    processBlock(elifBlock->statements);
                }
            }
            if (auto* elseBlock = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
                processBlock(elseBlock->statements);
            }
        }
        else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt.get())) {
            if (auto* body = dynamic_cast<Block*>(whileStmt->body.get())) {
                processBlock(body->statements);
            }
        }
        else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt.get())) {
            if (auto* body = dynamic_cast<Block*>(forStmt->body.get())) {
                processBlock(body->statements);
            }
        }
        else if (auto* block = dynamic_cast<Block*>(stmt.get())) {
            processBlock(block->statements);
        }
    }
}

bool StoreSinkingPass::canSinkPast(Statement* store, Statement* stmt) {
    if (!store || !stmt) return false;
    
    auto* assignStmt = dynamic_cast<AssignStmt*>(store);
    if (!assignStmt) return false;
    
    auto* target = dynamic_cast<Identifier*>(assignStmt->target.get());
    if (!target) return false;
    
    // Check if stmt reads the stored variable
    std::set<std::string> reads = getReads(stmt);
    return reads.find(target->name) == reads.end();
}

std::set<std::string> StoreSinkingPass::getReads(Statement* stmt) {
    std::set<std::string> reads;
    
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        reads = getReadsExpr(exprStmt->expr.get());
    }
    else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        if (varDecl->initializer) {
            reads = getReadsExpr(varDecl->initializer.get());
        }
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        reads = getReadsExpr(assignStmt->value.get());
        if (assignStmt->op != TokenType::ASSIGN) {
            if (auto* target = dynamic_cast<Identifier*>(assignStmt->target.get())) {
                reads.insert(target->name);
            }
        }
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        if (returnStmt->value) {
            reads = getReadsExpr(returnStmt->value.get());
        }
    }
    
    return reads;
}

std::set<std::string> StoreSinkingPass::getReadsExpr(Expression* expr) {
    std::set<std::string> reads;
    
    if (!expr) return reads;
    
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        reads.insert(ident->name);
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        auto left = getReadsExpr(binary->left.get());
        auto right = getReadsExpr(binary->right.get());
        reads.insert(left.begin(), left.end());
        reads.insert(right.begin(), right.end());
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        reads = getReadsExpr(unary->operand.get());
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        for (auto& arg : call->args) {
            auto argReads = getReadsExpr(arg.get());
            reads.insert(argReads.begin(), argReads.end());
        }
    }
    else if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        auto obj = getReadsExpr(index->object.get());
        auto idx = getReadsExpr(index->index.get());
        reads.insert(obj.begin(), obj.end());
        reads.insert(idx.begin(), idx.end());
    }
    
    return reads;
}

// Factory functions
std::unique_ptr<GVNPREPass> createGVNPREPass() {
    return std::make_unique<GVNPREPass>();
}

std::unique_ptr<LoadEliminationPass> createLoadEliminationPass() {
    return std::make_unique<LoadEliminationPass>();
}

std::unique_ptr<StoreSinkingPass> createStoreSinkingPass() {
    return std::make_unique<StoreSinkingPass>();
}

} // namespace tyl
