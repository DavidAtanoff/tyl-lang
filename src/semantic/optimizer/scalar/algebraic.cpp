// Tyl Compiler - Algebraic Simplification Implementation
#include "algebraic.h"
#include <cmath>

namespace tyl {

// ============================================
// Algebraic Simplification Pass
// ============================================

void AlgebraicSimplificationPass::run(Program& ast) {
    transformations_ = 0;
    processBlock(ast.statements);
}

void AlgebraicSimplificationPass::processBlock(std::vector<StmtPtr>& statements) {
    for (auto& stmt : statements) {
        processStatement(stmt);
    }
}

void AlgebraicSimplificationPass::processStatement(StmtPtr& stmt) {
    if (!stmt) return;
    
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt.get())) {
        if (varDecl->initializer) {
            auto simplified = simplifyExpression(varDecl->initializer);
            if (simplified) {
                varDecl->initializer = std::move(simplified);
            }
        }
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt.get())) {
        auto simplified = simplifyExpression(assignStmt->value);
        if (simplified) {
            assignStmt->value = std::move(simplified);
        }
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt.get())) {
        auto simplified = simplifyExpression(exprStmt->expr);
        if (simplified) {
            exprStmt->expr = std::move(simplified);
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
        auto simplified = simplifyExpression(ifStmt->condition);
        if (simplified) {
            ifStmt->condition = std::move(simplified);
        }
        processStatement(ifStmt->thenBranch);
        for (auto& elif : ifStmt->elifBranches) {
            auto elifSimplified = simplifyExpression(elif.first);
            if (elifSimplified) {
                elif.first = std::move(elifSimplified);
            }
            processStatement(elif.second);
        }
        processStatement(ifStmt->elseBranch);
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt.get())) {
        auto simplified = simplifyExpression(whileStmt->condition);
        if (simplified) {
            whileStmt->condition = std::move(simplified);
        }
        processStatement(whileStmt->body);
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt.get())) {
        processStatement(forStmt->body);
    }
    else if (auto* block = dynamic_cast<Block*>(stmt.get())) {
        processBlock(block->statements);
    }
    else if (auto* fnDecl = dynamic_cast<FnDecl*>(stmt.get())) {
        processStatement(fnDecl->body);
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt.get())) {
        if (returnStmt->value) {
            auto simplified = simplifyExpression(returnStmt->value);
            if (simplified) {
                returnStmt->value = std::move(simplified);
            }
        }
    }
}

ExprPtr AlgebraicSimplificationPass::simplifyExpression(ExprPtr& expr) {
    if (!expr) return nullptr;
    
    // First, recursively simplify sub-expressions
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr.get())) {
        auto leftSimplified = simplifyExpression(binary->left);
        if (leftSimplified) binary->left = std::move(leftSimplified);
        
        auto rightSimplified = simplifyExpression(binary->right);
        if (rightSimplified) binary->right = std::move(rightSimplified);
        
        return simplifyBinary(binary);
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr.get())) {
        auto operandSimplified = simplifyExpression(unary->operand);
        if (operandSimplified) unary->operand = std::move(operandSimplified);
        
        return simplifyUnary(unary);
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr.get())) {
        for (auto& arg : call->args) {
            auto argSimplified = simplifyExpression(arg);
            if (argSimplified) arg = std::move(argSimplified);
        }
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr.get())) {
        auto condSimplified = simplifyExpression(ternary->condition);
        if (condSimplified) ternary->condition = std::move(condSimplified);
        
        auto thenSimplified = simplifyExpression(ternary->thenExpr);
        if (thenSimplified) ternary->thenExpr = std::move(thenSimplified);
        
        auto elseSimplified = simplifyExpression(ternary->elseExpr);
        if (elseSimplified) ternary->elseExpr = std::move(elseSimplified);
    }
    
    return nullptr;
}

