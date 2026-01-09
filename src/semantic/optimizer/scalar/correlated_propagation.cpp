// Tyl Compiler - Correlated Value Propagation Implementation
// Uses range analysis to simplify comparisons and eliminate branches
#include "correlated_propagation.h"
#include <algorithm>
#include <iostream>
#include <limits>

namespace tyl {

// ============================================
// ValueRange Implementation
// ============================================

ValueRange ValueRange::intersect(const ValueRange& other) const {
    if (isUnknown) return other;
    if (other.isUnknown) return *this;
    
    ValueRange result;
    result.min = std::max(min, other.min);
    result.max = std::min(max, other.max);
    result.isUnknown = false;
    return result;
}

ValueRange ValueRange::unionWith(const ValueRange& other) const {
    if (isUnknown || other.isUnknown) return unknown();
    
    ValueRange result;
    result.min = std::min(min, other.min);
    result.max = std::max(max, other.max);
    result.isUnknown = false;
    return result;
}

ValueRange ValueRange::add(const ValueRange& other) const {
    if (isUnknown || other.isUnknown) return unknown();
    
    // Simple overflow check
    int64_t newMin = min + other.min;
    int64_t newMax = max + other.max;
    
    // Check for overflow
    if ((other.min > 0 && min > INT64_MAX - other.min) ||
        (other.min < 0 && min < INT64_MIN - other.min) ||
        (other.max > 0 && max > INT64_MAX - other.max) ||
        (other.max < 0 && max < INT64_MIN - other.max)) {
        return unknown();
    }
    
    return ValueRange(newMin, newMax);
}

ValueRange ValueRange::sub(const ValueRange& other) const {
    if (isUnknown || other.isUnknown) return unknown();
    
    int64_t newMin = min - other.max;
    int64_t newMax = max - other.min;
    
    // Check for overflow
    if ((other.max < 0 && min > INT64_MAX + other.max) ||
        (other.max > 0 && min < INT64_MIN + other.max) ||
        (other.min < 0 && max > INT64_MAX + other.min) ||
        (other.min > 0 && max < INT64_MIN + other.min)) {
        return unknown();
    }
    
    return ValueRange(newMin, newMax);
}

ValueRange ValueRange::mul(const ValueRange& other) const {
    if (isUnknown || other.isUnknown) return unknown();
    
    // Compute all four corner products
    int64_t products[4] = {
        min * other.min,
        min * other.max,
        max * other.min,
        max * other.max
    };
    
    int64_t newMin = std::min({products[0], products[1], products[2], products[3]});
    int64_t newMax = std::max({products[0], products[1], products[2], products[3]});
    
    return ValueRange(newMin, newMax);
}

std::optional<bool> ValueRange::compareWith(const ValueRange& other, TokenType op) const {
    if (isUnknown || other.isUnknown) return std::nullopt;
    
    switch (op) {
        case TokenType::LT:
            if (max < other.min) return true;
            if (min >= other.max) return false;
            break;
            
        case TokenType::LE:
            if (max <= other.min) return true;
            if (min > other.max) return false;
            break;
            
        case TokenType::GT:
            if (min > other.max) return true;
            if (max <= other.min) return false;
            break;
            
        case TokenType::GE:
            if (min >= other.max) return true;
            if (max < other.min) return false;
            break;
            
        case TokenType::EQ:
            if (min == max && other.min == other.max && min == other.min) return true;
            if (max < other.min || min > other.max) return false;
            break;
            
        case TokenType::NE:
            if (max < other.min || min > other.max) return true;
            if (min == max && other.min == other.max && min == other.min) return false;
            break;
            
        default:
            break;
    }
    
    return std::nullopt;
}

// ============================================
// ValueConstraint Implementation
// ============================================

ValueRange ValueConstraint::toRange() const {
    switch (op) {
        case TokenType::LT:
            return ValueRange(INT64_MIN, value - 1);
        case TokenType::LE:
            return ValueRange(INT64_MIN, value);
        case TokenType::GT:
            return ValueRange(value + 1, INT64_MAX);
        case TokenType::GE:
            return ValueRange(value, INT64_MAX);
        case TokenType::EQ:
            return ValueRange(value, value);
        case TokenType::NE:
            // Can't represent "not equal" as a single range
            return ValueRange::unknown();
        default:
            return ValueRange::unknown();
    }
}

std::optional<bool> ValueConstraint::isSatisfiedBy(const ValueRange& range) const {
    return range.compareWith(ValueRange(value), op);
}

// ============================================
// CorrelatedValuePropagationPass Implementation
// ============================================

void CorrelatedValuePropagationPass::run(Program& ast) {
    transformations_ = 0;
    
    // Process each function
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

void CorrelatedValuePropagationPass::processFunction(FnDecl* fn) {
    if (!fn || !fn->body) return;
    
    auto* body = dynamic_cast<Block*>(fn->body.get());
    if (!body) return;
    
    valueRanges_.clear();
    rangeStack_.clear();
    
    // Initialize parameter ranges (unknown by default)
    for (const auto& param : fn->params) {
        valueRanges_[param.first] = ValueRange::unknown();
    }
    
    processStatements(body->statements);
}

void CorrelatedValuePropagationPass::processStatements(std::vector<StmtPtr>& stmts) {
    for (auto& stmt : stmts) {
        processStatement(stmt);
    }
}

void CorrelatedValuePropagationPass::processStatement(StmtPtr& stmt) {
    if (!stmt) return;
    
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt.get())) {
        // Track range from initializer
        if (varDecl->initializer) {
            ValueRange range = processExpression(varDecl->initializer.get());
            setRange(varDecl->name, range);
        }
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt.get())) {
        // Update range from assignment
        if (assignStmt->value) {
            ValueRange range = processExpression(assignStmt->value.get());
            if (auto* ident = dynamic_cast<Identifier*>(assignStmt->target.get())) {
                setRange(ident->name, range);
            }
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
        // Try to simplify condition
        if (auto* cmp = dynamic_cast<BinaryExpr*>(ifStmt->condition.get())) {
            if (isComparison(cmp)) {
                auto simplified = simplifyComparison(cmp);
                if (simplified) {
                    ifStmt->condition = std::move(simplified);
                    transformations_++;
                }
            }
        }
        
        // Extract constraints from condition
        auto constraints = extractConstraints(ifStmt->condition.get());
        
        // Process then branch with constraints applied
        pushScope();
        applyConstraints(constraints);
        
        if (auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
            processStatements(thenBlock->statements);
        }
        
        auto thenRanges = valueRanges_;
        popScope();
        
        // Process else branch with negated constraints
        pushScope();
        auto negatedConstraints = extractConstraints(ifStmt->condition.get(), true);
        applyConstraints(negatedConstraints);
        
        if (auto* elseBlock = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
            processStatements(elseBlock->statements);
        }
        
        auto elseRanges = valueRanges_;
        popScope();
        
        // Merge ranges from both branches
        mergeRanges(thenRanges);
        mergeRanges(elseRanges);
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt.get())) {
        // Try to simplify condition
        if (auto* cmp = dynamic_cast<BinaryExpr*>(whileStmt->condition.get())) {
            if (isComparison(cmp)) {
                auto simplified = simplifyComparison(cmp);
                if (simplified) {
                    whileStmt->condition = std::move(simplified);
                    transformations_++;
                }
            }
        }
        
        // Process loop body with constraints
        pushScope();
        auto constraints = extractConstraints(whileStmt->condition.get());
        applyConstraints(constraints);
        
        if (auto* body = dynamic_cast<Block*>(whileStmt->body.get())) {
            processStatements(body->statements);
        }
        
        popScope();
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt.get())) {
        // For-each loop - process body
        pushScope();
        
        if (auto* body = dynamic_cast<Block*>(forStmt->body.get())) {
            processStatements(body->statements);
        }
        
        popScope();
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt.get())) {
        // Process expression for side effects and range updates
        if (exprStmt->expr) {
            processExpression(exprStmt->expr.get());
        }
    }
}

