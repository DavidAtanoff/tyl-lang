// Tyl Compiler - Constraint Elimination Implementation
// Uses constraint solving to eliminate redundant checks
#include "constraint_elimination.h"
#include <algorithm>
#include <iostream>

namespace tyl {

// ============================================
// LinearConstraint Implementation
// ============================================

void LinearConstraint::addTerm(const std::string& var, int64_t coeff) {
    if (coeff == 0) return;
    
    auto it = coefficients.find(var);
    if (it != coefficients.end()) {
        it->second += coeff;
        if (it->second == 0) {
            coefficients.erase(it);
        }
    } else {
        coefficients[var] = coeff;
    }
}

LinearConstraint LinearConstraint::negate() const {
    LinearConstraint result;
    result.constant = -constant;
    for (const auto& [var, coeff] : coefficients) {
        result.coefficients[var] = -coeff;
    }
    // Negate strict inequality: !(x <= 0) becomes x > 0, i.e., -x < 0
    // But we represent as -x - 1 <= 0
    if (!isStrict) {
        result.constant -= 1;
    }
    result.isStrict = !isStrict;
    return result;
}

std::optional<bool> LinearConstraint::isTriviallyTrue() const {
    // If no variables, just check constant
    if (coefficients.empty()) {
        if (isStrict) {
            return constant < 0;
        } else {
            return constant <= 0;
        }
    }
    return std::nullopt;
}

std::optional<bool> LinearConstraint::isTriviallyFalse() const {
    // If no variables, just check constant
    if (coefficients.empty()) {
        if (isStrict) {
            return constant >= 0;
        } else {
            return constant > 0;
        }
    }
    return std::nullopt;
}

LinearConstraint LinearConstraint::substitute(const std::map<std::string, int64_t>& values) const {
    LinearConstraint result;
    result.constant = constant;
    result.isStrict = isStrict;
    
    for (const auto& [var, coeff] : coefficients) {
        auto it = values.find(var);
        if (it != values.end()) {
            // Substitute known value
            result.constant += coeff * it->second;
        } else {
            // Keep variable
            result.coefficients[var] = coeff;
        }
    }
    
    return result;
}

// ============================================
// ConstraintSystem Implementation
// ============================================

void ConstraintSystem::addConstraint(const LinearConstraint& constraint) {
    constraints_.push_back(constraint);
}

std::optional<bool> ConstraintSystem::isImplied(const LinearConstraint& constraint) const {
    // First check if trivially true/false
    auto trivial = constraint.isTriviallyTrue();
    if (trivial.has_value()) {
        return trivial;
    }
    
    // Check if any existing constraint implies this one
    if (checkImplication(constraint)) {
        return true;
    }
    
    return std::nullopt;
}

std::optional<bool> ConstraintSystem::isContradicted(const LinearConstraint& constraint) const {
    // Check if the negation is implied
    LinearConstraint negated = constraint.negate();
    return isImplied(negated);
}

void ConstraintSystem::clear() {
    constraints_.clear();
    scopeStack_.clear();
}

void ConstraintSystem::pushScope() {
    scopeStack_.push_back(constraints_.size());
}

void ConstraintSystem::popScope() {
    if (!scopeStack_.empty()) {
        size_t prevSize = scopeStack_.back();
        scopeStack_.pop_back();
        constraints_.resize(prevSize);
    }
}

bool ConstraintSystem::checkImplication(const LinearConstraint& constraint) const {
    // Simple implication check:
    // If we have a constraint that directly implies this one
    
    // For single-variable constraints, check transitivity
    if (constraint.coefficients.size() == 1) {
        const auto& [var, coeff] = *constraint.coefficients.begin();
        
        for (const auto& existing : constraints_) {
            if (existing.coefficients.size() == 1) {
                auto it = existing.coefficients.find(var);
                if (it != existing.coefficients.end() && it->second == coeff) {
                    // Same variable with same coefficient
                    // existing: coeff * var + c1 <= 0
                    // constraint: coeff * var + c2 <= 0
                    // If c1 >= c2, then existing implies constraint
                    if (existing.constant >= constraint.constant) {
                        if (!constraint.isStrict || existing.isStrict || 
                            existing.constant > constraint.constant) {
                            return true;
                        }
                    }
                }
            }
        }
    }
    
    // Check for exact match
    for (const auto& existing : constraints_) {
        if (existing.coefficients == constraint.coefficients) {
            if (existing.constant >= constraint.constant) {
                if (!constraint.isStrict || existing.isStrict ||
                    existing.constant > constraint.constant) {
                    return true;
                }
            }
        }
    }
    
    return false;
}

// ============================================
// ConstraintEliminationPass Implementation
// ============================================

void ConstraintEliminationPass::run(Program& ast) {
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

void ConstraintEliminationPass::processFunction(FnDecl* fn) {
    if (!fn || !fn->body) return;
    
    auto* body = dynamic_cast<Block*>(fn->body.get());
    if (!body) return;
    
    constraintSystem_.clear();
    worklist_.clear();
    currentDFSIn_ = 0;
    currentDFSOut_ = 0;
    
    // Build worklist with DFS numbering
    buildWorklist(body->statements);
    
    // Process worklist
    processWorklist(body->statements);
    
    // Apply transformations
    transformStatements(body->statements);
}

void ConstraintEliminationPass::buildWorklist(std::vector<StmtPtr>& stmts) {
    for (auto& stmt : stmts) {
        visitStatement(stmt.get());
    }
}

void ConstraintEliminationPass::visitStatement(Statement* stmt) {
    if (!stmt) return;
    
    unsigned dfsIn = currentDFSIn_++;
    
    if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        // Add condition as a check
        if (ifStmt->condition) {
            visitExpression(ifStmt->condition.get());
            worklist_.push_back(ConstraintFactOrCheck::makeCheck(
                ifStmt->condition.get(), dfsIn, currentDFSOut_));
        }
        
        // Extract facts from condition for then branch
        auto facts = extractFacts(ifStmt->condition.get());
        for (const auto& fact : facts) {
            worklist_.push_back(ConstraintFactOrCheck::makeFact(fact, dfsIn, currentDFSOut_));
        }
        
        // Visit then branch
        if (auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
            buildWorklist(thenBlock->statements);
        }
        
        // Extract negated facts for else branch
        auto negatedFacts = extractFacts(ifStmt->condition.get(), true);
        for (const auto& fact : negatedFacts) {
            worklist_.push_back(ConstraintFactOrCheck::makeFact(fact, dfsIn, currentDFSOut_));
        }
        
        // Visit else branch
        if (auto* elseBlock = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
            buildWorklist(elseBlock->statements);
        }
        
        // Visit elif branches
        for (auto& elif : ifStmt->elifBranches) {
            if (elif.first) {
                visitExpression(elif.first.get());
            }
            if (auto* elifBlock = dynamic_cast<Block*>(elif.second.get())) {
                buildWorklist(elifBlock->statements);
            }
        }
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        // Add condition as a check
        if (whileStmt->condition) {
            visitExpression(whileStmt->condition.get());
            worklist_.push_back(ConstraintFactOrCheck::makeCheck(
                whileStmt->condition.get(), dfsIn, currentDFSOut_));
            
            // Facts hold inside the loop
            auto facts = extractFacts(whileStmt->condition.get());
            for (const auto& fact : facts) {
                worklist_.push_back(ConstraintFactOrCheck::makeFact(fact, dfsIn, currentDFSOut_));
            }
        }
        
        // Visit body
        if (auto* body = dynamic_cast<Block*>(whileStmt->body.get())) {
            buildWorklist(body->statements);
        }
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
        // For-each loop - visit body
        if (auto* body = dynamic_cast<Block*>(forStmt->body.get())) {
            buildWorklist(body->statements);
        }
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        if (exprStmt->expr) {
            visitExpression(exprStmt->expr.get());
        }
    }
    
    currentDFSOut_ = currentDFSIn_;
}

void ConstraintEliminationPass::visitExpression(Expression* expr) {
    if (!expr) return;
    
    unsigned dfsIn = currentDFSIn_;
    
    // Add comparisons as checks
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        switch (binary->op) {
            case TokenType::LT:
            case TokenType::LE:
            case TokenType::GT:
            case TokenType::GE:
            case TokenType::EQ:
            case TokenType::NE:
                worklist_.push_back(ConstraintFactOrCheck::makeCheck(expr, dfsIn, currentDFSOut_));
                break;
            default:
                break;
        }
        
        visitExpression(binary->left.get());
        visitExpression(binary->right.get());
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        visitExpression(unary->operand.get());
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        for (auto& arg : call->args) {
            visitExpression(arg.get());
        }
    }
    else if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        visitExpression(index->object.get());
        visitExpression(index->index.get());
        