ExprPtr AlgebraicSimplificationPass::simplifyBinary(BinaryExpr* binary) {
    if (!binary) return nullptr;
    
    SourceLocation loc = binary->location;
    
    // Addition identities
    if (binary->op == TokenType::PLUS) {
        // x + 0 = x
        if (isZero(binary->right.get())) {
            transformations_++;
            return cloneExpr(binary->left.get());
        }
        // 0 + x = x
        if (isZero(binary->left.get())) {
            transformations_++;
            return cloneExpr(binary->right.get());
        }
        // x + (-x) = 0 (would need more analysis)
    }
    
    // Subtraction identities
    if (binary->op == TokenType::MINUS) {
        // x - 0 = x
        if (isZero(binary->right.get())) {
            transformations_++;
            return cloneExpr(binary->left.get());
        }
        // 0 - x = -x
        if (isZero(binary->left.get())) {
            transformations_++;
            return std::make_unique<UnaryExpr>(
                TokenType::MINUS,
                cloneExpr(binary->right.get()),
                loc);
        }
        // x - x = 0 (same identifier)
        if (auto* leftId = dynamic_cast<Identifier*>(binary->left.get())) {
            if (auto* rightId = dynamic_cast<Identifier*>(binary->right.get())) {
                if (leftId->name == rightId->name) {
                    transformations_++;
                    return std::make_unique<IntegerLiteral>(0, loc);
                }
            }
        }
    }
    
    // Multiplication identities
    if (binary->op == TokenType::STAR) {
        // x * 0 = 0
        if (isZero(binary->right.get())) {
            transformations_++;
            return std::make_unique<IntegerLiteral>(0, loc);
        }
        if (isZero(binary->left.get())) {
            transformations_++;
            return std::make_unique<IntegerLiteral>(0, loc);
        }
        // x * 1 = x
        if (isOne(binary->right.get())) {
            transformations_++;
            return cloneExpr(binary->left.get());
        }
        if (isOne(binary->left.get())) {
            transformations_++;
            return cloneExpr(binary->right.get());
        }
        // x * -1 = -x
        if (isNegOne(binary->right.get())) {
            transformations_++;
            return std::make_unique<UnaryExpr>(
                TokenType::MINUS,
                cloneExpr(binary->left.get()),
                loc);
        }
        if (isNegOne(binary->left.get())) {
            transformations_++;
            return std::make_unique<UnaryExpr>(
                TokenType::MINUS,
                cloneExpr(binary->right.get()),
                loc);
        }
        // x * 2 = x + x (strength reduction)
        int power;
        if (isPowerOfTwo(binary->right.get(), power)) {
            if (power == 1) {
                transformations_++;
                return std::make_unique<BinaryExpr>(
                    cloneExpr(binary->left.get()),
                    TokenType::PLUS,
                    cloneExpr(binary->left.get()),
                    loc);
            }
            // x * 2^n = x << n (if we had shift operators)
            // For now, decompose: x * 4 = (x + x) + (x + x)
            if (power == 2) {
                transformations_++;
                auto xPlusX = std::make_unique<BinaryExpr>(
                    cloneExpr(binary->left.get()),
                    TokenType::PLUS,
                    cloneExpr(binary->left.get()),
                    loc);
                return std::make_unique<BinaryExpr>(
                    cloneExpr(xPlusX.get()),
                    TokenType::PLUS,
                    std::move(xPlusX),
                    loc);
            }
        }
        if (isPowerOfTwo(binary->left.get(), power)) {
            if (power == 1) {
                transformations_++;
                return std::make_unique<BinaryExpr>(
                    cloneExpr(binary->right.get()),
                    TokenType::PLUS,
                    cloneExpr(binary->right.get()),
                    loc);
            }
        }
    }
    
    // Division identities
    if (binary->op == TokenType::SLASH) {
        // x / 1 = x
        if (isOne(binary->right.get())) {
            transformations_++;
            return cloneExpr(binary->left.get());
        }
        // 0 / x = 0 (assuming x != 0)
        if (isZero(binary->left.get())) {
            transformations_++;
            return std::make_unique<IntegerLiteral>(0, loc);
        }
        // x / x = 1 (same identifier, assuming x != 0)
        if (auto* leftId = dynamic_cast<Identifier*>(binary->left.get())) {
            if (auto* rightId = dynamic_cast<Identifier*>(binary->right.get())) {
                if (leftId->name == rightId->name) {
                    transformations_++;
                    return std::make_unique<IntegerLiteral>(1, loc);
                }
            }
        }
    }
    
    // Modulo identities
    if (binary->op == TokenType::PERCENT) {
        // x % 1 = 0
        if (isOne(binary->right.get())) {
            transformations_++;
            return std::make_unique<IntegerLiteral>(0, loc);
        }
        // 0 % x = 0
        if (isZero(binary->left.get())) {
            transformations_++;
            return std::make_unique<IntegerLiteral>(0, loc);
        }
    }
    
    // Boolean identities
    if (binary->op == TokenType::AND || binary->op == TokenType::AMP_AMP) {
        // x && false = false
        if (auto* rightBool = dynamic_cast<BoolLiteral*>(binary->right.get())) {
            if (!rightBool->value) {
                transformations_++;
                return std::make_unique<BoolLiteral>(false, loc);
            }
            // x && true = x
            if (rightBool->value) {
                transformations_++;
                return cloneExpr(binary->left.get());
            }
        }
        if (auto* leftBool = dynamic_cast<BoolLiteral*>(binary->left.get())) {
            if (!leftBool->value) {
                transformations_++;
                return std::make_unique<BoolLiteral>(false, loc);
            }
            if (leftBool->value) {
                transformations_++;
                return cloneExpr(binary->right.get());
            }
        }
    }
    
    if (binary->op == TokenType::OR || binary->op == TokenType::PIPE_PIPE) {
        // x || true = true
        if (auto* rightBool = dynamic_cast<BoolLiteral*>(binary->right.get())) {
            if (rightBool->value) {
                transformations_++;
                return std::make_unique<BoolLiteral>(true, loc);
            }
            // x || false = x
            if (!rightBool->value) {
                transformations_++;
                return cloneExpr(binary->left.get());
            }
        }
        if (auto* leftBool = dynamic_cast<BoolLiteral*>(binary->left.get())) {
            if (leftBool->value) {
                transformations_++;
                return std::make_unique<BoolLiteral>(true, loc);
            }
            if (!leftBool->value) {
                transformations_++;
                return cloneExpr(binary->right.get());
            }
        }
    }
    
    // Comparison identities
    if (binary->op == TokenType::EQ) {
        // x == x = true (same identifier)
        if (auto* leftId = dynamic_cast<Identifier*>(binary->left.get())) {
            if (auto* rightId = dynamic_cast<Identifier*>(binary->right.get())) {
                if (leftId->name == rightId->name) {
                    transformations_++;
                    return std::make_unique<BoolLiteral>(true, loc);
                }
            }
        }
    }
    
    if (binary->op == TokenType::NE) {
        // x != x = false (same identifier)
        if (auto* leftId = dynamic_cast<Identifier*>(binary->left.get())) {
            if (auto* rightId = dynamic_cast<Identifier*>(binary->right.get())) {
                if (leftId->name == rightId->name) {
                    transformations_++;
                    return std::make_unique<BoolLiteral>(false, loc);
                }
            }
        }
    }
    
    return nullptr;
}

