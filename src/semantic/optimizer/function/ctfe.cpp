// Tyl Compiler - Compile-Time Function Execution Implementation
// Evaluates pure functions with constant arguments at compile time
#include "ctfe.h"
#include <cmath>

namespace tyl {

void CTFEPass::run(Program& ast) {
    transformations_ = 0;
    functions_.clear();
    ctfeCandidates_.clear();
    
    // Phase 1: Collect all function declarations
    collectFunctions(ast);
    
    // Phase 2: Analyze functions for CTFE eligibility
    analyzeFunctions();
    
    // Phase 3: Transform calls to pure functions with constant args
    transformProgram(ast);
}

void CTFEPass::collectFunctions(Program& ast) {
    for (auto& stmt : ast.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            CTFEFunctionInfo info;
            info.decl = fn;
            functions_[fn->name] = info;
        }
        else if (auto* module = dynamic_cast<ModuleDecl*>(stmt.get())) {
            for (auto& modStmt : module->body) {
                if (auto* fn = dynamic_cast<FnDecl*>(modStmt.get())) {
                    CTFEFunctionInfo info;
                    info.decl = fn;
                    functions_[fn->name] = info;
                }
            }
        }
    }
}

bool CTFEPass::checkPurityExpr(Expression* expr) {
    if (!expr) return true;
    
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        if (auto* callee = dynamic_cast<Identifier*>(call->callee.get())) {
            // Built-in pure functions
            if (callee->name == "str" || callee->name == "len" ||
                callee->name == "upper" || callee->name == "contains") {
                // Check args are pure
                for (auto& arg : call->args) {
                    if (!checkPurityExpr(arg.get())) return false;
                }
                return true;
            }
            // I/O and side-effect functions
            if (callee->name == "print" || callee->name == "println" ||
                callee->name == "sleep" || callee->name == "delete" ||
                callee->name == "now" || callee->name == "now_ms" ||
                callee->name == "hostname" || callee->name == "username") {
                return false;
            }
            // Check if it's a user function - we'll verify purity later
            // For now, assume user functions are potentially pure
            auto it = functions_.find(callee->name);
            if (it != functions_.end()) {
                // Check args are pure
                for (auto& arg : call->args) {
                    if (!checkPurityExpr(arg.get())) return false;
                }
                return true;  // Will be verified in analyzeFunctions
            }
        }
        return false;  // Unknown calls are impure
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        return checkPurityExpr(binary->left.get()) && 
               checkPurityExpr(binary->right.get());
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        return checkPurityExpr(unary->operand.get());
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        return checkPurityExpr(ternary->condition.get()) &&
               checkPurityExpr(ternary->thenExpr.get()) &&
               checkPurityExpr(ternary->elseExpr.get());
    }
    
    // Literals and identifiers are pure
    return true;
}

bool CTFEPass::checkPurity(Statement* stmt) {
    if (!stmt) return true;
    
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        return checkPurityExpr(exprStmt->expr.get());
    }
    else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        return checkPurityExpr(varDecl->initializer.get());
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        return checkPurityExpr(returnStmt->value.get());
    }
    else if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            if (!checkPurity(s.get())) return false;
        }
        return true;
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        if (!checkPurityExpr(ifStmt->condition.get())) return false;
        if (!checkPurity(ifStmt->thenBranch.get())) return false;
        for (auto& elif : ifStmt->elifBranches) {
            if (!checkPurityExpr(elif.first.get())) return false;
            if (!checkPurity(elif.second.get())) return false;
        }
        return checkPurity(ifStmt->elseBranch.get());
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        return checkPurityExpr(whileStmt->condition.get()) &&
               checkPurity(whileStmt->body.get());
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
        return checkPurityExpr(forStmt->iterable.get()) &&
               checkPurity(forStmt->body.get());
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        // Assignments to local variables are okay for CTFE
        return checkPurityExpr(assignStmt->value.get());
    }
    else if (dynamic_cast<ContinueStmt*>(stmt)) {
        // Continue is pure - it's just control flow
        return true;
    }
    else if (dynamic_cast<BreakStmt*>(stmt)) {
        // Break is pure - it's just control flow
        return true;
    }
    
    return false;  // Unknown statements are impure
}

