// Tyl Compiler - Jump Threading Implementation
#include "jump_threading.h"
#include <algorithm>
#include <iostream>

namespace tyl {

void JumpThreadingPass::run(Program& ast) {
    transformations_ = 0;
    stats_ = JumpThreadingStats{};
    
    // Process all functions in the program
    for (auto& stmt : ast.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            processFunction(fn);
        }
    }
    
    // Sum up all transformations
    transformations_ = stats_.jumpsThreaded +
                       stats_.conditionsFolded +
                       stats_.blocksEliminated +
                       stats_.phiNodesSimplified;
}

void JumpThreadingPass::processFunction(FnDecl* fn) {
    if (!fn->body) return;
    
    // Clear known values at function entry
    clearKnownValues();
    
    // Process the function body
    if (auto* block = dynamic_cast<Block*>(fn->body.get())) {
        processBlock(block->statements);
    }
}

void JumpThreadingPass::processBlock(std::vector<StmtPtr>& stmts) {
    std::vector<StmtPtr> insertBefore;
    
    for (size_t i = 0; i < stmts.size(); ++i) {
        insertBefore.clear();
        
        if (processStatement(stmts[i], insertBefore)) {
            // Statement was modified
        }
        
        // Insert any statements that need to go before current
        if (!insertBefore.empty()) {
            stmts.insert(stmts.begin() + i, 
                        std::make_move_iterator(insertBefore.begin()),
                        std::make_move_iterator(insertBefore.end()));
            i += insertBefore.size();
        }
        
        // Handle statement that became null (removed)
        if (!stmts[i]) {
            stmts.erase(stmts.begin() + i);
            --i;
        }
    }
}

bool JumpThreadingPass::processStatement(StmtPtr& stmt, std::vector<StmtPtr>& insertBefore) {
    if (!stmt) return false;
    
    bool changed = false;
    
    // Track values from variable declarations
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt.get())) {
        if (varDecl->initializer) {
            KnownValue kv;
            kv.varName = varDecl->name;
            
            if (auto* intLit = dynamic_cast<IntegerLiteral*>(varDecl->initializer.get())) {
                kv.isConstant = true;
                kv.type = KnownValue::Type::Integer;
                kv.intValue = intLit->value;
                recordKnownValue(varDecl->name, kv);
            } else if (auto* boolLit = dynamic_cast<BoolLiteral*>(varDecl->initializer.get())) {
                kv.isConstant = true;
                kv.type = KnownValue::Type::Boolean;
                kv.boolValue = boolLit->value;
                recordKnownValue(varDecl->name, kv);
            }
        }
    }
    
    // Track values from assignments
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt.get())) {
        if (auto* assign = dynamic_cast<AssignExpr*>(exprStmt->expr.get())) {
            if (auto* target = dynamic_cast<Identifier*>(assign->target.get())) {
                if (assign->op == TokenType::ASSIGN) {
                    KnownValue kv;
                    kv.varName = target->name;
                    
                    if (auto* intLit = dynamic_cast<IntegerLiteral*>(assign->value.get())) {
                        kv.isConstant = true;
                        kv.type = KnownValue::Type::Integer;
                        kv.intValue = intLit->value;
                        recordKnownValue(target->name, kv);
                    } else if (auto* boolLit = dynamic_cast<BoolLiteral*>(assign->value.get())) {
                        kv.isConstant = true;
                        kv.type = KnownValue::Type::Boolean;
                        kv.boolValue = boolLit->value;
                        recordKnownValue(target->name, kv);
                    } else {
                        // Assignment to non-constant - invalidate
                        knownValues_.erase(target->name);
                    }
                }
            }
        }
    }
    
    // Try jump threading on if statements
    if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
        // Check if we can determine the condition from known values
        bool condResult;
        if (canDetermineCondition(ifStmt->condition.get(), condResult)) {
            // Thread the jump - replace if with the appropriate branch
            if (condResult) {
                // Condition is true - take then branch
                stmt = std::move(ifStmt->thenBranch);
                ++stats_.jumpsThreaded;
                ++stats_.conditionsFolded;
                changed = true;
            } else {
                // Condition is false - take else branch (or remove)
                if (ifStmt->elseBranch) {
                    stmt = std::move(ifStmt->elseBranch);
                } else {
                    stmt = nullptr;
                }
                ++stats_.jumpsThreaded;
                ++stats_.conditionsFolded;
                changed = true;
            }
        } else {
            // Can't determine condition - but record implied values for branches
            auto savedValues = knownValues_;
            
            // Process then branch with condition being true
            recordImpliedValues(ifStmt->condition.get(), true);
            if (ifStmt->thenBranch) {
                if (auto* block = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
                    processBlock(block->statements);
                } else {
                    std::vector<StmtPtr> dummy;
                    processStatement(ifStmt->thenBranch, dummy);
                }
            }
            
            // Restore and process else branch with condition being false
            knownValues_ = savedValues;
            recordImpliedValues(ifStmt->condition.get(), false);
            if (ifStmt->elseBranch) {
                if (auto* block = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
                    processBlock(block->statements);
                } else {
                    std::vector<StmtPtr> dummy;
                    processStatement(ifStmt->elseBranch, dummy);
                }
            }
            
            // Process elif branches
            for (auto& elif : ifStmt->elifBranches) {
                knownValues_ = savedValues;
                if (elif.second) {
                    if (auto* block = dynamic_cast<Block*>(elif.second.get())) {
                        processBlock(block->statements);
                    }
                }
            }
            
            // After if-else, we don't know which branch was taken
            // Clear values that might have been modified
            knownValues_ = savedValues;
        }
    }
    
    // Process nested structures
    if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt.get())) {
        // Clear values at loop entry (conservative)
        clearKnownValues();
        if (whileStmt->body) {
            if (auto* block = dynamic_cast<Block*>(whileStmt->body.get())) {
                processBlock(block->statements);
            }
        }
        clearKnownValues();
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt.get())) {
        clearKnownValues();
        if (forStmt->body) {
            if (auto* block = dynamic_cast<Block*>(forStmt->body.get())) {
                processBlock(block->statements);
            }
        }
        clearKnownValues();
    }
    else if (auto* block = dynamic_cast<Block*>(stmt.get())) {
        processBlock(block->statements);
    }
    else if (auto* matchStmt = dynamic_cast<MatchStmt*>(stmt.get())) {
        auto savedValues = knownValues_;
        for (auto& c : matchStmt->cases) {
            knownValues_ = savedValues;
            if (c.body) {
                if (auto* block = dynamic_cast<Block*>(c.body.get())) {
                    processBlock(block->statements);
                }
            }
        }
        knownValues_ = savedValues;
    }
    
    return changed;
}