ExprPtr AlgebraicSimplificationPass::simplifyUnary(UnaryExpr* unary) {
    if (!unary) return nullptr;
    
    SourceLocation loc = unary->location;
    
    // Double negation: --x = x
    if (unary->op == TokenType::MINUS) {
        if (auto* innerUnary = dynamic_cast<UnaryExpr*>(unary->operand.get())) {
            if (innerUnary->op == TokenType::MINUS) {
                transformations_++;
                return cloneExpr(innerUnary->operand.get());
            }
        }
        // -0 = 0
        if (isZero(unary->operand.get())) {
            transformations_++;
            return std::make_unique<IntegerLiteral>(0, loc);
        }
    }
    
    // Double NOT: !!x = x (for booleans)
    if (unary->op == TokenType::NOT || unary->op == TokenType::BANG) {
        if (auto* innerUnary = dynamic_cast<UnaryExpr*>(unary->operand.get())) {
            if (innerUnary->op == TokenType::NOT || innerUnary->op == TokenType::BANG) {
                transformations_++;
                return cloneExpr(innerUnary->operand.get());
            }
        }
    }
    
    return nullptr;
}

bool AlgebraicSimplificationPass::isZero(Expression* expr) {
    if (auto* intLit = dynamic_cast<IntegerLiteral*>(expr)) {
        return intLit->value == 0;
    }
    if (auto* floatLit = dynamic_cast<FloatLiteral*>(expr)) {
        return floatLit->value == 0.0;
    }
    return false;
}