bool CTFEPass::checkRecursion(FnDecl* fn, const std::string& targetName) {
    std::function<bool(Statement*)> check = [&](Statement* stmt) -> bool {
        if (!stmt) return false;
        
        std::function<bool(Expression*)> checkExpr = [&](Expression* expr) -> bool {
            if (!expr) return false;
            
            if (auto* call = dynamic_cast<CallExpr*>(expr)) {
                if (auto* callee = dynamic_cast<Identifier*>(call->callee.get())) {
                    if (callee->name == targetName) return true;
                }
                for (auto& arg : call->args) {
                    if (checkExpr(arg.get())) return true;
                }
            }
            else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
                return checkExpr(binary->left.get()) || checkExpr(binary->right.get());
            }
            else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
                return checkExpr(unary->operand.get());
            }
            else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
                return checkExpr(ternary->condition.get()) || 
                       checkExpr(ternary->thenExpr.get()) || 
                       checkExpr(ternary->elseExpr.get());
            }
            return false;
        };
        
        if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
            return checkExpr(exprStmt->expr.get());
        }
        else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
            return checkExpr(varDecl->initializer.get());
        }
        else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
            return checkExpr(returnStmt->value.get());
        }
        else if (auto* block = dynamic_cast<Block*>(stmt)) {
            for (auto& s : block->statements) {
                if (check(s.get())) return true;
            }
        }
        else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
            if (checkExpr(ifStmt->condition.get())) return true;
            if (check(ifStmt->thenBranch.get())) return true;
            for (auto& elif : ifStmt->elifBranches) {
                if (checkExpr(elif.first.get())) return true;
                if (check(elif.second.get())) return true;
            }
            if (check(ifStmt->elseBranch.get())) return true;
        }
        return false;
    };
    
    return check(fn->body.get());
}

void CTFEPass::analyzeFunctions() {
    // First pass: check basic purity (no I/O, no global state)
    for (auto& [name, info] : functions_) {
        if (!info.decl || !info.decl->body) continue;
        if (info.decl->isExtern || info.decl->isAsync) continue;
        if (name == "main") continue;
        
        info.isPure = checkPurity(info.decl->body.get());
        info.isRecursive = checkRecursion(info.decl, name);
    }
    
    // Second pass: propagate purity - a function calling another pure function is still pure
    // Do multiple iterations until no changes
    bool changed = true;
    int iterations = 0;
    while (changed && iterations < 10) {
        changed = false;
        iterations++;
        
        for (auto& [name, info] : functions_) {
            if (!info.isPure) continue;
            if (!info.decl || !info.decl->body) continue;
            
            // Re-check purity now that we know which functions are pure
            // This handles cases like factorial calling factorial_tail
            bool stillPure = checkPurity(info.decl->body.get());
            if (!stillPure) {
                info.isPure = false;
                changed = true;
            }
        }
    }
    
    // Third pass: mark CTFE candidates
    // A function can be CTFE'd if it's pure - recursive functions ARE allowed
    // because we have recursion depth limits in the evaluator
    // Skip functions marked as 'comptime' - they are handled by the new CTFE interpreter
    for (auto& [name, info] : functions_) {
        if (info.isPure && info.decl && !info.decl->isComptime) {
            info.canCTFE = true;
            ctfeCandidates_.insert(name);
        }
    }
}

void CTFEPass::transformProgram(Program& ast) {
    processBlock(ast.statements);
}

void CTFEPass::processBlock(std::vector<StmtPtr>& statements) {
    for (auto& stmt : statements) {
        processStatement(stmt);
    }
}

