// Tyl Compiler - Constant Propagation Implementation
// Tracks variable values and eliminates redundant comparisons
#include "constant_propagation.h"
#include <algorithm>

namespace tyl {

void ConstantPropagationPass::run(Program& ast) {
    transformations_ = 0;
    knownValues_.clear();
    modifiedVars_.clear();
    mutableValues_.clear();
    
    // Process each function body separately
    for (auto& stmt : ast.statements) {
        if (auto* fnDecl = dynamic_cast<FnDecl*>(stmt.get())) {
            if (auto* body = dynamic_cast<Block*>(fnDecl->body.get())) {
                // First pass: optimize accumulator patterns (handles unrolled loops)
                optimizeAccumulators(body->statements);
                
                // Second pass: standard constant propagation
                knownValues_.clear();
                modifiedVars_.clear();
                processBlock(body->statements);
                
                // Third pass: Dead Store Elimination
                eliminateDeadStores(body->statements);
            }
        }
    }
    
    // Also process top-level statements (outside functions)
    optimizeAccumulators(ast.statements);
    knownValues_.clear();
    modifiedVars_.clear();
    processBlock(ast.statements);
    eliminateDeadStores(ast.statements);
}

void ConstantPropagationPass::eliminateDeadStores(std::vector<StmtPtr>& statements) {
    // First, flatten single-statement blocks into the parent
    // This handles cases where if-elimination left Block wrappers
    bool changed = true;
    while (changed) {
        changed = false;
        std::vector<StmtPtr> flattened;
        for (auto& stmt : statements) {
            if (auto* block = dynamic_cast<Block*>(stmt.get())) {
                // Flatten the block's contents into parent
                for (auto& s : block->statements) {
                    flattened.push_back(std::move(s));
                }
                changed = true;
            } else {
                flattened.push_back(std::move(stmt));
            }
        }
        statements = std::move(flattened);
    }
    
    // Recursively process nested structures (functions, etc.)
    for (auto& stmt : statements) {
        if (auto* fnDecl = dynamic_cast<FnDecl*>(stmt.get())) {
            if (auto* body = dynamic_cast<Block*>(fnDecl->body.get())) {
                eliminateDeadStores(body->statements);
            }
        }
    }
    
    // Now do dead store elimination on the flattened list
    std::map<std::string, size_t> lastAssignIdx;
    std::map<std::string, bool> wasRead;
    std::set<size_t> deadStores;
    
    // Helper to mark variables as read
    std::function<void(Expression*)> markReads = [&](Expression* expr) {
        if (!expr) return;
        if (auto* id = dynamic_cast<Identifier*>(expr)) {
            wasRead[id->name] = true;
        } else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
            markReads(binary->left.get());
            markReads(binary->right.get());
        } else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
            markReads(unary->operand.get());
        } else if (auto* call = dynamic_cast<CallExpr*>(expr)) {
            markReads(call->callee.get());
            for (auto& arg : call->args) {
                markReads(arg.get());
            }
        } else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
            markReads(ternary->condition.get());
            markReads(ternary->thenExpr.get());
            markReads(ternary->elseExpr.get());
        }
    };
    
    // Scan backwards
    for (size_t i = statements.size(); i > 0; --i) {
        size_t idx = i - 1;
        auto* stmt = statements[idx].get();
        
        if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
            if (auto* target = dynamic_cast<Identifier*>(assignStmt->target.get())) {
                const std::string& varName = target->name;
                
                if (lastAssignIdx.count(varName) && !wasRead[varName]) {
                    deadStores.insert(idx);
                    transformations_++;
                }
                
                lastAssignIdx[varName] = idx;
                wasRead[varName] = false;
                markReads(assignStmt->value.get());
                
                // For compound assignments (+=, -=, etc.), the target is also read
                if (assignStmt->op != TokenType::ASSIGN) {
                    wasRead[varName] = true;
                }
            }
        } else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
            const std::string& varName = varDecl->name;
            
            if (lastAssignIdx.count(varName) && !wasRead[varName] && varDecl->initializer) {
                // Initializer is dead - simplify it
                varDecl->initializer = std::make_unique<IntegerLiteral>(0, varDecl->initializer->location);
                transformations_++;
            }
            
            wasRead[varName] = true;
            if (varDecl->initializer) {
                markReads(varDecl->initializer.get());
            }
        } else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
            markReads(exprStmt->expr.get());
        } else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
            if (returnStmt->value) {
                markReads(returnStmt->value.get());
            }
        }
    }
    
    // Remove dead stores
    if (!deadStores.empty()) {
        std::vector<StmtPtr> newStatements;
        for (size_t i = 0; i < statements.size(); ++i) {
            if (deadStores.find(i) == deadStores.end()) {
                newStatements.push_back(std::move(statements[i]));
            }
        }
        statements = std::move(newStatements);
    }
}

