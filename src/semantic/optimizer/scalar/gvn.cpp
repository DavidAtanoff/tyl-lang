// Tyl Compiler - Global Value Numbering Implementation
#include "gvn.h"
#include <algorithm>

namespace tyl {

// ============================================
// Global Value Numbering Pass
// ============================================

void GVNPass::run(Program& ast) {
    transformations_ = 0;
    resetState();
    
    // First pass: collect all expressions and assign value numbers
    processBlock(ast.statements);
    
    // Second pass: perform CSE by replacing duplicate expressions
    performCSE(ast.statements);
}

void GVNPass::resetState() {
    nextVN_ = 1;
    exprToVN_.clear();
    varToVN_.clear();
    vnToExpr_.clear();
    vnToConst_.clear();
}

void GVNPass::invalidateVar(const std::string& name) {
    varToVN_.erase(name);
    invalidateExpressionsUsing(name);
}

void GVNPass::invalidateExpressionsUsing(const std::string& var) {
    // Remove expressions from cache that depend on this variable
    std::vector<VNKey> toRemove;
    for (const auto& [key, vn] : exprToVN_) {
        // Check if this expression uses the variable
        // We need to check if the VN corresponds to this variable
        auto it = varToVN_.find(var);
        if (it != varToVN_.end()) {
            if (key.left == it->second || key.right == it->second) {
                toRemove.push_back(key);
            }
        }
        // Also check literal field for identifier references
        if (key.op == TokenType::IDENTIFIER && key.literal == var) {
            toRemove.push_back(key);
        }
    }
    for (const auto& key : toRemove) {
        exprToVN_.erase(key);
    }
}

void GVNPass::collectModifiedVars(Statement* stmt, std::set<std::string>& modified) {
    if (!stmt) return;
    
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        modified.insert(varDecl->name);
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        if (auto* target = dynamic_cast<Identifier*>(assignStmt->target.get())) {
            modified.insert(target->name);
        }
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        if (auto* assign = dynamic_cast<AssignExpr*>(exprStmt->expr.get())) {
            if (auto* target = dynamic_cast<Identifier*>(assign->target.get())) {
                modified.insert(target->name);
            }
        }
    }
    else if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            collectModifiedVars(s.get(), modified);
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        collectModifiedVars(ifStmt->thenBranch.get(), modified);
        for (auto& elif : ifStmt->elifBranches) {
            collectModifiedVars(elif.second.get(), modified);
        }
        collectModifiedVars(ifStmt->elseBranch.get(), modified);
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        collectModifiedVars(whileStmt->body.get(), modified);
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
        modified.insert(forStmt->var);
        collectModifiedVars(forStmt->body.get(), modified);
    }
}

ValueNumber GVNPass::getValueNumber(Expression* expr) {
    if (!expr) return INVALID_VN;
    
    VNKey key = makeKey(expr);
    
    // Check if we've seen this expression before
    auto it = exprToVN_.find(key);
    if (it != exprToVN_.end()) {
        return it->second;
    }
    
    // Assign new value number
    ValueNumber vn = nextVN_++;
    exprToVN_[key] = vn;
    
    // Track constant values
    if (auto* intLit = dynamic_cast<IntegerLiteral*>(expr)) {
        vnToConst_[vn] = intLit->value;
    }
    
    return vn;
}

VNKey GVNPass::makeKey(Expression* expr) {
    VNKey key;
    key.op = TokenType::ERROR;  // Use ERROR as invalid marker
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
        key.op = TokenType::TRUE;  // Use TRUE for bool literals
        key.literal = boolLit->value ? "true" : "false";
    }
    else if (auto* strLit = dynamic_cast<StringLiteral*>(expr)) {
        key.op = TokenType::STRING;
        key.literal = strLit->value;
    }
    else if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        // Look up variable's current value number
        auto it = varToVN_.find(ident->name);
        if (it != varToVN_.end()) {
            key.op = TokenType::IDENTIFIER;
            key.left = it->second;
            key.literal = "";  // Use VN, not name
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
                             binary->op == TokenType::OR);
        
        if (isCommutative && key.left > key.right) {
            std::swap(key.left, key.right);
        }
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        key.op = unary->op;
        key.left = getValueNumber(unary->operand.get());
    }
    else if (auto* walrus = dynamic_cast<WalrusExpr*>(expr)) {
        // Walrus expression - get value number of the value
        key.op = TokenType::WALRUS;
        key.left = getValueNumber(walrus->value.get());
        key.literal = walrus->varName;
    }
    
    return key;
}