bool JumpThreadingPass::canDetermineCondition(Expression* cond, bool& result) {
    // First try direct evaluation
    if (evaluateCondition(cond, result)) {
        return true;
    }
    
    // Then try range-based analysis
    if (canDetermineFromRange(cond, result)) {
        return true;
    }
    
    return false;
}

bool JumpThreadingPass::evaluateCondition(Expression* cond, bool& result) {
    if (!cond) return false;
    
    // Direct boolean literal
    if (auto* boolLit = dynamic_cast<BoolLiteral*>(cond)) {
        result = boolLit->value;
        return true;
    }
    
    // Boolean variable with known value
    if (auto* id = dynamic_cast<Identifier*>(cond)) {
        KnownValue kv;
        if (getKnownValue(id->name, kv) && kv.type == KnownValue::Type::Boolean) {
            result = kv.boolValue;
            return true;
        }
    }
    
    // Negation
    if (auto* unary = dynamic_cast<UnaryExpr*>(cond)) {
        if (unary->op == TokenType::NOT || unary->op == TokenType::BANG) {
            bool innerResult;
            if (evaluateCondition(unary->operand.get(), innerResult)) {
                result = !innerResult;
                return true;
            }
        }
    }
    
    // Binary comparison with known values
    if (auto* binary = dynamic_cast<BinaryExpr*>(cond)) {
        // Try to evaluate both sides
        int64_t leftVal, rightVal;
        bool leftKnown = false, rightKnown = false;
        
        if (auto* leftInt = dynamic_cast<IntegerLiteral*>(binary->left.get())) {
            leftVal = leftInt->value;
            leftKnown = true;
        } else if (auto* leftId = dynamic_cast<Identifier*>(binary->left.get())) {
            KnownValue kv;
            if (getKnownValue(leftId->name, kv) && kv.type == KnownValue::Type::Integer) {
                leftVal = kv.intValue;
                leftKnown = true;
            }
        }
        
        if (auto* rightInt = dynamic_cast<IntegerLiteral*>(binary->right.get())) {
            rightVal = rightInt->value;
            rightKnown = true;
        } else if (auto* rightId = dynamic_cast<Identifier*>(binary->right.get())) {
            KnownValue kv;
            if (getKnownValue(rightId->name, kv) && kv.type == KnownValue::Type::Integer) {
                rightVal = kv.intValue;
                rightKnown = true;
            }
        }
        
        if (leftKnown && rightKnown) {
            switch (binary->op) {
                case TokenType::EQ:
                    result = (leftVal == rightVal);
                    return true;
                case TokenType::NE:
                    result = (leftVal != rightVal);
                    return true;
                case TokenType::LT:
                    result = (leftVal < rightVal);
                    return true;
                case TokenType::LE:
                    result = (leftVal <= rightVal);
                    return true;
                case TokenType::GT:
                    result = (leftVal > rightVal);
                    return true;
                case TokenType::GE:
                    result = (leftVal >= rightVal);
                    return true;
                default:
                    break;
            }
        }
        
        // Logical AND/OR with partial evaluation
        if (binary->op == TokenType::AND) {
            bool leftResult;
            if (evaluateCondition(binary->left.get(), leftResult)) {
                if (!leftResult) {
                    result = false;  // false && anything = false
                    return true;
                }
                // left is true, result depends on right
                return evaluateCondition(binary->right.get(), result);
            }
        }
        
        if (binary->op == TokenType::OR) {
            bool leftResult;
            if (evaluateCondition(binary->left.get(), leftResult)) {
                if (leftResult) {
                    result = true;  // true || anything = true
                    return true;
                }
                // left is false, result depends on right
                return evaluateCondition(binary->right.get(), result);
            }
        }
    }
    
    return false;
}