ValueRange CorrelatedValuePropagationPass::processExpression(Expression* expr) {
    if (!expr) return ValueRange::unknown();
    
    if (auto* lit = dynamic_cast<IntegerLiteral*>(expr)) {
        return ValueRange::constant(lit->value);
    }
    
    if (auto* boolLit = dynamic_cast<BoolLiteral*>(expr)) {
        return ValueRange::constant(boolLit->value ? 1 : 0);
    }
    
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        return getRange(ident->name);
    }
    
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        ValueRange lhsRange = processExpression(binary->left.get());
        ValueRange rhsRange = processExpression(binary->right.get());
        
        switch (binary->op) {
            case TokenType::PLUS:
                return lhsRange.add(rhsRange);
            case TokenType::MINUS:
                return lhsRange.sub(rhsRange);
            case TokenType::STAR:
                return lhsRange.mul(rhsRange);
            default:
                return ValueRange::unknown();
        }
    }
    
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        ValueRange operandRange = processExpression(unary->operand.get());
        
        if (unary->op == TokenType::MINUS) {
            // Negation flips the range
            if (!operandRange.isUnknown) {
                return ValueRange(-operandRange.max, -operandRange.min);
            }
        }
        
        return ValueRange::unknown();
    }
    
    return ValueRange::unknown();
}