        // Bounds check: index >= 0 is a check we might eliminate
        worklist_.push_back(ConstraintFactOrCheck::makeCheck(index->index.get(), dfsIn, currentDFSOut_));
    }
}

void ConstraintEliminationPass::processWorklist(std::vector<StmtPtr>& stmts) {
    // Sort worklist by DFS in number for proper dominance ordering
    std::sort(worklist_.begin(), worklist_.end(),
              [](const ConstraintFactOrCheck& a, const ConstraintFactOrCheck& b) {
                  return a.domIn < b.domIn;
              });
    
    // Process each item
    for (auto& item : worklist_) {
        if (item.type == ConstraintFactOrCheck::Type::Fact) {
            // Add fact to constraint system
            constraintSystem_.addConstraint(item.constraint);
        }
        else {
            // Try to simplify check
            auto simplified = trySimplifyCheck(item.expr);
            if (simplified) {
                // Mark for transformation
                item.expr = nullptr;  // Will be replaced
                transformations_++;
            }
        }
    }
}

ExprPtr ConstraintEliminationPass::trySimplifyCheck(Expression* expr) {
    if (!expr) return nullptr;
    
    auto constraint = toConstraint(expr);
    if (!constraint) return nullptr;
    
    // Check if constraint is implied (always true)
    auto implied = constraintSystem_.isImplied(*constraint);
    if (implied.has_value() && *implied) {
        return std::make_unique<BoolLiteral>(true, expr->location);
    }
    
    // Check if constraint is contradicted (always false)
    auto contradicted = constraintSystem_.isContradicted(*constraint);
    if (contradicted.has_value() && *contradicted) {
        return std::make_unique<BoolLiteral>(false, expr->location);
    }
    
    return nullptr;
}