void JumpThreadingPass::recordKnownValue(const std::string& var, const KnownValue& value) {
    knownValues_[var] = value;
}

bool JumpThreadingPass::getKnownValue(const std::string& var, KnownValue& value) {
    auto it = knownValues_.find(var);
    if (it != knownValues_.end()) {
        value = it->second;
        return true;
    }
    return false;
}

void JumpThreadingPass::clearKnownValues() {
    knownValues_.clear();
}

void JumpThreadingPass::recordImpliedValues(Expression* cond, bool condValue) {
    if (!cond) return;
    
    // Boolean variable: if (x) implies x = true in then branch
    if (auto* id = dynamic_cast<Identifier*>(cond)) {
        KnownValue kv;
        kv.varName = id->name;
        kv.isConstant = true;
        kv.type = KnownValue::Type::Boolean;
        kv.boolValue = condValue;
        recordKnownValue(id->name, kv);
        return;
    }
    
    // Negation: if (!x) implies x = false in then branch
    if (auto* unary = dynamic_cast<UnaryExpr*>(cond)) {
        if (unary->op == TokenType::NOT || unary->op == TokenType::BANG) {
            recordImpliedValues(unary->operand.get(), !condValue);
            return;
        }
    }
    
    // Comparison operators - record both exact values and ranges
    if (auto* binary = dynamic_cast<BinaryExpr*>(cond)) {
        // Equality comparison: if (x == 5) implies x = 5 in then branch
        if (binary->op == TokenType::EQ && condValue) {
            // x == const
            if (auto* leftId = dynamic_cast<Identifier*>(binary->left.get())) {
                if (auto* rightInt = dynamic_cast<IntegerLiteral*>(binary->right.get())) {
                    KnownValue kv;
                    kv.varName = leftId->name;
                    kv.isConstant = true;
                    kv.type = KnownValue::Type::Integer;
                    kv.intValue = rightInt->value;
                    kv.hasRange = true;
                    kv.minValue = rightInt->value;
                    kv.maxValue = rightInt->value;
                    recordKnownValue(leftId->name, kv);
                }
            }
            // const == x
            if (auto* rightId = dynamic_cast<Identifier*>(binary->right.get())) {
                if (auto* leftInt = dynamic_cast<IntegerLiteral*>(binary->left.get())) {
                    KnownValue kv;
                    kv.varName = rightId->name;
                    kv.isConstant = true;
                    kv.type = KnownValue::Type::Integer;
                    kv.intValue = leftInt->value;
                    kv.hasRange = true;
                    kv.minValue = leftInt->value;
                    kv.maxValue = leftInt->value;
                    recordKnownValue(rightId->name, kv);
                }
            }
        }
        
        // Inequality: if (x != 5) is true, we know x is not 5
        // This is harder to use but can help with range analysis
        
        // Range-based implications
        recordRangeFromComparison(cond, condValue);
        
        // Logical AND: if (a && b) is true, both a and b are true
        if (binary->op == TokenType::AND && condValue) {
            recordImpliedValues(binary->left.get(), true);
            recordImpliedValues(binary->right.get(), true);
        }
        
        // Logical OR: if (a || b) is false, both a and b are false
        if (binary->op == TokenType::OR && !condValue) {
            recordImpliedValues(binary->left.get(), false);
            recordImpliedValues(binary->right.get(), false);
        }
    }
}

