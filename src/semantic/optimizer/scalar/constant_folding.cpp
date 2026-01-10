// Tyl Compiler - Constant Folding Implementation
#include "constant_folding.h"
#include <cmath>
#include <limits>

namespace tyl {

void ConstantFoldingPass::run(Program& ast) {
    transformations_ = 0;
    processBlock(ast.statements);
}

void ConstantFoldingPass::processBlock(std::vector<StmtPtr>& statements) {
    for (auto& stmt : statements) {
        processStatement(stmt);
    }
}

void ConstantFoldingPass::processStatement(StmtPtr& stmt) {
    if (!stmt) return;
    
    // Handle different statement types
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt.get())) {
        if (varDecl->initializer) {
            auto folded = foldExpression(varDecl->initializer);
            if (folded) {
                varDecl->initializer = std::move(folded);
            }
        }
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt.get())) {
        if (exprStmt->expr) {
            auto folded = foldExpression(exprStmt->expr);
            if (folded) {
                exprStmt->expr = std::move(folded);
            }
        }
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt.get())) {
        if (assignStmt->value) {
            auto folded = foldExpression(assignStmt->value);
            if (folded) {
                assignStmt->value = std::move(folded);
            }
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
        // Fold condition
        if (ifStmt->condition) {
            auto folded = foldExpression(ifStmt->condition);
            if (folded) {
                ifStmt->condition = std::move(folded);
            }
        }
        // Process branches
        processStatement(ifStmt->thenBranch);
        for (auto& elif : ifStmt->elifBranches) {
            if (elif.first) {
                auto folded = foldExpression(elif.first);
                if (folded) {
                    elif.first = std::move(folded);
                }
            }
            processStatement(elif.second);
        }
        processStatement(ifStmt->elseBranch);
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt.get())) {
        if (whileStmt->condition) {
            auto folded = foldExpression(whileStmt->condition);
            if (folded) {
                whileStmt->condition = std::move(folded);
            }
        }
        processStatement(whileStmt->body);
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt.get())) {
        if (forStmt->iterable) {
            auto folded = foldExpression(forStmt->iterable);
            if (folded) {
                forStmt->iterable = std::move(folded);
            }
        }
        processStatement(forStmt->body);
    }
    else if (auto* matchStmt = dynamic_cast<MatchStmt*>(stmt.get())) {
        if (matchStmt->value) {
            auto folded = foldExpression(matchStmt->value);
            if (folded) {
                matchStmt->value = std::move(folded);
            }
        }
        for (auto& case_ : matchStmt->cases) {
            if (case_.pattern) {
                auto folded = foldExpression(case_.pattern);
                if (folded) {
                    case_.pattern = std::move(folded);
                }
            }
            if (case_.guard) {
                auto folded = foldExpression(case_.guard);
                if (folded) {
                    case_.guard = std::move(folded);
                }
            }
            processStatement(case_.body);
        }
        processStatement(matchStmt->defaultCase);
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt.get())) {
        if (returnStmt->value) {
            auto folded = foldExpression(returnStmt->value);
            if (folded) {
                returnStmt->value = std::move(folded);
            }
        }
    }
    else if (auto* block = dynamic_cast<Block*>(stmt.get())) {
        processBlock(block->statements);
    }
    else if (auto* fnDecl = dynamic_cast<FnDecl*>(stmt.get())) {
        processStatement(fnDecl->body);
    }
    else if (auto* moduleDecl = dynamic_cast<ModuleDecl*>(stmt.get())) {
        processBlock(moduleDecl->body);
    }
    else if (auto* ifLetStmt = dynamic_cast<IfLetStmt*>(stmt.get())) {
        // Fold value expression
        if (ifLetStmt->value) {
            auto folded = foldExpression(ifLetStmt->value);
            if (folded) {
                ifLetStmt->value = std::move(folded);
            }
        }
        // Fold guard if present
        if (ifLetStmt->guard) {
            auto folded = foldExpression(ifLetStmt->guard);
            if (folded) {
                ifLetStmt->guard = std::move(folded);
            }
        }
        // Process branches
        processStatement(ifLetStmt->thenBranch);
        processStatement(ifLetStmt->elseBranch);
    }
}