void ConstantPropagationPass::optimizeAccumulators(std::vector<StmtPtr>& statements) {
    // Track mutable variable values through sequential assignments
    // This handles patterns like: mut x = 0; x = x + 1; x = x + 1; ... -> mut x = N
    // Also handles: mut x = 0; x += 10; x += 11; x += 12; ... -> mut x = final_value
    // IMPORTANT: Only optimize if the variable is NOT read between assignments
    
    std::map<std::string, int64_t> accumulators;  // var -> current value
    std::map<std::string, size_t> declIdx;        // var -> index of declaration
    std::map<std::string, size_t> firstAssignIdx; // var -> index of first compound assignment
    std::map<std::string, size_t> lastAssignIdx;  // var -> index of last compound assignment
    std::map<std::string, bool> canOptimize;      // var -> can we optimize this?
    std::map<std::string, std::set<size_t>> readAtIdx;  // var -> indices where it's read (not in self-assignment)
    
    // Helper to collect variable reads from an expression
    std::function<void(Expression*, const std::string&, size_t)> collectReads = [&](Expression* expr, const std::string& excludeVar, size_t idx) {
        if (!expr) return;
        if (auto* id = dynamic_cast<Identifier*>(expr)) {
            if (id->name != excludeVar) {
                readAtIdx[id->name].insert(idx);
            }
        }
        else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
            collectReads(binary->left.get(), excludeVar, idx);
            collectReads(binary->right.get(), excludeVar, idx);
        }
        else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
            collectReads(unary->operand.get(), excludeVar, idx);
        }
        else if (auto* call = dynamic_cast<CallExpr*>(expr)) {
            collectReads(call->callee.get(), excludeVar, idx);
            for (auto& arg : call->args) {
                collectReads(arg.get(), excludeVar, idx);
            }
        }
        else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
            collectReads(ternary->condition.get(), excludeVar, idx);
            collectReads(ternary->thenExpr.get(), excludeVar, idx);
            collectReads(ternary->elseExpr.get(), excludeVar, idx);
        }
        else if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
            collectReads(index->object.get(), excludeVar, idx);
            collectReads(index->index.get(), excludeVar, idx);
        }
    };
    
    // First pass: identify accumulator patterns and track all reads
    for (size_t i = 0; i < statements.size(); ++i) {
        auto* stmt = statements[i].get();
        
        // Track variable declarations with initial value
        if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
            if (varDecl->isMutable && varDecl->initializer) {
                if (auto* intLit = dynamic_cast<IntegerLiteral*>(varDecl->initializer.get())) {
                    accumulators[varDecl->name] = intLit->value;
                    declIdx[varDecl->name] = i;
                    canOptimize[varDecl->name] = true;
                }
            }
            // Check if initializer reads any variables
            if (varDecl->initializer) {
                collectReads(varDecl->initializer.get(), varDecl->name, i);
            }
        }
        // Track compound assignments (x = x + 1, x += 1, x = x * 2, etc.)
        else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
            if (auto* target = dynamic_cast<Identifier*>(assignStmt->target.get())) {
                const std::string& varName = target->name;
                
                // Check if this is a compound assignment we can track
                if (canOptimize.count(varName) && canOptimize[varName]) {
                    bool isAccumPattern = false;
                    int64_t newValue = accumulators[varName];
                    
                    // Handle x += N
                    if (assignStmt->op == TokenType::PLUS_ASSIGN) {
                        if (auto* intLit = dynamic_cast<IntegerLiteral*>(assignStmt->value.get())) {
                            isAccumPattern = true;
                            newValue += intLit->value;
                        }
                    }
                    // Handle x -= N
                    else if (assignStmt->op == TokenType::MINUS_ASSIGN) {
                        if (auto* intLit = dynamic_cast<IntegerLiteral*>(assignStmt->value.get())) {
                            isAccumPattern = true;
                            newValue -= intLit->value;
                        }
                    }
                    // Handle x *= N
                    else if (assignStmt->op == TokenType::STAR_ASSIGN) {
                        if (auto* intLit = dynamic_cast<IntegerLiteral*>(assignStmt->value.get())) {
                            isAccumPattern = true;
                            newValue *= intLit->value;
                        }
                    }
                    // Handle x = N (direct assignment to constant)
                    else if (assignStmt->op == TokenType::ASSIGN) {
                        if (auto* intLit = dynamic_cast<IntegerLiteral*>(assignStmt->value.get())) {
                            // Direct assignment to constant - this breaks the accumulator pattern
                            canOptimize[varName] = false;
                            continue;
                        }
                        // Handle x = x + N or x = N + x
                        if (auto* binary = dynamic_cast<BinaryExpr*>(assignStmt->value.get())) {
                            if (binary->op == TokenType::PLUS) {
                                // Check x = x + N
                                if (auto* leftId = dynamic_cast<Identifier*>(binary->left.get())) {
                                    if (leftId->name == varName) {
                                        if (auto* intLit = dynamic_cast<IntegerLiteral*>(binary->right.get())) {
                                            isAccumPattern = true;
                                            newValue += intLit->value;
                                        }
                                    }
                                }
                                // Check x = N + x
                                if (!isAccumPattern) {
                                    if (auto* rightId = dynamic_cast<Identifier*>(binary->right.get())) {
                                        if (rightId->name == varName) {
                                            if (auto* intLit = dynamic_cast<IntegerLiteral*>(binary->left.get())) {
                                                isAccumPattern = true;
                                                newValue += intLit->value;
                                            }
                                        }
                                    }
                                }
                            }
                            // Handle x = x - N
                            else if (binary->op == TokenType::MINUS) {
                                if (auto* leftId = dynamic_cast<Identifier*>(binary->left.get())) {
                                    if (leftId->name == varName) {
                                        if (auto* intLit = dynamic_cast<IntegerLiteral*>(binary->right.get())) {
                                            isAccumPattern = true;
                                            newValue -= intLit->value;
                                        }
                                    }
                                }
                            }
                            // Handle x = x * N
                            else if (binary->op == TokenType::STAR) {
                                if (auto* leftId = dynamic_cast<Identifier*>(binary->left.get())) {
                                    if (leftId->name == varName) {
                                        if (auto* intLit = dynamic_cast<IntegerLiteral*>(binary->right.get())) {
                                            isAccumPattern = true;
                                            newValue *= intLit->value;
                                        }
                                    }
                                }
                                // Check x = N * x
                                if (!isAccumPattern) {
                                    if (auto* rightId = dynamic_cast<Identifier*>(binary->right.get())) {
                                        if (rightId->name == varName) {
                                            if (auto* intLit = dynamic_cast<IntegerLiteral*>(binary->left.get())) {
                                                isAccumPattern = true;
                                                newValue *= intLit->value;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    
                    if (isAccumPattern) {
                        accumulators[varName] = newValue;
                        if (firstAssignIdx.find(varName) == firstAssignIdx.end()) {
                            firstAssignIdx[varName] = i;
                        }
                        lastAssignIdx[varName] = i;
                    } else {
                        // Non-accumulator assignment - can't optimize
                        canOptimize[varName] = false;
                    }
                } else {
                    // Variable is being assigned but not tracked - check if it reads tracked vars
                    collectReads(assignStmt->value.get(), varName, i);
                }
            } else {
                // Non-identifier target - check for reads
                collectReads(assignStmt->value.get(), "", i);
            }
        }
        // Check if variable is used in other contexts (function calls, etc.)
        else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
            collectReads(exprStmt->expr.get(), "", i);
        }
        else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
            if (returnStmt->value) {
                collectReads(returnStmt->value.get(), "", i);
            }
        }
    }
    
    // Second pass: replace accumulator patterns with final values
    // Only optimize if the variable is NOT read BETWEEN declaration and last assignment
    for (auto& [varName, finalValue] : accumulators) {
        if (!canOptimize[varName]) continue;
        if (firstAssignIdx.find(varName) == firstAssignIdx.end()) continue;
        
        size_t declI = declIdx.count(varName) ? declIdx[varName] : 0;
        size_t lastI = lastAssignIdx[varName];
        
        // Check if variable is read between declaration and last assignment
        bool hasIntermediateRead = false;
        if (readAtIdx.count(varName)) {
            for (size_t readIdx : readAtIdx[varName]) {
                if (readIdx > declI && readIdx <= lastI) {
                    hasIntermediateRead = true;
                    break;
                }
            }
        }
        
        if (hasIntermediateRead) continue;  // Can't optimize - variable read during accumulation
        
        size_t first = firstAssignIdx[varName];
        size_t last = lastAssignIdx[varName];
        
        // Update the declaration with the final value
        if (declIdx.count(varName)) {
            size_t dIdx = declIdx[varName];
            if (auto* varDecl = dynamic_cast<VarDecl*>(statements[dIdx].get())) {
                SourceLocation loc = varDecl->initializer ? varDecl->initializer->location : varDecl->location;
                varDecl->initializer = std::make_unique<IntegerLiteral>(finalValue, loc);
                transformations_++;
            }
        }
        
        // Mark all assignments for removal
        for (size_t i = first; i <= last; ++i) {
            if (auto* assignStmt = dynamic_cast<AssignStmt*>(statements[i].get())) {
                if (auto* target = dynamic_cast<Identifier*>(assignStmt->target.get())) {
                    if (target->name == varName) {
                        statements[i] = nullptr;
                        transformations_++;
                    }
                }
            }
        }
    }
    
    // Remove nullptr statements
    statements.erase(
        std::remove_if(statements.begin(), statements.end(),
            [](const StmtPtr& s) { return s == nullptr; }),
        statements.end());
}

void ConstantPropagationPass::processBlock(std::vector<StmtPtr>& statements) {
    // First pass: collect all variable assignments
    for (auto& stmt : statements) {
        processStatement(stmt);
    }
    
    // Remove statements marked for deletion (nullptr)
    statements.erase(
        std::remove_if(statements.begin(), statements.end(),
            [](const StmtPtr& s) { return s == nullptr; }),
        statements.end());
}

bool ConstantPropagationPass::processStatement(StmtPtr& stmt) {
    if (!stmt) return false;
    
    // Handle variable declarations - track the value
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt.get())) {
        if (varDecl->initializer && !varDecl->isMutable) {
            // Try to propagate into the initializer first
            auto propagated = propagateExpression(varDecl->initializer);
            if (propagated) {
                varDecl->initializer = std::move(propagated);
            }
            
            // Track the value
            auto val = tryGetValue(varDecl->initializer.get());
            if (val) {
                knownValues_[varDecl->name] = *val;
            }
        } else if (varDecl->isMutable) {
            // Mutable variables can't be reliably tracked
            modifiedVars_.insert(varDecl->name);
        }
        return false;
    }
    
    // Handle assignments - update or invalidate tracking
    if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt.get())) {
        // Propagate into the value expression
        auto propagated = propagateExpression(assignStmt->value);
        if (propagated) {
            assignStmt->value = std::move(propagated);
        }
        
        if (auto* id = dynamic_cast<Identifier*>(assignStmt->target.get())) {
            if (assignStmt->op == TokenType::ASSIGN) {
                // Simple assignment - track new value
                auto val = tryGetValue(assignStmt->value.get());
                if (val) {
                    knownValues_[id->name] = *val;
                } else {
                    knownValues_.erase(id->name);
                }
            } else {
                // Compound assignment - invalidate
                knownValues_.erase(id->name);
            }
            modifiedVars_.insert(id->name);
        }
        return false;
    }
    
    // Handle if statements - check if condition is always true/false
    if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
        // Propagate into condition
        auto propagated = propagateExpression(ifStmt->condition);
        if (propagated) {
            ifStmt->condition = std::move(propagated);
        }
        
        // Check if condition is now a constant
        auto condResult = evaluateCondition(ifStmt->condition.get());
        if (condResult) {
            transformations_++;
            if (*condResult) {
                // Condition is always true - replace with then branch
                stmt = std::move(ifStmt->thenBranch);
                processStatement(stmt);
            } else if (!ifStmt->elifBranches.empty()) {
                // Try first elif
                ifStmt->condition = std::move(ifStmt->elifBranches[0].first);
                ifStmt->thenBranch = std::move(ifStmt->elifBranches[0].second);
                ifStmt->elifBranches.erase(ifStmt->elifBranches.begin());
                processStatement(stmt);
            } else if (ifStmt->elseBranch) {
                // Condition is always false - replace with else branch
                stmt = std::move(ifStmt->elseBranch);
                processStatement(stmt);
            } else {
                // No else branch - remove the statement
                stmt = nullptr;
            }
            return stmt == nullptr;
        }
        
        // Process branches (save and restore known values for each branch)
        auto savedValues = knownValues_;
        processStatement(ifStmt->thenBranch);
        
        for (auto& elif : ifStmt->elifBranches) {
            auto elifPropagated = propagateExpression(elif.first);
            if (elifPropagated) {
                elif.first = std::move(elifPropagated);
            }
            knownValues_ = savedValues;
            processStatement(elif.second);
        }
        
        if (ifStmt->elseBranch) {
            knownValues_ = savedValues;
            processStatement(ifStmt->elseBranch);
        }
        
        // After if statement, we can't know which branch was taken
        // So invalidate any variables modified in any branch
        knownValues_ = savedValues;
        return false;
    }
    
    // Handle while loops - invalidate variables modified in loop
    if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt.get())) {
        // Propagate into condition
        auto propagated = propagateExpression(whileStmt->condition);
        if (propagated) {
            whileStmt->condition = std::move(propagated);
        }
        
        // Invalidate all variables that might be modified in the loop
        invalidateModifiedVars(whileStmt->body.get());
        
        // Process body
        processStatement(whileStmt->body);
        return false;
    }
    
    // Handle for loops
    if (auto* forStmt = dynamic_cast<ForStmt*>(stmt.get())) {
        // Loop variable is always modified
        knownValues_.erase(forStmt->var);
        modifiedVars_.insert(forStmt->var);
        
        // Invalidate variables modified in loop body
        invalidateModifiedVars(forStmt->body.get());
        
        processStatement(forStmt->body);
        return false;
    }
    
    // Handle blocks
    if (auto* block = dynamic_cast<Block*>(stmt.get())) {
        processBlock(block->statements);
        return false;
    }
    
    // Handle expression statements
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt.get())) {
        auto propagated = propagateExpression(exprStmt->expr);
        if (propagated) {
            exprStmt->expr = std::move(propagated);
        }
        return false;
    }
    
    // Handle return statements
    if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt.get())) {
        if (returnStmt->value) {
            auto propagated = propagateExpression(returnStmt->value);
            if (propagated) {
                returnStmt->value = std::move(propagated);
            }
        }
        return false;
    }
    
    // Handle function declarations
    if (auto* fnDecl = dynamic_cast<FnDecl*>(stmt.get())) {
        // Save and clear known values for function body
        auto savedValues = knownValues_;
        knownValues_.clear();
        processStatement(fnDecl->body);
        knownValues_ = savedValues;
        return false;
    }
    
    return false;
}