void JumpThreadingPass::recordRangeFromComparison(Expression* cond, bool condValue) {
    auto* binary = dynamic_cast<BinaryExpr*>(cond);
    if (!binary) return;
    
    std::string var;
    int64_t constVal;
    bool varOnLeft = false;
    
    // Extract variable and constant
    if (auto* leftId = dynamic_cast<Identifier*>(binary->left.get())) {
        if (auto* rightInt = dynamic_cast<IntegerLiteral*>(binary->right.get())) {
            var = leftId->name;
            constVal = rightInt->value;
            varOnLeft = true;
        }
    }
    if (var.empty()) {
        if (auto* rightId = dynamic_cast<Identifier*>(binary->right.get())) {
            if (auto* leftInt = dynamic_cast<IntegerLiteral*>(binary->left.get())) {
                var = rightId->name;
                constVal = leftInt->value;
                varOnLeft = false;
            }
        }
    }
    
    if (var.empty()) return;
    
    // Get or create range info
    KnownValue kv;
    auto it = knownValues_.find(var);
    if (it != knownValues_.end()) {
        kv = it->second;
    } else {
        kv.varName = var;
        kv.type = KnownValue::Type::Integer;
        kv.hasRange = true;
        kv.minValue = INT64_MIN;
        kv.maxValue = INT64_MAX;
    }
    
    // Update range based on comparison
    TokenType op = binary->op;
    if (!varOnLeft) {
        // Flip the comparison
        switch (op) {
            case TokenType::LT: op = TokenType::GT; break;
            case TokenType::LE: op = TokenType::GE; break;
            case TokenType::GT: op = TokenType::LT; break;
            case TokenType::GE: op = TokenType::LE; break;
            default: break;
        }
    }
    
    if (condValue) {
        // Condition is true
        switch (op) {
            case TokenType::LT:  // x < c  =>  x <= c-1
                kv.maxValue = std::min(kv.maxValue, constVal - 1);
                break;
            case TokenType::LE:  // x <= c
                kv.maxValue = std::min(kv.maxValue, constVal);
                break;
            case TokenType::GT:  // x > c  =>  x >= c+1
                kv.minValue = std::max(kv.minValue, constVal + 1);
                break;
            case TokenType::GE:  // x >= c
                kv.minValue = std::max(kv.minValue, constVal);
                break;
            default:
                break;
        }
    } else {
        // Condition is false - negate the comparison
        switch (op) {
            case TokenType::LT:  // !(x < c)  =>  x >= c
                kv.minValue = std::max(kv.minValue, constVal);
                break;
            case TokenType::LE:  // !(x <= c)  =>  x > c  =>  x >= c+1
                kv.minValue = std::max(kv.minValue, constVal + 1);
                break;
            case TokenType::GT:  // !(x > c)  =>  x <= c
                kv.maxValue = std::min(kv.maxValue, constVal);
                break;
            case TokenType::GE:  // !(x >= c)  =>  x < c  =>  x <= c-1
                kv.maxValue = std::min(kv.maxValue, constVal - 1);
                break;
            default:
                break;
        }
    }
    
    kv.hasRange = true;
    recordKnownValue(var, kv);
    ++stats_.rangeBasedOptimizations;
}