std::vector<ValueConstraint> CorrelatedValuePropagationPass::extractConstraints(
    Expression* cond, bool negate) {
    
    std::vector<ValueConstraint> constraints;
    
    if (auto* binary = dynamic_cast<BinaryExpr*>(cond)) {
        // Handle comparison operators
        if (isComparison(binary)) {
            auto varName = getVariableName(binary->left.get());
            auto constVal = evaluateConstant(binary->right.get());
            
            if (varName && constVal) {
                TokenType op = binary->op;
                
                // Negate the operator if needed
                if (negate) {
                    switch (op) {
                        case TokenType::LT: op = TokenType::GE; break;
                        case TokenType::LE: op = TokenType::GT; break;
                        case TokenType::GT: op = TokenType::LE; break;
                        case TokenType::GE: op = TokenType::LT; break;
                        case TokenType::EQ: op = TokenType::NE; break;
                        case TokenType::NE: op = TokenType::EQ; break;
                        default: break;
                    }
                }
                
                constraints.emplace_back(*varName, op, *constVal);
            }
            
            // Also try with operands swapped
            varName = getVariableName(binary->right.get());
            constVal = evaluateConstant(binary->left.get());
            
            if (varName && constVal) {
                // Swap the comparison direction
                TokenType op = binary->op;
                switch (op) {
                    case TokenType::LT: op = TokenType::GT; break;
                    case TokenType::LE: op = TokenType::GE; break;
                    case TokenType::GT: op = TokenType::LT; break;
                    case TokenType::GE: op = TokenType::LE; break;
                    default: break;
                }
                
                if (negate) {
                    switch (op) {
                        case TokenType::LT: op = TokenType::GE; break;
                        case TokenType::LE: op = TokenType::GT; break;
                        case TokenType::GT: op = TokenType::LE; break;
                        case TokenType::GE: op = TokenType::LT; break;
                        case TokenType::EQ: op = TokenType::NE; break;
                        case TokenType::NE: op = TokenType::EQ; break;
                        default: break;
                    }
                }
                
                constraints.emplace_back(*varName, op, *constVal);
            }
        }
        
        // Handle logical AND - both conditions must hold
        if (binary->op == TokenType::AMP_AMP && !negate) {
            auto leftConstraints = extractConstraints(binary->left.get(), false);
            auto rightConstraints = extractConstraints(binary->right.get(), false);
            constraints.insert(constraints.end(), leftConstraints.begin(), leftConstraints.end());
            constraints.insert(constraints.end(), rightConstraints.begin(), rightConstraints.end());
        }
        
        // Handle logical OR when negated - both negated conditions must hold
        if (binary->op == TokenType::PIPE_PIPE && negate) {
            auto leftConstraints = extractConstraints(binary->left.get(), true);
            auto rightConstraints = extractConstraints(binary->right.get(), true);
            constraints.insert(constraints.end(), leftConstraints.begin(), leftConstraints.end());
            constraints.insert(constraints.end(), rightConstraints.begin(), rightConstraints.end());
        }
    }
    
    return constraints;
}

void CorrelatedValuePropagationPass::applyConstraints(
    const std::vector<ValueConstraint>& constraints) {
    
    for (const auto& constraint : constraints) {
        ValueRange currentRange = getRange(constraint.variable);
        ValueRange constraintRange = constraint.toRange();
        ValueRange newRange = currentRange.intersect(constraintRange);
        setRange(constraint.variable, newRange);
    }
}