ExprPtr ConstantPropagationPass::propagateExpression(ExprPtr& expr) {
    if (!expr) return nullptr;
    
    // Handle identifiers - replace with known value
    if (auto* ident = dynamic_cast<Identifier*>(expr.get())) {
        auto it = knownValues_.find(ident->name);
        if (it != knownValues_.end() && !std::holds_alternative<std::monostate>(it->second)) {
            transformations_++;
            return createLiteral(it->second, ident->location);
        }
        return nullptr;
    }
    
    // Handle binary expressions
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr.get())) {
        auto leftProp = propagateExpression(binary->left);
        if (leftProp) binary->left = std::move(leftProp);
        
        auto rightProp = propagateExpression(binary->right);
        if (rightProp) binary->right = std::move(rightProp);
        
        // Try to evaluate the whole expression
        auto leftVal = tryGetValue(binary->left.get());
        auto rightVal = tryGetValue(binary->right.get());
        
        if (leftVal && rightVal) {
            auto result = evalBinary(binary->op, *leftVal, *rightVal);
            if (result && !std::holds_alternative<std::monostate>(*result)) {
                transformations_++;
                return createLiteral(*result, binary->location);
            }
        }
        return nullptr;
    }
    
    // Handle unary expressions
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr.get())) {
        auto operandProp = propagateExpression(unary->operand);
        if (operandProp) unary->operand = std::move(operandProp);
        return nullptr;
    }
    
    // Handle call expressions - propagate into arguments
    if (auto* call = dynamic_cast<CallExpr*>(expr.get())) {
        for (auto& arg : call->args) {
            auto argProp = propagateExpression(arg);
            if (argProp) arg = std::move(argProp);
        }
        return nullptr;
    }
    
    // Handle ternary expressions
    if (auto* ternary = dynamic_cast<TernaryExpr*>(expr.get())) {
        auto condProp = propagateExpression(ternary->condition);
        if (condProp) ternary->condition = std::move(condProp);
        
        auto thenProp = propagateExpression(ternary->thenExpr);
        if (thenProp) ternary->thenExpr = std::move(thenProp);
        
        auto elseProp = propagateExpression(ternary->elseExpr);
        if (elseProp) ternary->elseExpr = std::move(elseProp);
        
        // If condition is constant, eliminate ternary
        auto condVal = evaluateCondition(ternary->condition.get());
        if (condVal) {
            transformations_++;
            if (*condVal) {
                return std::move(ternary->thenExpr);
            } else {
                return std::move(ternary->elseExpr);
            }
        }
        return nullptr;
    }
    
    return nullptr;
}