std::optional<LinearConstraint> ConstraintEliminationPass::toConstraint(Expression* expr) {
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        return toConstraint(binary);
    }
    return std::nullopt;
}

std::optional<LinearConstraint> ConstraintEliminationPass::toConstraint(BinaryExpr* cmp) {
    if (!cmp) return std::nullopt;
    
    // Try to decompose both sides into linear form
    std::map<std::string, int64_t> lhsCoeffs, rhsCoeffs;
    int64_t lhsConst = 0, rhsConst = 0;
    
    if (!decompose(cmp->left.get(), lhsCoeffs, lhsConst)) return std::nullopt;
    if (!decompose(cmp->right.get(), rhsCoeffs, rhsConst)) return std::nullopt;
    
    // Build constraint: lhs - rhs <= 0 (or < 0 for strict)
    LinearConstraint result;
    result.constant = lhsConst - rhsConst;
    
    // Add LHS coefficients
    for (const auto& [var, coeff] : lhsCoeffs) {
        result.addTerm(var, coeff);
    }
    
    // Subtract RHS coefficients
    for (const auto& [var, coeff] : rhsCoeffs) {
        result.addTerm(var, -coeff);
    }
    
    // Set constraint type based on comparison operator
    switch (cmp->op) {
        case TokenType::LT:
            // x < y  =>  x - y < 0  =>  x - y + 1 <= 0
            result.constant += 1;
            result.isStrict = false;
            break;
            
        case TokenType::LE:
            // x <= y  =>  x - y <= 0
            result.isStrict = false;
            break;
            
        case TokenType::GT:
            // x > y  =>  y - x < 0  =>  y - x + 1 <= 0
            // Negate all coefficients
            result.constant = -result.constant + 1;
            for (auto& [var, coeff] : result.coefficients) {
                coeff = -coeff;
            }
            result.isStrict = false;
            break;
            
        case TokenType::GE:
            // x >= y  =>  y - x <= 0
            result.constant = -result.constant;
            for (auto& [var, coeff] : result.coefficients) {
                coeff = -coeff;
            }
            result.isStrict = false;
            break;
            
        case TokenType::EQ:
        case TokenType::NE:
            // Equality constraints are harder to represent
            // For now, only handle simple cases
            return std::nullopt;
            
        default:
            return std::nullopt;
    }
    
    return result;
}

std::vector<LinearConstraint> ConstraintEliminationPass::extractFacts(Expression* cond, bool negate) {
    std::vector<LinearConstraint> facts;
    
    if (auto* binary = dynamic_cast<BinaryExpr*>(cond)) {
        // Handle logical AND
        if (binary->op == TokenType::AMP_AMP && !negate) {
            auto leftFacts = extractFacts(binary->left.get(), false);
            auto rightFacts = extractFacts(binary->right.get(), false);
            facts.insert(facts.end(), leftFacts.begin(), leftFacts.end());
            facts.insert(facts.end(), rightFacts.begin(), rightFacts.end());
            return facts;
        }
        
        // Handle logical OR when negated (De Morgan's law)
        if (binary->op == TokenType::PIPE_PIPE && negate) {
            auto leftFacts = extractFacts(binary->left.get(), true);
            auto rightFacts = extractFacts(binary->right.get(), true);
            facts.insert(facts.end(), leftFacts.begin(), leftFacts.end());
            facts.insert(facts.end(), rightFacts.begin(), rightFacts.end());
            return facts;
        }
        
        // Handle comparison operators
        auto constraint = toConstraint(binary);
        if (constraint) {
            if (negate) {
                facts.push_back(constraint->negate());
            } else {
                facts.push_back(*constraint);
            }
        }
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(cond)) {
        if (unary->op == TokenType::BANG) {
            // !cond => extract facts with negation flipped
            return extractFacts(unary->operand.get(), !negate);
        }
    }
    
    return facts;
}