bool AlgebraicSimplificationPass::isOne(Expression* expr) {
    if (auto* intLit = dynamic_cast<IntegerLiteral*>(expr)) {
        return intLit->value == 1;
    }
    if (auto* floatLit = dynamic_cast<FloatLiteral*>(expr)) {
        return floatLit->value == 1.0;
    }
    return false;
}

bool AlgebraicSimplificationPass::isNegOne(Expression* expr) {
    if (auto* intLit = dynamic_cast<IntegerLiteral*>(expr)) {
        return intLit->value == -1;
    }
    if (auto* floatLit = dynamic_cast<FloatLiteral*>(expr)) {
        return floatLit->value == -1.0;
    }
    return false;
}

bool AlgebraicSimplificationPass::isPowerOfTwo(Expression* expr, int& power) {
    if (auto* intLit = dynamic_cast<IntegerLiteral*>(expr)) {
        int64_t val = intLit->value;
        if (val > 0 && (val & (val - 1)) == 0) {
            power = 0;
            while (val > 1) {
                val >>= 1;
                power++;
            }
            return true;
        }
    }
    return false;
}

ExprPtr AlgebraicSimplificationPass::cloneExpr(Expression* expr) {
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

// ============================================
// Advanced Strength Reduction Pass
// ============================================

void AdvancedStrengthReductionPass::run(Program& ast) {
    transformations_ = 0;
    processBlock(ast.statements);
}

void AdvancedStrengthReductionPass::processBlock(std::vector<StmtPtr>& statements) {
    for (auto& stmt : statements) {
        processStatement(stmt);
    }
}

void AdvancedStrengthReductionPass::processStatement(StmtPtr& stmt) {
    if (!stmt) return;
    
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt.get())) {
        if (varDecl->initializer) {
            auto reduced = reduceExpression(varDecl->initializer);
            if (reduced) {
                varDecl->initializer = std::move(reduced);
            }
        }
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt.get())) {
        auto reduced = reduceExpression(assignStmt->value);
        if (reduced) {
            assignStmt->value = std::move(reduced);
        }
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt.get())) {
        auto reduced = reduceExpression(exprStmt->expr);
        if (reduced) {
            exprStmt->expr = std::move(reduced);
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
        auto reduced = reduceExpression(ifStmt->condition);
        if (reduced) {
            ifStmt->condition = std::move(reduced);
        }
        processStatement(ifStmt->thenBranch);
        for (auto& elif : ifStmt->elifBranches) {
            auto elifReduced = reduceExpression(elif.first);
            if (elifReduced) {
                elif.first = std::move(elifReduced);
            }
            processStatement(elif.second);
        }
        processStatement(ifStmt->elseBranch);
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt.get())) {
        auto reduced = reduceExpression(whileStmt->condition);
        if (reduced) {
            whileStmt->condition = std::move(reduced);
        }
        processStatement(whileStmt->body);
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt.get())) {
        processStatement(forStmt->body);
    }
    else if (auto* block = dynamic_cast<Block*>(stmt.get())) {
        processBlock(block->statements);
    }
    else if (auto* fnDecl = dynamic_cast<FnDecl*>(stmt.get())) {
        processStatement(fnDecl->body);
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt.get())) {
        if (returnStmt->value) {
            auto reduced = reduceExpression(returnStmt->value);
            if (reduced) {
                returnStmt->value = std::move(reduced);
            }
        }
    }
}

ExprPtr AdvancedStrengthReductionPass::reduceExpression(ExprPtr& expr) {
    if (!expr) return nullptr;
    
    // First, recursively reduce sub-expressions
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr.get())) {
        auto leftReduced = reduceExpression(binary->left);
        if (leftReduced) binary->left = std::move(leftReduced);
        
        auto rightReduced = reduceExpression(binary->right);
        if (rightReduced) binary->right = std::move(rightReduced);
        
        // Apply specific reductions
        if (binary->op == TokenType::STAR) {
            return reduceMultiply(binary);
        }
        if (binary->op == TokenType::SLASH) {
            return reduceDivide(binary);
        }
        if (binary->op == TokenType::PERCENT) {
            return reduceModulo(binary);
        }
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr.get())) {
        auto operandReduced = reduceExpression(unary->operand);
        if (operandReduced) unary->operand = std::move(operandReduced);
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr.get())) {
        for (auto& arg : call->args) {
            auto argReduced = reduceExpression(arg);
            if (argReduced) arg = std::move(argReduced);
        }
    }
    
    return nullptr;
}