bool JumpThreadingPass::canDetermineFromRange(Expression* cond, bool& result) {
    auto* binary = dynamic_cast<BinaryExpr*>(cond);
    if (!binary) return false;
    
    std::string var;
    int64_t constVal;
    bool varOnLeft = false;
    
    // Extract variable and constant
    if (auto* leftId = dynamic_cast<Identifier*>(binary->left.get())) {
        if (auto* rightInt = dynamic_cast<IntegerLiteral*>(binary->right.get())) {
            var = leftId->name;
            constVal = rightInt->value;
            varOnLeft = true;
        }
    }
    if (var.empty()) {
        if (auto* rightId = dynamic_cast<Identifier*>(binary->right.get())) {
            if (auto* leftInt = dynamic_cast<IntegerLiteral*>(binary->left.get())) {
                var = rightId->name;
                constVal = leftInt->value;
                varOnLeft = false;
            }
        }
    }
    
    if (var.empty()) return false;
    
    // Get range info
    KnownValue kv;
    if (!getKnownValue(var, kv) || !kv.hasRange) return false;
    
    TokenType op = binary->op;
    if (!varOnLeft) {
        switch (op) {
            case TokenType::LT: op = TokenType::GT; break;
            case TokenType::LE: op = TokenType::GE; break;
            case TokenType::GT: op = TokenType::LT; break;
            case TokenType::GE: op = TokenType::LE; break;
            default: break;
        }
    }
    
    // Check if range implies the comparison result
    switch (op) {
        case TokenType::LT:  // x < c
            if (kv.maxValue < constVal) { result = true; return true; }
            if (kv.minValue >= constVal) { result = false; return true; }
            break;
        case TokenType::LE:  // x <= c
            if (kv.maxValue <= constVal) { result = true; return true; }
            if (kv.minValue > constVal) { result = false; return true; }
            break;
        case TokenType::GT:  // x > c
            if (kv.minValue > constVal) { result = true; return true; }
            if (kv.maxValue <= constVal) { result = false; return true; }
            break;
        case TokenType::GE:  // x >= c
            if (kv.minValue >= constVal) { result = true; return true; }
            if (kv.maxValue < constVal) { result = false; return true; }
            break;
        default:
            break;
    }
    
    return false;
}

bool JumpThreadingPass::areConditionsCorrelated(Expression* a, Expression* b, bool& impliedValue) {
    // Check if condition a being true/false implies something about condition b
    
    auto* binA = dynamic_cast<BinaryExpr*>(a);
    auto* binB = dynamic_cast<BinaryExpr*>(b);
    
    if (!binA || !binB) return false;
    
    // Extract variables and constants from both conditions
    std::string varA, varB;
    int64_t constA = 0, constB = 0;
    
    if (auto* leftId = dynamic_cast<Identifier*>(binA->left.get())) {
        if (auto* rightInt = dynamic_cast<IntegerLiteral*>(binA->right.get())) {
            varA = leftId->name;
            constA = rightInt->value;
        }
    }
    
    if (auto* leftId = dynamic_cast<Identifier*>(binB->left.get())) {
        if (auto* rightInt = dynamic_cast<IntegerLiteral*>(binB->right.get())) {
            varB = leftId->name;
            constB = rightInt->value;
        }
    }
    
    // Must be same variable
    if (varA.empty() || varA != varB) return false;
    
    // Check for implications
    // x < 5 implies x < 10
    if (binA->op == TokenType::LT && binB->op == TokenType::LT) {
        if (constA < constB) {
            impliedValue = true;  // If x < 5, then x < 10
            ++stats_.correlatedConditionsFound;
            return true;
        }
    }
    
    // x > 10 implies x > 5
    if (binA->op == TokenType::GT && binB->op == TokenType::GT) {
        if (constA > constB) {
            impliedValue = true;
            ++stats_.correlatedConditionsFound;
            return true;
        }
    }
    
    // x < 5 implies !(x > 10)
    if (binA->op == TokenType::LT && binB->op == TokenType::GT) {
        if (constA <= constB) {
            impliedValue = false;
            ++stats_.correlatedConditionsFound;
            return true;
        }
    }
    
    // x > 10 implies !(x < 5)
    if (binA->op == TokenType::GT && binB->op == TokenType::LT) {
        if (constA >= constB) {
            impliedValue = false;
            ++stats_.correlatedConditionsFound;
            return true;
        }
    }
    
    return false;
}

void JumpThreadingPass::mergeRanges(const std::map<std::string, KnownValue>& other) {
    // At join points, we need to widen ranges
    for (auto& [var, kv] : knownValues_) {
        auto it = other.find(var);
        if (it != other.end() && it->second.hasRange && kv.hasRange) {
            // Widen the range to include both possibilities
            kv.minValue = std::min(kv.minValue, it->second.minValue);
            kv.maxValue = std::max(kv.maxValue, it->second.maxValue);
            
            // If ranges don't overlap, we can't determine a constant
            if (kv.minValue != kv.maxValue) {
                kv.isConstant = false;
            }
        } else {
            // One path doesn't have range info - invalidate
            kv.hasRange = false;
            kv.isConstant = false;
        }
    }
}