void GVNPass::processBlock(std::vector<StmtPtr>& statements) {
    for (auto& stmt : statements) {
        processStatement(stmt);
    }
}

void GVNPass::processStatement(StmtPtr& stmt) {
    if (!stmt) return;
    
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt.get())) {
        if (varDecl->initializer) {
            auto replacement = processExpression(varDecl->initializer);
            if (replacement) {
                varDecl->initializer = std::move(replacement);
            }
            
            // Assign value number to variable
            ValueNumber vn = getValueNumber(varDecl->initializer.get());
            varToVN_[varDecl->name] = vn;
        }
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt.get())) {
        auto replacement = processExpression(assignStmt->value);
        if (replacement) {
            assignStmt->value = std::move(replacement);
        }
        
        if (auto* target = dynamic_cast<Identifier*>(assignStmt->target.get())) {
            // Update variable's value number
            ValueNumber vn = getValueNumber(assignStmt->value.get());
            varToVN_[target->name] = vn;
        }
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt.get())) {
        auto replacement = processExpression(exprStmt->expr);
        if (replacement) {
            exprStmt->expr = std::move(replacement);
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
        auto condReplacement = processExpression(ifStmt->condition);
        if (condReplacement) {
            ifStmt->condition = std::move(condReplacement);
        }
        
        // Save state for branches
        auto savedVarToVN = varToVN_;
        auto savedExprToVN = exprToVN_;
        
        // Track variables modified in each branch
        std::set<std::string> thenModified;
        std::set<std::string> elseModified;
        
        if (auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
            processBlock(thenBlock->statements);
            // Find what was modified in then branch
            for (const auto& [var, vn] : varToVN_) {
                auto it = savedVarToVN.find(var);
                if (it == savedVarToVN.end() || it->second != vn) {
                    thenModified.insert(var);
                }
            }
        } else {
            processStatement(ifStmt->thenBranch);
        }
        
        for (auto& elif : ifStmt->elifBranches) {
            varToVN_ = savedVarToVN;
            exprToVN_ = savedExprToVN;
            auto elifCondReplacement = processExpression(elif.first);
            if (elifCondReplacement) {
                elif.first = std::move(elifCondReplacement);
            }
            if (auto* elifBlock = dynamic_cast<Block*>(elif.second.get())) {
                processBlock(elifBlock->statements);
            } else {
                processStatement(elif.second);
            }
        }
        
        if (ifStmt->elseBranch) {
            varToVN_ = savedVarToVN;
            exprToVN_ = savedExprToVN;
            if (auto* elseBlock = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
                processBlock(elseBlock->statements);
                // Find what was modified in else branch
                for (const auto& [var, vn] : varToVN_) {
                    auto it = savedVarToVN.find(var);
                    if (it == savedVarToVN.end() || it->second != vn) {
                        elseModified.insert(var);
                    }
                }
            } else {
                processStatement(ifStmt->elseBranch);
            }
        }
        
        // After if, only invalidate variables that were modified in any branch
        // Keep VNs for variables that weren't modified
        varToVN_ = savedVarToVN;
        exprToVN_ = savedExprToVN;
        
        // Invalidate variables modified in any branch
        std::set<std::string> allModified;
        allModified.insert(thenModified.begin(), thenModified.end());
        allModified.insert(elseModified.begin(), elseModified.end());
        
        for (const auto& var : allModified) {
            varToVN_.erase(var);
            // Also invalidate expressions that use this variable
            invalidateExpressionsUsing(var);
        }
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt.get())) {
        // For loops, we need to be more careful
        // First, find all variables modified in the loop
        std::set<std::string> loopModified;
        collectModifiedVars(whileStmt->body.get(), loopModified);
        
        // Invalidate only those variables before processing
        for (const auto& var : loopModified) {
            varToVN_.erase(var);
            invalidateExpressionsUsing(var);
        }
        
        auto condReplacement = processExpression(whileStmt->condition);
        if (condReplacement) {
            whileStmt->condition = std::move(condReplacement);
        }
        
        if (auto* body = dynamic_cast<Block*>(whileStmt->body.get())) {
            processBlock(body->statements);
        } else {
            processStatement(whileStmt->body);
        }
        
        // After loop, invalidate loop-modified variables again
        for (const auto& var : loopModified) {
            varToVN_.erase(var);
            invalidateExpressionsUsing(var);
        }
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt.get())) {
        // Find all variables modified in the loop
        std::set<std::string> loopModified;
        loopModified.insert(forStmt->var);  // Loop variable is always modified
        collectModifiedVars(forStmt->body.get(), loopModified);
        
        // Invalidate those variables
        for (const auto& var : loopModified) {
            varToVN_.erase(var);
            invalidateExpressionsUsing(var);
        }
        
        if (auto* body = dynamic_cast<Block*>(forStmt->body.get())) {
            processBlock(body->statements);
        } else {
            processStatement(forStmt->body);
        }
        
        // After loop, invalidate again
        for (const auto& var : loopModified) {
            varToVN_.erase(var);
            invalidateExpressionsUsing(var);
        }
    }
    else if (auto* block = dynamic_cast<Block*>(stmt.get())) {
        processBlock(block->statements);
    }
    else if (auto* fnDecl = dynamic_cast<FnDecl*>(stmt.get())) {
        auto savedState = varToVN_;
        varToVN_.clear();
        
        // Add parameters
        for (auto& param : fnDecl->params) {
            varToVN_[param.first] = nextVN_++;
        }
        
        if (auto* body = dynamic_cast<Block*>(fnDecl->body.get())) {
            processBlock(body->statements);
        } else {
            processStatement(fnDecl->body);
        }
        
        varToVN_ = savedState;
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

ExprPtr GVNPass::processExpression(ExprPtr& expr) {
    if (!expr) return nullptr;
    
    // Process sub-expressions first
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr.get())) {
        auto leftReplacement = processExpression(binary->left);
        if (leftReplacement) binary->left = std::move(leftReplacement);
        
        auto rightReplacement = processExpression(binary->right);
        if (rightReplacement) binary->right = std::move(rightReplacement);
        
        // Check if both operands are now constants
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
                    if (rightInt->value != 0) {
                        result = leftInt->value / rightInt->value;
                    } else {
                        canFold = false;
                    }
                    break;
                case TokenType::PERCENT:
                    if (rightInt->value != 0) {
                        result = leftInt->value % rightInt->value;
                    } else {
                        canFold = false;
                    }
                    break;
                default:
                    canFold = false;
            }
            
            if (canFold) {
                transformations_++;
                return std::make_unique<IntegerLiteral>(result, expr->location);
            }
        }
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr.get())) {
        auto operandReplacement = processExpression(unary->operand);
        if (operandReplacement) unary->operand = std::move(operandReplacement);
    }
    else if (auto* ident = dynamic_cast<Identifier*>(expr.get())) {
        // Check if variable has a known constant value
        auto vnIt = varToVN_.find(ident->name);
        if (vnIt != varToVN_.end()) {
            auto constIt = vnToConst_.find(vnIt->second);
            if (constIt != vnToConst_.end()) {
                transformations_++;
                return std::make_unique<IntegerLiteral>(constIt->second, expr->location);
            }
        }
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr.get())) {
        for (auto& arg : call->args) {
            auto argReplacement = processExpression(arg);
            if (argReplacement) arg = std::move(argReplacement);
        }
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr.get())) {
        auto condReplacement = processExpression(ternary->condition);
        if (condReplacement) ternary->condition = std::move(condReplacement);
        
        auto thenReplacement = processExpression(ternary->thenExpr);
        if (thenReplacement) ternary->thenExpr = std::move(thenReplacement);
        
        auto elseReplacement = processExpression(ternary->elseExpr);
        if (elseReplacement) ternary->elseExpr = std::move(elseReplacement);
    }
    else if (auto* walrus = dynamic_cast<WalrusExpr*>(expr.get())) {
        auto valueReplacement = processExpression(walrus->value);
        if (valueReplacement) walrus->value = std::move(valueReplacement);
    }
    
    return nullptr;
}