ExprPtr AdvancedStrengthReductionPass::reduceMultiply(BinaryExpr* binary) {
    if (!binary) return nullptr;
    
    SourceLocation loc = binary->location;
    
    // Check for multiplication by power of 2
    if (auto* rightLit = dynamic_cast<IntegerLiteral*>(binary->right.get())) {
        int64_t val = rightLit->value;
        
        if (isPowerOfTwo(val)) {
            int shift = log2(val);
            
            // x * 2 -> x + x
            if (shift == 1) {
                transformations_++;
                return std::make_unique<BinaryExpr>(
                    cloneExpr(binary->left.get()),
                    TokenType::PLUS,
                    cloneExpr(binary->left.get()),
                    loc);
            }
            
            // x * 3 = x * 2 + x = (x + x) + x
            // x * 4 = (x + x) + (x + x)
            // x * 5 = x * 4 + x
            // etc.
            
            // For small multipliers, decompose into additions
            if (val == 3) {
                transformations_++;
                auto xPlusX = std::make_unique<BinaryExpr>(
                    cloneExpr(binary->left.get()),
                    TokenType::PLUS,
                    cloneExpr(binary->left.get()),
                    loc);
                return std::make_unique<BinaryExpr>(
                    std::move(xPlusX),
                    TokenType::PLUS,
                    cloneExpr(binary->left.get()),
                    loc);
            }
            
            if (val == 4) {
                transformations_++;
                auto xPlusX = std::make_unique<BinaryExpr>(
                    cloneExpr(binary->left.get()),
                    TokenType::PLUS,
                    cloneExpr(binary->left.get()),
                    loc);
                auto xPlusX2 = std::make_unique<BinaryExpr>(
                    cloneExpr(binary->left.get()),
                    TokenType::PLUS,
                    cloneExpr(binary->left.get()),
                    loc);
                return std::make_unique<BinaryExpr>(
                    std::move(xPlusX),
                    TokenType::PLUS,
                    std::move(xPlusX2),
                    loc);
            }
            
            if (val == 5) {
                transformations_++;
                // x * 5 = x * 4 + x
                auto xPlusX = std::make_unique<BinaryExpr>(
                    cloneExpr(binary->left.get()),
                    TokenType::PLUS,
                    cloneExpr(binary->left.get()),
                    loc);
                auto xPlusX2 = std::make_unique<BinaryExpr>(
                    cloneExpr(binary->left.get()),
                    TokenType::PLUS,
                    cloneExpr(binary->left.get()),
                    loc);
                auto x4 = std::make_unique<BinaryExpr>(
                    std::move(xPlusX),
                    TokenType::PLUS,
                    std::move(xPlusX2),
                    loc);
                return std::make_unique<BinaryExpr>(
                    std::move(x4),
                    TokenType::PLUS,
                    cloneExpr(binary->left.get()),
                    loc);
            }
        }
    }
    
    // Also check left operand
    if (auto* leftLit = dynamic_cast<IntegerLiteral*>(binary->left.get())) {
        int64_t val = leftLit->value;
        
        if (val == 2) {
            transformations_++;
            return std::make_unique<BinaryExpr>(
                cloneExpr(binary->right.get()),
                TokenType::PLUS,
                cloneExpr(binary->right.get()),
                loc);
        }
    }
    
    return nullptr;
}

ExprPtr AdvancedStrengthReductionPass::reduceDivide(BinaryExpr* binary) {
    // Division by power of 2 could be converted to right shift
    // But we don't have shift operators in the language yet
    // For now, just return nullptr
    (void)binary;
    return nullptr;
}

ExprPtr AdvancedStrengthReductionPass::reduceModulo(BinaryExpr* binary) {
    // Modulo by power of 2 could be converted to AND with (n-1)
    // But we need bitwise AND operator
    // For now, just return nullptr
    (void)binary;
    return nullptr;
}

bool AdvancedStrengthReductionPass::isPowerOfTwo(int64_t value) {
    return value > 0 && (value & (value - 1)) == 0;
}

int AdvancedStrengthReductionPass::log2(int64_t value) {
    int result = 0;
    while (value > 1) {
        value >>= 1;
        result++;
    }
    return result;
}

ExprPtr AdvancedStrengthReductionPass::cloneExpr(Expression* expr) {
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

} // namespace tyl