void CTFEPass::processStatement(StmtPtr& stmt) {
    if (!stmt) return;
    
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt.get())) {
        auto newExpr = processExpression(exprStmt->expr);
        if (newExpr) {
            exprStmt->expr = std::move(newExpr);
        }
    }
    else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt.get())) {
        if (varDecl->initializer) {
            auto newExpr = processExpression(varDecl->initializer);
            if (newExpr) {
                varDecl->initializer = std::move(newExpr);
            }
        }
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt.get())) {
        auto newExpr = processExpression(assignStmt->value);
        if (newExpr) {
            assignStmt->value = std::move(newExpr);
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
        auto newCond = processExpression(ifStmt->condition);
        if (newCond) ifStmt->condition = std::move(newCond);
        processStatement(ifStmt->thenBranch);
        for (auto& elif : ifStmt->elifBranches) {
            auto newElifCond = processExpression(elif.first);
            if (newElifCond) elif.first = std::move(newElifCond);
            processStatement(elif.second);
        }
        processStatement(ifStmt->elseBranch);
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt.get())) {
        auto newCond = processExpression(whileStmt->condition);
        if (newCond) whileStmt->condition = std::move(newCond);
        processStatement(whileStmt->body);
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt.get())) {
        auto newIter = processExpression(forStmt->iterable);
        if (newIter) forStmt->iterable = std::move(newIter);
        processStatement(forStmt->body);
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt.get())) {
        if (returnStmt->value) {
            auto newExpr = processExpression(returnStmt->value);
            if (newExpr) returnStmt->value = std::move(newExpr);
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
}

ExprPtr CTFEPass::processExpression(ExprPtr& expr) {
    if (!expr) return nullptr;
    
    // Check if this is a call to a CTFE-able function
    if (auto* call = dynamic_cast<CallExpr*>(expr.get())) {
        if (auto* callee = dynamic_cast<Identifier*>(call->callee.get())) {
            if (ctfeCandidates_.count(callee->name)) {
                // Try to evaluate at compile time
                auto result = evaluateCall(call);
                if (result) {
                    transformations_++;
                    return createLiteral(*result, call->location);
                }
            }
        }
        // Process arguments even if we can't CTFE the call
        for (auto& arg : call->args) {
            auto newArg = processExpression(arg);
            if (newArg) arg = std::move(newArg);
        }
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr.get())) {
        auto newLeft = processExpression(binary->left);
        if (newLeft) binary->left = std::move(newLeft);
        auto newRight = processExpression(binary->right);
        if (newRight) binary->right = std::move(newRight);
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr.get())) {
        auto newOp = processExpression(unary->operand);
        if (newOp) unary->operand = std::move(newOp);
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr.get())) {
        auto newCond = processExpression(ternary->condition);
        if (newCond) ternary->condition = std::move(newCond);
        auto newThen = processExpression(ternary->thenExpr);
        if (newThen) ternary->thenExpr = std::move(newThen);
        auto newElse = processExpression(ternary->elseExpr);
        if (newElse) ternary->elseExpr = std::move(newElse);
    }
    
    return nullptr;
}

std::optional<CTFEValue> CTFEPass::evaluateCall(CallExpr* call) {
    if (!call) return std::nullopt;
    
    auto* callee = dynamic_cast<Identifier*>(call->callee.get());
    if (!callee) return std::nullopt;
    
    auto it = functions_.find(callee->name);
    if (it == functions_.end() || !it->second.canCTFE) return std::nullopt;
    
    // Evaluate all arguments - they must all be constants
    std::vector<CTFEValue> args;
    std::map<std::string, CTFEValue> emptyEnv;
    
    for (auto& arg : call->args) {
        auto val = evaluateExpression(arg.get(), emptyEnv, 0);
        if (!val) return std::nullopt;  // Non-constant argument
        args.push_back(*val);
    }
    
    // Execute the function
    currentIterations_ = 0;
    loopControl_ = LoopControl::None;
    return evaluateFunction(it->second.decl, args, 0);
}

std::optional<CTFEValue> CTFEPass::evaluateFunction(FnDecl* fn, 
    const std::vector<CTFEValue>& args, size_t depth) {
    
    if (depth > maxRecursionDepth_) return std::nullopt;
    if (!fn || !fn->body) return std::nullopt;
    
    // Build environment with parameters
    std::map<std::string, CTFEValue> env;
    for (size_t i = 0; i < fn->params.size() && i < args.size(); ++i) {
        env[fn->params[i].first] = args[i];
    }
    
    return evaluateStatement(fn->body.get(), env, depth);
}