std::optional<PropValue> ConstantPropagationPass::tryGetValue(Expression* expr) {
    if (!expr) return std::nullopt;
    
    if (auto* intLit = dynamic_cast<IntegerLiteral*>(expr)) {
        return PropValue{intLit->value};
    }
    if (auto* floatLit = dynamic_cast<FloatLiteral*>(expr)) {
        return PropValue{floatLit->value};
    }
    if (auto* boolLit = dynamic_cast<BoolLiteral*>(expr)) {
        return PropValue{boolLit->value};
    }
    if (auto* strLit = dynamic_cast<StringLiteral*>(expr)) {
        return PropValue{strLit->value};
    }
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        auto it = knownValues_.find(ident->name);
        if (it != knownValues_.end()) {
            return it->second;
        }
    }
    
    return std::nullopt;
}

std::optional<PropValue> ConstantPropagationPass::evalBinary(TokenType op, const PropValue& left, const PropValue& right) {
    // Integer operations
    if (std::holds_alternative<int64_t>(left) && std::holds_alternative<int64_t>(right)) {
        int64_t l = std::get<int64_t>(left);
        int64_t r = std::get<int64_t>(right);
        
        switch (op) {
            case TokenType::PLUS:  return PropValue{l + r};
            case TokenType::MINUS: return PropValue{l - r};
            case TokenType::STAR:  return PropValue{l * r};
            case TokenType::SLASH: 
                if (r != 0) return PropValue{l / r};
                return std::nullopt;
            case TokenType::PERCENT:
                if (r != 0) return PropValue{l % r};
                return std::nullopt;
            case TokenType::EQ:  return PropValue{l == r};
            case TokenType::NE:  return PropValue{l != r};
            case TokenType::LT:  return PropValue{l < r};
            case TokenType::GT:  return PropValue{l > r};
            case TokenType::LE:  return PropValue{l <= r};
            case TokenType::GE:  return PropValue{l >= r};
            default: break;
        }
    }
    
    // Boolean operations
    if (std::holds_alternative<bool>(left) && std::holds_alternative<bool>(right)) {
        bool l = std::get<bool>(left);
        bool r = std::get<bool>(right);
        
        switch (op) {
            case TokenType::AND:
            case TokenType::AMP_AMP: return PropValue{l && r};
            case TokenType::OR:
            case TokenType::PIPE_PIPE: return PropValue{l || r};
            case TokenType::EQ: return PropValue{l == r};
            case TokenType::NE: return PropValue{l != r};
            default: break;
        }
    }
    
    return std::nullopt;
}

