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
    processBlock(ast.statements);
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
        
        if (auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
            processBlock(thenBlock->statements);
        } else {
            processStatement(ifStmt->thenBranch);
        }
        
        for (auto& elif : ifStmt->elifBranches) {
            varToVN_ = savedVarToVN;
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
            if (auto* elseBlock = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
                processBlock(elseBlock->statements);
            } else {
                processStatement(ifStmt->elseBranch);
            }
        }
        
        // After if, conservatively clear variable VNs
        varToVN_.clear();
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt.get())) {
        varToVN_.clear();  // Loops invalidate all
        
        auto condReplacement = processExpression(whileStmt->condition);
        if (condReplacement) {
            whileStmt->condition = std::move(condReplacement);
        }
        
        if (auto* body = dynamic_cast<Block*>(whileStmt->body.get())) {
            processBlock(body->statements);
        } else {
            processStatement(whileStmt->body);
        }
        
        varToVN_.clear();
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt.get())) {
        varToVN_.clear();
        
        if (auto* body = dynamic_cast<Block*>(forStmt->body.get())) {
            processBlock(body->statements);
        } else {
            processStatement(forStmt->body);
        }
        
        varToVN_.clear();
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
    
    return nullptr;
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
    
    return nullptr;
}

} // namespace tyl