void ConstraintEliminationPass::transformStatements(std::vector<StmtPtr>& stmts) {
    for (auto& stmt : stmts) {
        if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
            // Try to simplify condition
            if (ifStmt->condition) {
                auto simplified = trySimplifyCheck(ifStmt->condition.get());
                if (simplified) {
                    ifStmt->condition = std::move(simplified);
                }
            }
            
            // Recurse into branches
            if (auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
                transformStatements(thenBlock->statements);
            }
            if (auto* elseBlock = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
                transformStatements(elseBlock->statements);
            }
            for (auto& elif : ifStmt->elifBranches) {
                if (auto* elifBlock = dynamic_cast<Block*>(elif.second.get())) {
                    transformStatements(elifBlock->statements);
                }
            }
        }
        else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt.get())) {
            if (whileStmt->condition) {
                auto simplified = trySimplifyCheck(whileStmt->condition.get());
                if (simplified) {
                    whileStmt->condition = std::move(simplified);
                }
            }
            
            if (auto* body = dynamic_cast<Block*>(whileStmt->body.get())) {
                transformStatements(body->statements);
            }
        }
        else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt.get())) {
            if (auto* body = dynamic_cast<Block*>(forStmt->body.get())) {
                transformStatements(body->statements);
            }
        }
    }
}

bool ConstraintEliminationPass::decompose(Expression* expr, 
                                          std::map<std::string, int64_t>& coeffs, 
                                          int64_t& constant) {
    if (!expr) return false;
    
    if (auto* lit = dynamic_cast<IntegerLiteral*>(expr)) {
        constant = lit->value;
        return true;
    }
    
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        coeffs[ident->name] = 1;
        constant = 0;
        return true;
    }
    
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        if (unary->op == TokenType::MINUS) {
            if (!decompose(unary->operand.get(), coeffs, constant)) return false;
            constant = -constant;
            for (auto& [var, coeff] : coeffs) {
                coeff = -coeff;
            }
            return true;
        }
        return false;
    }
    
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        std::map<std::string, int64_t> lhsCoeffs, rhsCoeffs;
        int64_t lhsConst = 0, rhsConst = 0;
        
        if (!decompose(binary->left.get(), lhsCoeffs, lhsConst)) return false;
        if (!decompose(binary->right.get(), rhsCoeffs, rhsConst)) return false;
        
        switch (binary->op) {
            case TokenType::PLUS:
                constant = lhsConst + rhsConst;
                coeffs = lhsCoeffs;
                for (const auto& [var, coeff] : rhsCoeffs) {
                    coeffs[var] += coeff;
                    if (coeffs[var] == 0) coeffs.erase(var);
                }
                return true;
                
            case TokenType::MINUS:
                constant = lhsConst - rhsConst;
                coeffs = lhsCoeffs;
                for (const auto& [var, coeff] : rhsCoeffs) {
                    coeffs[var] -= coeff;
                    if (coeffs[var] == 0) coeffs.erase(var);
                }
                return true;
                
            case TokenType::STAR:
                // Only handle constant * variable or variable * constant
                if (lhsCoeffs.empty() && rhsCoeffs.size() == 1) {
                    // constant * variable
                    constant = lhsConst * rhsConst;
                    for (const auto& [var, coeff] : rhsCoeffs) {
                        coeffs[var] = lhsConst * coeff;
                    }
                    return true;
                }
                if (rhsCoeffs.empty() && lhsCoeffs.size() == 1) {
                    // variable * constant
                    constant = lhsConst * rhsConst;
                    for (const auto& [var, coeff] : lhsCoeffs) {
                        coeffs[var] = rhsConst * coeff;
                    }
                    return true;
                }
                // Non-linear - can't decompose
                return false;
                
            default:
                return false;
        }
    }
    
    return false;
}

std::optional<std::string> ConstraintEliminationPass::getVariableName(Expression* expr) {
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        return ident->name;
    }
    return std::nullopt;
}

std::optional<int64_t> ConstraintEliminationPass::evaluateConstant(Expression* expr) {
    if (auto* lit = dynamic_cast<IntegerLiteral*>(expr)) {
        return lit->value;
    }
    if (auto* boolLit = dynamic_cast<BoolLiteral*>(expr)) {
        return boolLit->value ? 1 : 0;
    }
    
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

} // namespace tyl