std::optional<bool> ConstantPropagationPass::evaluateCondition(Expression* cond) {
    auto val = tryGetValue(cond);
    if (!val) return std::nullopt;
    
    if (std::holds_alternative<bool>(*val)) {
        return std::get<bool>(*val);
    }
    if (std::holds_alternative<int64_t>(*val)) {
        return std::get<int64_t>(*val) != 0;
    }
    
    return std::nullopt;
}

ExprPtr ConstantPropagationPass::createLiteral(const PropValue& value, const SourceLocation& loc) {
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

void ConstantPropagationPass::invalidateModifiedVars(Statement* stmt) {
    if (!stmt) return;
    
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            invalidateModifiedVars(s.get());
        }
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        if (auto* id = dynamic_cast<Identifier*>(assignStmt->target.get())) {
            knownValues_.erase(id->name);
            modifiedVars_.insert(id->name);
        }
    }
    else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        if (varDecl->isMutable) {
            knownValues_.erase(varDecl->name);
            modifiedVars_.insert(varDecl->name);
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        invalidateModifiedVars(ifStmt->thenBranch.get());
        for (auto& elif : ifStmt->elifBranches) {
            invalidateModifiedVars(elif.second.get());
        }
        invalidateModifiedVars(ifStmt->elseBranch.get());
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        invalidateModifiedVars(whileStmt->body.get());
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
        knownValues_.erase(forStmt->var);
        modifiedVars_.insert(forStmt->var);
        invalidateModifiedVars(forStmt->body.get());
    }
}

} // namespace tyl