std::optional<CTFEValue> CTFEPass::evaluateStatement(Statement* stmt,
    std::map<std::string, CTFEValue>& env, size_t depth) {
    
    if (!stmt) return std::nullopt;
    if (++currentIterations_ > maxIterations_) return std::nullopt;
    
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            // Evaluate the statement first (including assignments)
            auto result = evaluateStatement(s.get(), env, depth);
            
            // Check for loop control (continue/break)
            if (loopControl_ != LoopControl::None) {
                return std::nullopt;  // Propagate loop control up
            }
            
            // If statement returned a value (from return statement), propagate it
            if (result) {
                return result;
            }
        }
        return std::nullopt;
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        if (returnStmt->value) {
            return evaluateExpression(returnStmt->value.get(), env, depth);
        }
        return CTFEValue{int64_t(0)};  // void return
    }
    else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        if (varDecl->initializer) {
            auto val = evaluateExpression(varDecl->initializer.get(), env, depth);
            if (val) {
                env[varDecl->name] = *val;
            }
        }
        return std::nullopt;  // Variable declaration doesn't return a value
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        if (auto* target = dynamic_cast<Identifier*>(assignStmt->target.get())) {
            auto val = evaluateExpression(assignStmt->value.get(), env, depth);
            if (val) {
                env[target->name] = *val;
            }
        }
        return std::nullopt;
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        // Handle expression statements - especially AssignExpr
        if (auto* assignExpr = dynamic_cast<AssignExpr*>(exprStmt->expr.get())) {
            if (auto* target = dynamic_cast<Identifier*>(assignExpr->target.get())) {
                auto val = evaluateExpression(assignExpr->value.get(), env, depth);
                if (val) {
                    // Handle compound assignments
                    if (assignExpr->op == TokenType::ASSIGN) {
                        env[target->name] = *val;
                    } else if (assignExpr->op == TokenType::PLUS_ASSIGN) {
                        auto current = env.find(target->name);
                        if (current != env.end() && std::holds_alternative<int64_t>(current->second) && std::holds_alternative<int64_t>(*val)) {
                            env[target->name] = CTFEValue{std::get<int64_t>(current->second) + std::get<int64_t>(*val)};
                        }
                    } else if (assignExpr->op == TokenType::MINUS_ASSIGN) {
                        auto current = env.find(target->name);
                        if (current != env.end() && std::holds_alternative<int64_t>(current->second) && std::holds_alternative<int64_t>(*val)) {
                            env[target->name] = CTFEValue{std::get<int64_t>(current->second) - std::get<int64_t>(*val)};
                        }
                    } else if (assignExpr->op == TokenType::STAR_ASSIGN) {
                        auto current = env.find(target->name);
                        if (current != env.end() && std::holds_alternative<int64_t>(current->second) && std::holds_alternative<int64_t>(*val)) {
                            env[target->name] = CTFEValue{std::get<int64_t>(current->second) * std::get<int64_t>(*val)};
                        }
                    }
                }
            }
        }
        // Other expression statements are evaluated but don't affect control flow
        return std::nullopt;
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        auto cond = evaluateExpression(ifStmt->condition.get(), env, depth);
        if (!cond) return std::nullopt;
        
        bool condTrue = false;
        if (std::holds_alternative<bool>(*cond)) {
            condTrue = std::get<bool>(*cond);
        } else if (std::holds_alternative<int64_t>(*cond)) {
            condTrue = std::get<int64_t>(*cond) != 0;
        }
        
        if (condTrue) {
            return evaluateStatement(ifStmt->thenBranch.get(), env, depth);
        }
        
        // Check elif branches
        for (auto& elif : ifStmt->elifBranches) {
            auto elifCond = evaluateExpression(elif.first.get(), env, depth);
            if (!elifCond) return std::nullopt;
            
            bool elifTrue = false;
            if (std::holds_alternative<bool>(*elifCond)) {
                elifTrue = std::get<bool>(*elifCond);
            } else if (std::holds_alternative<int64_t>(*elifCond)) {
                elifTrue = std::get<int64_t>(*elifCond) != 0;
            }
            
            if (elifTrue) {
                return evaluateStatement(elif.second.get(), env, depth);
            }
        }
        
        // Else branch
        if (ifStmt->elseBranch) {
            return evaluateStatement(ifStmt->elseBranch.get(), env, depth);
        }
        
        return std::nullopt;
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        while (true) {
            if (++currentIterations_ > maxIterations_) return std::nullopt;
            
            auto cond = evaluateExpression(whileStmt->condition.get(), env, depth);
            if (!cond) return std::nullopt;
            
            bool condTrue = false;
            if (std::holds_alternative<bool>(*cond)) {
                condTrue = std::get<bool>(*cond);
            } else if (std::holds_alternative<int64_t>(*cond)) {
                condTrue = std::get<int64_t>(*cond) != 0;
            }
            
            if (!condTrue) break;
            
            loopControl_ = LoopControl::None;
            auto result = evaluateStatement(whileStmt->body.get(), env, depth);
            
            // Check for loop control
            if (loopControl_ == LoopControl::Break) {
                loopControl_ = LoopControl::None;
                break;
            }
            if (loopControl_ == LoopControl::Continue) {
                loopControl_ = LoopControl::None;
                continue;  // Restart the loop
            }
            
            // If we got a return value, propagate it
            if (result) {
                return result;
            }
        }
        return std::nullopt;
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
        // Evaluate the iterable to get the range
        auto* rangeExpr = dynamic_cast<RangeExpr*>(forStmt->iterable.get());
        auto* callExpr = dynamic_cast<CallExpr*>(forStmt->iterable.get());
        
        int64_t start = 0, end = 0, step = 1;
        bool isInclusive = false;
        
        if (rangeExpr) {
            // Inclusive range (1..5)
            auto startVal = evaluateExpression(rangeExpr->start.get(), env, depth);
            auto endVal = evaluateExpression(rangeExpr->end.get(), env, depth);
            if (!startVal || !endVal) return std::nullopt;
            if (!std::holds_alternative<int64_t>(*startVal) || !std::holds_alternative<int64_t>(*endVal)) return std::nullopt;
            start = std::get<int64_t>(*startVal);
            end = std::get<int64_t>(*endVal);
            if (rangeExpr->step) {
                auto stepVal = evaluateExpression(rangeExpr->step.get(), env, depth);
                if (stepVal && std::holds_alternative<int64_t>(*stepVal)) {
                    step = std::get<int64_t>(*stepVal);
                }
            }
            isInclusive = true;
        } else if (callExpr) {
            // range() function call
            if (auto* callee = dynamic_cast<Identifier*>(callExpr->callee.get())) {
                if (callee->name == "range") {
                    if (callExpr->args.size() == 1) {
                        auto endVal = evaluateExpression(callExpr->args[0].get(), env, depth);
                        if (!endVal || !std::holds_alternative<int64_t>(*endVal)) return std::nullopt;
                        end = std::get<int64_t>(*endVal);
                    } else if (callExpr->args.size() >= 2) {
                        auto startVal = evaluateExpression(callExpr->args[0].get(), env, depth);
                        auto endVal = evaluateExpression(callExpr->args[1].get(), env, depth);
                        if (!startVal || !endVal) return std::nullopt;
                        if (!std::holds_alternative<int64_t>(*startVal) || !std::holds_alternative<int64_t>(*endVal)) return std::nullopt;
                        start = std::get<int64_t>(*startVal);
                        end = std::get<int64_t>(*endVal);
                        if (callExpr->args.size() >= 3) {
                            auto stepVal = evaluateExpression(callExpr->args[2].get(), env, depth);
                            if (stepVal && std::holds_alternative<int64_t>(*stepVal)) {
                                step = std::get<int64_t>(*stepVal);
                            }
                        }
                    }
                    isInclusive = false;
                } else {
                    return std::nullopt;  // Unknown iterable
                }
            } else {
                return std::nullopt;
            }
        } else {
            return std::nullopt;  // Unknown iterable type
        }
        
        // Execute the loop
        for (int64_t i = start; isInclusive ? (i <= end) : (i < end); i += step) {
            if (++currentIterations_ > maxIterations_) return std::nullopt;
            
            // Set the loop variable
            env[forStmt->var] = CTFEValue{i};
            
            loopControl_ = LoopControl::None;
            auto result = evaluateStatement(forStmt->body.get(), env, depth);
            
            // Check for loop control
            if (loopControl_ == LoopControl::Break) {
                loopControl_ = LoopControl::None;
                break;
            }
            if (loopControl_ == LoopControl::Continue) {
                loopControl_ = LoopControl::None;
                continue;
            }
            
            // If we got a return value, propagate it
            if (result) {
                return result;
            }
        }
        return std::nullopt;
    }
    else if (dynamic_cast<ContinueStmt*>(stmt)) {
        loopControl_ = LoopControl::Continue;
        return std::nullopt;
    }
    else if (dynamic_cast<BreakStmt*>(stmt)) {
        loopControl_ = LoopControl::Break;
        return std::nullopt;
    }
    
    return std::nullopt;
}