// ============================================
// CSE (Common Subexpression Elimination)
// ============================================

void GVNPass::performCSE(std::vector<StmtPtr>& statements) {
    for (auto& stmt : statements) {
        performCSEOnStatement(stmt);
    }
}

void GVNPass::performCSEOnStatement(StmtPtr& stmt) {
    if (!stmt) return;
    
    if (auto* fnDecl = dynamic_cast<FnDecl*>(stmt.get())) {
        if (auto* body = dynamic_cast<Block*>(fnDecl->body.get())) {
            performCSEOnBlock(body->statements);
        }
    }
    else if (auto* block = dynamic_cast<Block*>(stmt.get())) {
        performCSEOnBlock(block->statements);
    }
}

void GVNPass::performCSEOnBlock(std::vector<StmtPtr>& statements) {
    // Map: expression signature -> (temp var name, defining statement index)
    std::map<std::string, std::pair<std::string, size_t>> exprToTemp;
    std::set<std::string> modifiedVars;
    
    // First pass: identify duplicate expressions
    std::vector<std::pair<size_t, std::string>> duplicates;  // (stmt index, expr signature)
    
    for (size_t i = 0; i < statements.size(); ++i) {
        auto* stmt = statements[i].get();
        
        // Track variable modifications
        if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
            modifiedVars.insert(varDecl->name);
            
            if (varDecl->initializer) {
                std::string sig = getExprSignature(varDecl->initializer.get());
                if (!sig.empty() && isCSECandidate(varDecl->initializer.get())) {
                    auto it = exprToTemp.find(sig);
                    if (it != exprToTemp.end()) {
                        // Found duplicate - check if variables used haven't been modified
                        if (!exprUsesModifiedVars(varDecl->initializer.get(), modifiedVars, it->second.second)) {
                            duplicates.push_back({i, sig});
                        }
                    } else {
                        // First occurrence - record it
                        exprToTemp[sig] = {varDecl->name, i};
                    }
                }
            }
        }
        else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
            if (auto* target = dynamic_cast<Identifier*>(assignStmt->target.get())) {
                modifiedVars.insert(target->name);
            }
        }
        else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
            if (auto* assign = dynamic_cast<AssignExpr*>(exprStmt->expr.get())) {
                if (auto* target = dynamic_cast<Identifier*>(assign->target.get())) {
                    modifiedVars.insert(target->name);
                }
            }
        }
        else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
            // Process nested blocks
            if (auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
                performCSEOnBlock(thenBlock->statements);
            }
            for (auto& elif : ifStmt->elifBranches) {
                if (auto* elifBlock = dynamic_cast<Block*>(elif.second.get())) {
                    performCSEOnBlock(elifBlock->statements);
                }
            }
            if (auto* elseBlock = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
                performCSEOnBlock(elseBlock->statements);
            }
            // Conservatively invalidate all expressions after control flow
            exprToTemp.clear();
        }
        else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
            modifiedVars.insert(forStmt->var);
            if (auto* body = dynamic_cast<Block*>(forStmt->body.get())) {
                performCSEOnBlock(body->statements);
            }
            exprToTemp.clear();
        }
        else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
            if (auto* body = dynamic_cast<Block*>(whileStmt->body.get())) {
                performCSEOnBlock(body->statements);
            }
            exprToTemp.clear();
        }
    }
    
    // Second pass: replace duplicates with references to first occurrence
    for (auto& [stmtIdx, sig] : duplicates) {
        auto it = exprToTemp.find(sig);
        if (it != exprToTemp.end()) {
            auto* varDecl = dynamic_cast<VarDecl*>(statements[stmtIdx].get());
            if (varDecl && varDecl->initializer) {
                // Replace the expression with a reference to the first variable
                varDecl->initializer = std::make_unique<Identifier>(
                    it->second.first, varDecl->initializer->location);
                transformations_++;
            }
        }
    }
}