ExprPtr ConstantFoldingPass::foldExpression(ExprPtr& expr) {
    if (!expr) return nullptr;
    
    // First, recursively fold sub-expressions
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr.get())) {
        auto leftFolded = foldExpression(binary->left);
        if (leftFolded) binary->left = std::move(leftFolded);
        
        auto rightFolded = foldExpression(binary->right);
        if (rightFolded) binary->right = std::move(rightFolded);
        
        // Now try to fold this binary expression
        auto leftVal = tryEvaluate(binary->left.get());
        auto rightVal = tryEvaluate(binary->right.get());
        
        if (leftVal && rightVal) {
            auto result = foldBinary(binary->op, *leftVal, *rightVal);
            if (result) {
                transformations_++;
                return createLiteral(*result, binary->location);
            }
        }
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr.get())) {
        auto operandFolded = foldExpression(unary->operand);
        if (operandFolded) unary->operand = std::move(operandFolded);
        
        auto operandVal = tryEvaluate(unary->operand.get());
        if (operandVal) {
            auto result = foldUnary(unary->op, *operandVal);
            if (result) {
                transformations_++;
                return createLiteral(*result, unary->location);
            }
        }
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr.get())) {
        auto condFolded = foldExpression(ternary->condition);
        if (condFolded) ternary->condition = std::move(condFolded);
        
        auto thenFolded = foldExpression(ternary->thenExpr);
        if (thenFolded) ternary->thenExpr = std::move(thenFolded);
        
        auto elseFolded = foldExpression(ternary->elseExpr);
        if (elseFolded) ternary->elseExpr = std::move(elseFolded);
        
        // If condition is constant, we can eliminate the ternary
        auto condVal = tryEvaluate(ternary->condition.get());
        if (condVal && std::holds_alternative<bool>(*condVal)) {
            transformations_++;
            if (std::get<bool>(*condVal)) {
                return std::move(ternary->thenExpr);
            } else {
                return std::move(ternary->elseExpr);
            }
        }
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr.get())) {
        // Fold arguments
        for (auto& arg : call->args) {
            auto folded = foldExpression(arg);
            if (folded) arg = std::move(folded);
        }
        for (auto& namedArg : call->namedArgs) {
            auto folded = foldExpression(namedArg.second);
            if (folded) namedArg.second = std::move(folded);
        }
    }
    else if (auto* list = dynamic_cast<ListExpr*>(expr.get())) {
        for (auto& elem : list->elements) {
            auto folded = foldExpression(elem);
            if (folded) elem = std::move(folded);
        }
    }
    else if (auto* record = dynamic_cast<RecordExpr*>(expr.get())) {
        for (auto& field : record->fields) {
            auto folded = foldExpression(field.second);
            if (folded) field.second = std::move(folded);
        }
    }
    else if (auto* map = dynamic_cast<MapExpr*>(expr.get())) {
        for (auto& entry : map->entries) {
            auto keyFolded = foldExpression(entry.first);
            if (keyFolded) entry.first = std::move(keyFolded);
            auto valFolded = foldExpression(entry.second);
            if (valFolded) entry.second = std::move(valFolded);
        }
    }
    else if (auto* index = dynamic_cast<IndexExpr*>(expr.get())) {
        auto objFolded = foldExpression(index->object);
        if (objFolded) index->object = std::move(objFolded);
        
        auto idxFolded = foldExpression(index->index);
        if (idxFolded) index->index = std::move(idxFolded);
    }
    else if (auto* member = dynamic_cast<MemberExpr*>(expr.get())) {
        auto objFolded = foldExpression(member->object);
        if (objFolded) member->object = std::move(objFolded);
    }
    
    return nullptr;  // No replacement needed
}

std::optional<ConstValue> ConstantFoldingPass::tryEvaluate(Expression* expr) {
    if (!expr) return std::nullopt;
    
    if (auto* intLit = dynamic_cast<IntegerLiteral*>(expr)) {
        return ConstValue{intLit->value};
    }
    if (auto* floatLit = dynamic_cast<FloatLiteral*>(expr)) {
        return ConstValue{floatLit->value};
    }
    if (auto* boolLit = dynamic_cast<BoolLiteral*>(expr)) {
        return ConstValue{boolLit->value};
    }
    if (auto* strLit = dynamic_cast<StringLiteral*>(expr)) {
        return ConstValue{strLit->value};
    }
    
    return std::nullopt;
}