ExprPtr CorrelatedValuePropagationPass::simplifyComparison(BinaryExpr* cmp) {
    if (!cmp) return nullptr;
    
    ValueRange lhsRange = processExpression(cmp->left.get());
    ValueRange rhsRange = processExpression(cmp->right.get());
    
    auto result = lhsRange.compareWith(rhsRange, cmp->op);
    
    if (result.has_value()) {
        return std::make_unique<BoolLiteral>(*result, cmp->location);
    }
    
    return nullptr;
}

ExprPtr CorrelatedValuePropagationPass::convertSignedToUnsigned(BinaryExpr* expr) {
    // If both operands are known non-negative, we can use unsigned operations
    ValueRange lhsRange = processExpression(expr->left.get());
    ValueRange rhsRange = processExpression(expr->right.get());
    
    if (lhsRange.isNonNegative() && rhsRange.isNonNegative()) {
        // Could convert signed comparison to unsigned
        // This is mainly useful for backend code generation
        return nullptr;  // For now, just track the information
    }
    
    return nullptr;
}

ExprPtr CorrelatedValuePropagationPass::narrowDivRem(BinaryExpr* expr) {
    // If operand ranges are small enough, we could narrow the operation
    // This is mainly useful for backend code generation
    return nullptr;
}

void CorrelatedValuePropagationPass::pushScope() {
    rangeStack_.push_back(valueRanges_);
}

void CorrelatedValuePropagationPass::popScope() {
    if (!rangeStack_.empty()) {
        valueRanges_ = rangeStack_.back();
        rangeStack_.pop_back();
    }
}

ValueRange CorrelatedValuePropagationPass::getRange(const std::string& var) const {
    auto it = valueRanges_.find(var);
    if (it != valueRanges_.end()) {
        return it->second;
    }
    return ValueRange::unknown();
}

void CorrelatedValuePropagationPass::setRange(const std::string& var, const ValueRange& range) {
    valueRanges_[var] = range;
}

void CorrelatedValuePropagationPass::mergeRanges(const std::map<std::string, ValueRange>& other) {
    for (const auto& [var, range] : other) {
        auto it = valueRanges_.find(var);
        if (it != valueRanges_.end()) {
            // Union the ranges from both branches
            it->second = it->second.unionWith(range);
        } else {
            valueRanges_[var] = range;
        }
    }
}

std::optional<std::string> CorrelatedValuePropagationPass::getVariableName(Expression* expr) {
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        return ident->name;
    }
    return std::nullopt;
}

std::optional<int64_t> CorrelatedValuePropagationPass::evaluateConstant(Expression* expr) {
    if (auto* lit = dynamic_cast<IntegerLiteral*>(expr)) {
        return lit->value;
    }
    if (auto* boolLit = dynamic_cast<BoolLiteral*>(expr)) {
        return boolLit->value ? 1 : 0;
    }
    
    // Try to evaluate constant expressions
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        auto operand = evaluateConstant(unary->operand.get());
        if (operand) {
            if (unary->op == TokenType::MINUS) return -*operand;
            if (unary->op == TokenType::BANG) return *operand == 0 ? 1 : 0;
        }
    }
    
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        auto lhs = evaluateConstant(binary->left.get());
        auto rhs = evaluateConstant(binary->right.get());
        if (lhs && rhs) {
            switch (binary->op) {
                case TokenType::PLUS: return *lhs + *rhs;
                case TokenType::MINUS: return *lhs - *rhs;
                case TokenType::STAR: return *lhs * *rhs;
                case TokenType::SLASH: 
                    if (*rhs != 0) return *lhs / *rhs;
                    return std::nullopt;
                default: break;
            }
        }
    }
    
    return std::nullopt;
}

bool CorrelatedValuePropagationPass::isComparison(Expression* expr) {
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        switch (binary->op) {
            case TokenType::LT:
            case TokenType::LE:
            case TokenType::GT:
            case TokenType::GE:
            case TokenType::EQ:
            case TokenType::NE:
                return true;
            default:
                return false;
        }
    }
    return false;
}

} // namespace tyl