bool JumpThreadingPass::isSimpleComparison(Expression* cond, std::string& var,
                                           TokenType& op, int64_t& value) {
    auto* binary = dynamic_cast<BinaryExpr*>(cond);
    if (!binary) return false;
    
    // Check for comparison operators
    if (binary->op != TokenType::EQ && binary->op != TokenType::NE &&
        binary->op != TokenType::LT && binary->op != TokenType::LE &&
        binary->op != TokenType::GT && binary->op != TokenType::GE) {
        return false;
    }
    
    // var op const
    if (auto* leftId = dynamic_cast<Identifier*>(binary->left.get())) {
        if (auto* rightInt = dynamic_cast<IntegerLiteral*>(binary->right.get())) {
            var = leftId->name;
            op = binary->op;
            value = rightInt->value;
            return true;
        }
    }
    
    // const op var (swap)
    if (auto* rightId = dynamic_cast<Identifier*>(binary->right.get())) {
        if (auto* leftInt = dynamic_cast<IntegerLiteral*>(binary->left.get())) {
            var = rightId->name;
            value = leftInt->value;
            // Swap the operator
            switch (binary->op) {
                case TokenType::LT: op = TokenType::GT; break;
                case TokenType::LE: op = TokenType::GE; break;
                case TokenType::GT: op = TokenType::LT; break;
                case TokenType::GE: op = TokenType::LE; break;
                default: op = binary->op; break;
            }
            return true;
        }
    }
    
    return false;
}

bool JumpThreadingPass::isBooleanVar(Expression* cond, std::string& var) {
    if (auto* id = dynamic_cast<Identifier*>(cond)) {
        var = id->name;
        return true;
    }
    return false;
}

StmtPtr JumpThreadingPass::cloneStatement(Statement* stmt) {
    if (!stmt) return nullptr;
    
    if (auto* ret = dynamic_cast<ReturnStmt*>(stmt)) {
        return std::make_unique<ReturnStmt>(cloneExpression(ret->value.get()), ret->location);
    }
    
    if (auto* expr = dynamic_cast<ExprStmt*>(stmt)) {
        return std::make_unique<ExprStmt>(cloneExpression(expr->expr.get()), expr->location);
    }
    
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        auto newBlock = std::make_unique<Block>(block->location);
        for (auto& s : block->statements) {
            if (auto cloned = cloneStatement(s.get())) {
                newBlock->statements.push_back(std::move(cloned));
            }
        }
        return newBlock;
    }
    
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        auto newVar = std::make_unique<VarDecl>(
            varDecl->name, varDecl->typeName,
            cloneExpression(varDecl->initializer.get()),
            varDecl->location);
        newVar->isMutable = varDecl->isMutable;
        newVar->isConst = varDecl->isConst;
        return newVar;
    }
    
    return nullptr;
}

ExprPtr JumpThreadingPass::cloneExpression(Expression* expr) {
    if (!expr) return nullptr;
    
    if (auto* lit = dynamic_cast<IntegerLiteral*>(expr)) {
        return std::make_unique<IntegerLiteral>(lit->value, lit->location, lit->suffix);
    }
    if (auto* lit = dynamic_cast<FloatLiteral*>(expr)) {
        return std::make_unique<FloatLiteral>(lit->value, lit->location, lit->suffix);
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
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        auto newCall = std::make_unique<CallExpr>(
            cloneExpression(call->callee.get()),
            call->location);
        for (auto& arg : call->args) {
            newCall->args.push_back(cloneExpression(arg.get()));
        }
        return newCall;
    }
    if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        return std::make_unique<MemberExpr>(
            cloneExpression(member->object.get()),
            member->member,
            member->location);
    }
    if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        return std::make_unique<IndexExpr>(
            cloneExpression(index->object.get()),
            cloneExpression(index->index.get()),
            index->location);
    }
    if (auto* walrus = dynamic_cast<WalrusExpr*>(expr)) {
        return std::make_unique<WalrusExpr>(
            walrus->varName,
            cloneExpression(walrus->value.get()),
            walrus->location);
    }
    
    return nullptr;
}

} // namespace tyl