std::optional<ConstValue> ConstantFoldingPass::foldBinary(TokenType op, const ConstValue& left, const ConstValue& right) {
    // Integer operations
    if (std::holds_alternative<int64_t>(left) && std::holds_alternative<int64_t>(right)) {
        int64_t l = std::get<int64_t>(left);
        int64_t r = std::get<int64_t>(right);
        
        switch (op) {
            case TokenType::PLUS:  return ConstValue{l + r};
            case TokenType::MINUS: return ConstValue{l - r};
            case TokenType::STAR:  return ConstValue{l * r};
            case TokenType::SLASH: 
                if (r != 0) return ConstValue{l / r};
                return std::nullopt;  // Don't fold division by zero
            case TokenType::PERCENT:
                if (r != 0) return ConstValue{l % r};
                return std::nullopt;
            case TokenType::EQ:  return ConstValue{l == r};
            case TokenType::NE:  return ConstValue{l != r};
            case TokenType::LT:  return ConstValue{l < r};
            case TokenType::GT:  return ConstValue{l > r};
            case TokenType::LE:  return ConstValue{l <= r};
            case TokenType::GE:  return ConstValue{l >= r};
            case TokenType::AMP: return ConstValue{l & r};
            case TokenType::PIPE: return ConstValue{l | r};
            case TokenType::CARET: return ConstValue{l ^ r};
            default: break;
        }
    }
    
    // Float operations - now enabled with native float support
    if ((std::holds_alternative<double>(left) || std::holds_alternative<int64_t>(left)) &&
        (std::holds_alternative<double>(right) || std::holds_alternative<int64_t>(right))) {
        
        double l = std::holds_alternative<double>(left) ? std::get<double>(left) : static_cast<double>(std::get<int64_t>(left));
        double r = std::holds_alternative<double>(right) ? std::get<double>(right) : static_cast<double>(std::get<int64_t>(right));
        
        // Only fold if at least one is a float (otherwise use integer ops above)
        if (std::holds_alternative<double>(left) || std::holds_alternative<double>(right)) {
            switch (op) {
                case TokenType::PLUS:  return ConstValue{l + r};
                case TokenType::MINUS: return ConstValue{l - r};
                case TokenType::STAR:  return ConstValue{l * r};
                case TokenType::SLASH:
                    if (r != 0.0) return ConstValue{l / r};
                    return std::nullopt;
                case TokenType::EQ:  return ConstValue{l == r};
                case TokenType::NE:  return ConstValue{l != r};
                case TokenType::LT:  return ConstValue{l < r};
                case TokenType::GT:  return ConstValue{l > r};
                case TokenType::LE:  return ConstValue{l <= r};
                case TokenType::GE:  return ConstValue{l >= r};
                default: break;
            }
        }
    }
    
    // Boolean operations
    if (std::holds_alternative<bool>(left) && std::holds_alternative<bool>(right)) {
        bool l = std::get<bool>(left);
        bool r = std::get<bool>(right);
        
        switch (op) {
            case TokenType::AND:
            case TokenType::AMP_AMP: return ConstValue{l && r};
            case TokenType::OR:
            case TokenType::PIPE_PIPE: return ConstValue{l || r};
            case TokenType::EQ: return ConstValue{l == r};
            case TokenType::NE: return ConstValue{l != r};
            default: break;
        }
    }
    
    // String concatenation
    if (std::holds_alternative<std::string>(left) && std::holds_alternative<std::string>(right)) {
        const std::string& l = std::get<std::string>(left);
        const std::string& r = std::get<std::string>(right);
        
        switch (op) {
            case TokenType::PLUS: return ConstValue{l + r};
            case TokenType::EQ: return ConstValue{l == r};
            case TokenType::NE: return ConstValue{l != r};
            default: break;
        }
    }
    
    return std::nullopt;
}

std::optional<ConstValue> ConstantFoldingPass::foldUnary(TokenType op, const ConstValue& operand) {
    // Integer unary
    if (std::holds_alternative<int64_t>(operand)) {
        int64_t v = std::get<int64_t>(operand);
        switch (op) {
            case TokenType::MINUS: return ConstValue{-v};
            case TokenType::TILDE: return ConstValue{~v};
            default: break;
        }
    }
    
    // Float unary - now enabled with native float support
    if (std::holds_alternative<double>(operand)) {
        double v = std::get<double>(operand);
        switch (op) {
            case TokenType::MINUS: return ConstValue{-v};
            default: break;
        }
    }
    
    // Boolean unary
    if (std::holds_alternative<bool>(operand)) {
        bool v = std::get<bool>(operand);
        switch (op) {
            case TokenType::NOT:
            case TokenType::BANG: return ConstValue{!v};
            default: break;
        }
    }
    
    return std::nullopt;
}

ExprPtr ConstantFoldingPass::createLiteral(const ConstValue& value, const SourceLocation& loc) {
    if (std::holds_alternative<int64_t>(value)) {
        return std::make_unique<IntegerLiteral>(std::get<int64_t>(value), loc);
    }
    if (std::holds_alternative<double>(value)) {
        return std::make_unique<FloatLiteral>(std::get<double>(value), loc);
    }
    if (std::holds_alternative<bool>(value)) {
        return std::make_unique<BoolLiteral>(std::get<bool>(value), loc);
    }
    if (std::holds_alternative<std::string>(value)) {
        return std::make_unique<StringLiteral>(std::get<std::string>(value), loc);
    }
    return nullptr;
}

} // namespace tyl