std::string GVNPass::getExprSignature(Expression* expr) {
    if (!expr) return "";
    
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        std::string left = getExprSignature(binary->left.get());
        std::string right = getExprSignature(binary->right.get());
        if (left.empty() || right.empty()) return "";
        
        // For commutative ops, normalize order
        bool isCommutative = (binary->op == TokenType::PLUS || 
                             binary->op == TokenType::STAR ||
                             binary->op == TokenType::EQ ||
                             binary->op == TokenType::NE);
        if (isCommutative && left > right) {
            std::swap(left, right);
        }
        
        return "(" + left + " " + std::to_string(static_cast<int>(binary->op)) + " " + right + ")";
    }
    else if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        return "var:" + ident->name;
    }
    else if (auto* intLit = dynamic_cast<IntegerLiteral*>(expr)) {
        return "int:" + std::to_string(intLit->value);
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        std::string operand = getExprSignature(unary->operand.get());
        if (operand.empty()) return "";
        return "unary:" + std::to_string(static_cast<int>(unary->op)) + ":" + operand;
    }
    
    return "";
}

bool GVNPass::isCSECandidate(Expression* expr) {
    // Only consider binary expressions with non-trivial computation
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        // Skip simple operations that are cheaper to recompute
        if (binary->op == TokenType::PLUS || binary->op == TokenType::MINUS) {
            // Only CSE if operands are complex
            bool leftComplex = dynamic_cast<BinaryExpr*>(binary->left.get()) != nullptr;
            bool rightComplex = dynamic_cast<BinaryExpr*>(binary->right.get()) != nullptr;
            return leftComplex || rightComplex;
        }
        // Always CSE multiplication, division, modulo
        return binary->op == TokenType::STAR || 
               binary->op == TokenType::SLASH ||
               binary->op == TokenType::PERCENT;
    }
    return false;
}