std::optional<CTFEValue> CTFEPass::evaluateExpression(Expression* expr,
    const std::map<std::string, CTFEValue>& env, size_t depth) {
    
    if (!expr) return std::nullopt;
    
    if (auto* intLit = dynamic_cast<IntegerLiteral*>(expr)) {
        return CTFEValue{intLit->value};
    }
    else if (auto* floatLit = dynamic_cast<FloatLiteral*>(expr)) {
        return CTFEValue{floatLit->value};
    }
    else if (auto* boolLit = dynamic_cast<BoolLiteral*>(expr)) {
        return CTFEValue{boolLit->value};
    }
    else if (auto* strLit = dynamic_cast<StringLiteral*>(expr)) {
        return CTFEValue{strLit->value};
    }
    else if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        auto it = env.find(ident->name);
        if (it != env.end()) {
            return it->second;
        }
        return std::nullopt;  // Unknown variable
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        auto left = evaluateExpression(binary->left.get(), env, depth);
        auto right = evaluateExpression(binary->right.get(), env, depth);
        if (!left || !right) return std::nullopt;
        
        // Integer operations
        if (std::holds_alternative<int64_t>(*left) && std::holds_alternative<int64_t>(*right)) {
            int64_t l = std::get<int64_t>(*left);
            int64_t r = std::get<int64_t>(*right);
            
            switch (binary->op) {
                case TokenType::PLUS:  return CTFEValue{l + r};
                case TokenType::MINUS: return CTFEValue{l - r};
                case TokenType::STAR:  return CTFEValue{l * r};
                case TokenType::SLASH: 
                    if (r != 0) return CTFEValue{l / r};
                    return std::nullopt;
                case TokenType::PERCENT:
                    if (r != 0) return CTFEValue{l % r};
                    return std::nullopt;
                case TokenType::EQ:  return CTFEValue{l == r};
                case TokenType::NE:  return CTFEValue{l != r};
                case TokenType::LT:  return CTFEValue{l < r};
                case TokenType::GT:  return CTFEValue{l > r};
                case TokenType::LE:  return CTFEValue{l <= r};
                case TokenType::GE:  return CTFEValue{l >= r};
                case TokenType::AMP: return CTFEValue{l & r};
                case TokenType::PIPE: return CTFEValue{l | r};
                case TokenType::CARET: return CTFEValue{l ^ r};
                default: break;
            }
        }
        
        // Float operations
        if (std::holds_alternative<double>(*left) && std::holds_alternative<double>(*right)) {
            double l = std::get<double>(*left);
            double r = std::get<double>(*right);
            
            switch (binary->op) {
                case TokenType::PLUS:  return CTFEValue{l + r};
                case TokenType::MINUS: return CTFEValue{l - r};
                case TokenType::STAR:  return CTFEValue{l * r};
                case TokenType::SLASH: 
                    if (r != 0.0) return CTFEValue{l / r};
                    return std::nullopt;
                case TokenType::EQ:  return CTFEValue{l == r};
                case TokenType::NE:  return CTFEValue{l != r};
                case TokenType::LT:  return CTFEValue{l < r};
                case TokenType::GT:  return CTFEValue{l > r};
                case TokenType::LE:  return CTFEValue{l <= r};
                case TokenType::GE:  return CTFEValue{l >= r};
                default: break;
            }
        }
        
        // Mixed int/float operations - promote int to float
        if ((std::holds_alternative<int64_t>(*left) && std::holds_alternative<double>(*right)) ||
            (std::holds_alternative<double>(*left) && std::holds_alternative<int64_t>(*right))) {
            double l = std::holds_alternative<double>(*left) ? 
                       std::get<double>(*left) : static_cast<double>(std::get<int64_t>(*left));
            double r = std::holds_alternative<double>(*right) ? 
                       std::get<double>(*right) : static_cast<double>(std::get<int64_t>(*right));
            
            switch (binary->op) {
                case TokenType::PLUS:  return CTFEValue{l + r};
                case TokenType::MINUS: return CTFEValue{l - r};
                case TokenType::STAR:  return CTFEValue{l * r};
                case TokenType::SLASH: 
                    if (r != 0.0) return CTFEValue{l / r};
                    return std::nullopt;
                case TokenType::EQ:  return CTFEValue{l == r};
                case TokenType::NE:  return CTFEValue{l != r};
                case TokenType::LT:  return CTFEValue{l < r};
                case TokenType::GT:  return CTFEValue{l > r};
                case TokenType::LE:  return CTFEValue{l <= r};
                case TokenType::GE:  return CTFEValue{l >= r};
                default: break;
            }
        }
        
        // Boolean operations
        if (std::holds_alternative<bool>(*left) && std::holds_alternative<bool>(*right)) {
            bool l = std::get<bool>(*left);
            bool r = std::get<bool>(*right);
            
            switch (binary->op) {
                case TokenType::AND:
                case TokenType::AMP_AMP: return CTFEValue{l && r};
                case TokenType::OR:
                case TokenType::PIPE_PIPE: return CTFEValue{l || r};
                case TokenType::EQ: return CTFEValue{l == r};
                case TokenType::NE: return CTFEValue{l != r};
                default: break;
            }
        }
        
        // String concatenation
        if (std::holds_alternative<std::string>(*left) && std::holds_alternative<std::string>(*right)) {
            const std::string& l = std::get<std::string>(*left);
            const std::string& r = std::get<std::string>(*right);
            
            if (binary->op == TokenType::PLUS) {
                return CTFEValue{l + r};
            }
        }
        
        return std::nullopt;
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        auto operand = evaluateExpression(unary->operand.get(), env, depth);
        if (!operand) return std::nullopt;
        
        if (std::holds_alternative<int64_t>(*operand)) {
            int64_t v = std::get<int64_t>(*operand);
            switch (unary->op) {
                case TokenType::MINUS: return CTFEValue{-v};
                case TokenType::TILDE: return CTFEValue{~v};
                default: break;
            }
        }
        else if (std::holds_alternative<double>(*operand)) {
            double v = std::get<double>(*operand);
            switch (unary->op) {
                case TokenType::MINUS: return CTFEValue{-v};
                default: break;
            }
        }
        else if (std::holds_alternative<bool>(*operand)) {
            bool v = std::get<bool>(*operand);
            switch (unary->op) {
                case TokenType::NOT:
                case TokenType::BANG: return CTFEValue{!v};
                default: break;
            }
        }
        
        return std::nullopt;
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        auto cond = evaluateExpression(ternary->condition.get(), env, depth);
        if (!cond) return std::nullopt;
        
        bool condTrue = false;
        if (std::holds_alternative<bool>(*cond)) {
            condTrue = std::get<bool>(*cond);
        } else if (std::holds_alternative<int64_t>(*cond)) {
            condTrue = std::get<int64_t>(*cond) != 0;
        }
        
        if (condTrue) {
            return evaluateExpression(ternary->thenExpr.get(), env, depth);
        } else {
            return evaluateExpression(ternary->elseExpr.get(), env, depth);
        }
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        if (auto* callee = dynamic_cast<Identifier*>(call->callee.get())) {
            // Check if it's a CTFE-able function
            auto it = functions_.find(callee->name);
            if (it != functions_.end() && it->second.canCTFE) {
                std::vector<CTFEValue> args;
                for (auto& arg : call->args) {
                    auto val = evaluateExpression(arg.get(), env, depth);
                    if (!val) return std::nullopt;
                    args.push_back(*val);
                }
                return evaluateFunction(it->second.decl, args, depth + 1);
            }
        }
        return std::nullopt;
    }
    
    return std::nullopt;
}

ExprPtr CTFEPass::createLiteral(const CTFEValue& value, const SourceLocation& loc) {
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