bool GVNPass::exprUsesModifiedVars(Expression* expr, const std::set<std::string>& modified, size_t sinceIdx) {
    if (!expr) return false;
    
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        return modified.count(ident->name) > 0;
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        return exprUsesModifiedVars(binary->left.get(), modified, sinceIdx) ||
               exprUsesModifiedVars(binary->right.get(), modified, sinceIdx);
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        return exprUsesModifiedVars(unary->operand.get(), modified, sinceIdx);
    }
    
    return false;
}

// ============================================
// Copy Propagation Pass
// ============================================

void CopyPropagationPass::run(Program& ast) {
    transformations_ = 0;
    copies_.clear();
    constants_.clear();
    modified_.clear();
    processBlock(ast.statements);
}

void CopyPropagationPass::processBlock(std::vector<StmtPtr>& statements) {
    for (auto& stmt : statements) {
        processStatement(stmt);
    }
}

std::string CopyPropagationPass::getUltimateSource(const std::string& var) {
    std::string current = var;
    std::set<std::string> visited;
    
    while (copies_.count(current) && !visited.count(current)) {
        visited.insert(current);
        current = copies_[current];
    }
    
    return current;
}

void CopyPropagationPass::invalidateCopies(const std::string& var) {
    // Remove copies where var is the source
    std::vector<std::string> toRemove;
    for (const auto& [dest, src] : copies_) {
        if (src == var) {
            toRemove.push_back(dest);
        }
    }
    for (const auto& dest : toRemove) {
        copies_.erase(dest);
    }
    
    // Remove var as a destination
    copies_.erase(var);
    constants_.erase(var);
    modified_.insert(var);
}

void CopyPropagationPass::processStatement(StmtPtr& stmt) {
    if (!stmt) return;
    
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt.get())) {
        if (varDecl->initializer) {
            auto replacement = processExpression(varDecl->initializer);
            if (replacement) {
                varDecl->initializer = std::move(replacement);
            }
            
            // Track copy or constant
            if (auto* srcIdent = dynamic_cast<Identifier*>(varDecl->initializer.get())) {
                copies_[varDecl->name] = srcIdent->name;
            }
            else if (auto* intLit = dynamic_cast<IntegerLiteral*>(varDecl->initializer.get())) {
                constants_[varDecl->name] = intLit->value;
            }
        }
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt.get())) {
        auto replacement = processExpression(assignStmt->value);
        if (replacement) {
            assignStmt->value = std::move(replacement);
        }
        
        if (auto* target = dynamic_cast<Identifier*>(assignStmt->target.get())) {
            invalidateCopies(target->name);
            
            // Track new copy or constant
            if (assignStmt->op == TokenType::ASSIGN) {
                if (auto* srcIdent = dynamic_cast<Identifier*>(assignStmt->value.get())) {
                    copies_[target->name] = srcIdent->name;
                }
                else if (auto* intLit = dynamic_cast<IntegerLiteral*>(assignStmt->value.get())) {
                    constants_[target->name] = intLit->value;
                }
            }
        }
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt.get())) {
        auto replacement = processExpression(exprStmt->expr);
        if (replacement) {
            exprStmt->expr = std::move(replacement);
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
        auto condReplacement = processExpression(ifStmt->condition);
        if (condReplacement) {
            ifStmt->condition = std::move(condReplacement);
        }
        
        auto savedCopies = copies_;
        auto savedConstants = constants_;
        
        if (auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
            processBlock(thenBlock->statements);
        } else {
            processStatement(ifStmt->thenBranch);
        }
        
        for (auto& elif : ifStmt->elifBranches) {
            copies_ = savedCopies;
            constants_ = savedConstants;
            auto elifCondReplacement = processExpression(elif.first);
            if (elifCondReplacement) {
                elif.first = std::move(elifCondReplacement);
            }
            if (auto* elifBlock = dynamic_cast<Block*>(elif.second.get())) {
                processBlock(elifBlock->statements);
            } else {
                processStatement(elif.second);
            }
        }
        
        if (ifStmt->elseBranch) {
            copies_ = savedCopies;
            constants_ = savedConstants;
            if (auto* elseBlock = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
                processBlock(elseBlock->statements);
            } else {
                processStatement(ifStmt->elseBranch);
            }
        }
        
        // Conservative: clear after if
        copies_.clear();
        constants_.clear();
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt.get())) {
        copies_.clear();
        constants_.clear();
        
        auto condReplacement = processExpression(whileStmt->condition);
        if (condReplacement) {
            whileStmt->condition = std::move(condReplacement);
        }
        
        if (auto* body = dynamic_cast<Block*>(whileStmt->body.get())) {
            processBlock(body->statements);
        } else {
            processStatement(whileStmt->body);
        }
        
        copies_.clear();
        constants_.clear();
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt.get())) {
        copies_.clear();
        constants_.clear();
        
        if (auto* body = dynamic_cast<Block*>(forStmt->body.get())) {
            processBlock(body->statements);
        } else {
            processStatement(forStmt->body);
        }
        
        copies_.clear();
        constants_.clear();
    }
    else if (auto* block = dynamic_cast<Block*>(stmt.get())) {
        processBlock(block->statements);
    }
    else if (auto* fnDecl = dynamic_cast<FnDecl*>(stmt.get())) {
        auto savedCopies = copies_;
        auto savedConstants = constants_;
        copies_.clear();
        constants_.clear();
        
        if (auto* body = dynamic_cast<Block*>(fnDecl->body.get())) {
            processBlock(body->statements);
        } else {
            processStatement(fnDecl->body);
        }
        
        copies_ = savedCopies;
        constants_ = savedConstants;
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

ExprPtr CopyPropagationPass::processExpression(ExprPtr& expr) {
    if (!expr) return nullptr;
    
    if (auto* ident = dynamic_cast<Identifier*>(expr.get())) {
        // Check for constant
        auto constIt = constants_.find(ident->name);
        if (constIt != constants_.end()) {
            transformations_++;
            return std::make_unique<IntegerLiteral>(constIt->second, expr->location);
        }
        
        // Check for copy chain
        std::string ultimate = getUltimateSource(ident->name);
        if (ultimate != ident->name && !modified_.count(ultimate)) {
            // Check if ultimate source has a constant
            auto ultimateConstIt = constants_.find(ultimate);
            if (ultimateConstIt != constants_.end()) {
                transformations_++;
                return std::make_unique<IntegerLiteral>(ultimateConstIt->second, expr->location);
            }
            
            // Replace with ultimate source
            transformations_++;
            return std::make_unique<Identifier>(ultimate, expr->location);
        }
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr.get())) {
        auto leftReplacement = processExpression(binary->left);
        if (leftReplacement) binary->left = std::move(leftReplacement);
        
        auto rightReplacement = processExpression(binary->right);
        if (rightReplacement) binary->right = std::move(rightReplacement);
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr.get())) {
        auto operandReplacement = processExpression(unary->operand);
        if (operandReplacement) unary->operand = std::move(operandReplacement);
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr.get())) {
        for (auto& arg : call->args) {
            auto argReplacement = processExpression(arg);
            if (argReplacement) arg = std::move(argReplacement);
        }
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr.get())) {
        auto condReplacement = processExpression(ternary->condition);
        if (condReplacement) ternary->condition = std::move(condReplacement);
        
        auto thenReplacement = processExpression(ternary->thenExpr);
        if (thenReplacement) ternary->thenExpr = std::move(thenReplacement);
        
        auto elseReplacement = processExpression(ternary->elseExpr);
        if (elseReplacement) ternary->elseExpr = std::move(elseReplacement);
    }
    else if (auto* walrus = dynamic_cast<WalrusExpr*>(expr.get())) {
        auto valueReplacement = processExpression(walrus->value);
        if (valueReplacement) walrus->value = std::move(valueReplacement);
    }
    
    return nullptr;
}

} // namespace tyl
