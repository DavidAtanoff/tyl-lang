// Flex Compiler - Function Inlining Implementation
// Enhanced inlining with expression-level inlining for single-return functions
#include "inlining.h"

namespace flex {

std::string InliningPass::generateUniqueName(const std::string& base) {
    return "$inline_" + base + "_" + std::to_string(uniqueVarCounter_++);
}

void InliningPass::run(Program& ast) {
    transformations_ = 0;
    functions_.clear();
    inlineCandidates_.clear();
    exprInlineCandidates_.clear();
    inlineCount_.clear();
    uniqueVarCounter_ = 0;
    
    // Phase 1: Collect all function declarations
    collectFunctions(ast);
    
    // Phase 2: Analyze functions for inlining eligibility
    analyzeFunctions();
    
    // Phase 3: Perform inlining
    inlineCalls(ast);
}

void InliningPass::collectFunctions(Program& ast) {
    for (auto& stmt : ast.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            FunctionInfo info;
            info.decl = fn;
            info.statementCount = countStatements(fn->body.get());
            functions_[fn->name] = info;
        }
        else if (auto* module = dynamic_cast<ModuleDecl*>(stmt.get())) {
            for (auto& modStmt : module->body) {
                if (auto* fn = dynamic_cast<FnDecl*>(modStmt.get())) {
                    FunctionInfo info;
                    info.decl = fn;
                    info.statementCount = countStatements(fn->body.get());
                    functions_[fn->name] = info;
                }
            }
        }
    }
}

size_t InliningPass::countStatements(Statement* stmt) {
    if (!stmt) return 0;
    
    size_t count = 1;
    
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        count = 0;
        for (auto& s : block->statements) {
            count += countStatements(s.get());
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        count += countStatements(ifStmt->thenBranch.get());
        for (auto& elif : ifStmt->elifBranches) {
            count += countStatements(elif.second.get());
        }
        count += countStatements(ifStmt->elseBranch.get());
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        count += countStatements(whileStmt->body.get());
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
        count += countStatements(forStmt->body.get());
    }
    
    return count;
}

size_t InliningPass::countExpressionComplexity(Expression* expr) {
    if (!expr) return 0;
    
    size_t complexity = 1;
    
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        complexity += countExpressionComplexity(binary->left.get());
        complexity += countExpressionComplexity(binary->right.get());
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        complexity += countExpressionComplexity(unary->operand.get());
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        complexity += 3;  // Function calls are more expensive
        for (auto& arg : call->args) {
            complexity += countExpressionComplexity(arg.get());
        }
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        complexity += countExpressionComplexity(ternary->condition.get());
        complexity += countExpressionComplexity(ternary->thenExpr.get());
        complexity += countExpressionComplexity(ternary->elseExpr.get());
    }
    else if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        complexity += countExpressionComplexity(member->object.get());
    }
    else if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        complexity += countExpressionComplexity(index->object.get());
        complexity += countExpressionComplexity(index->index.get());
    }
    
    return complexity;
}

void InliningPass::countCalls(Statement* stmt) {
    if (!stmt) return;
    
    // Helper to count calls in expressions
    std::function<void(Expression*)> countInExpr = [&](Expression* expr) {
        if (!expr) return;
        
        if (auto* call = dynamic_cast<CallExpr*>(expr)) {
            if (auto* callee = dynamic_cast<Identifier*>(call->callee.get())) {
                if (functions_.count(callee->name)) {
                    functions_[callee->name].callCount++;
                }
            }
            for (auto& arg : call->args) {
                countInExpr(arg.get());
            }
        }
        else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
            countInExpr(binary->left.get());
            countInExpr(binary->right.get());
        }
        else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
            countInExpr(unary->operand.get());
        }
        else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
            countInExpr(ternary->condition.get());
            countInExpr(ternary->thenExpr.get());
            countInExpr(ternary->elseExpr.get());
        }
    };
    
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        countInExpr(exprStmt->expr.get());
    }
    else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        countInExpr(varDecl->initializer.get());
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        countInExpr(assignStmt->value.get());
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        countInExpr(returnStmt->value.get());
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        countInExpr(ifStmt->condition.get());
        countCalls(ifStmt->thenBranch.get());
        for (auto& elif : ifStmt->elifBranches) {
            countInExpr(elif.first.get());
            countCalls(elif.second.get());
        }
        countCalls(ifStmt->elseBranch.get());
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        countInExpr(whileStmt->condition.get());
        countCalls(whileStmt->body.get());
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
        countInExpr(forStmt->iterable.get());
        countCalls(forStmt->body.get());
    }
    else if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            countCalls(s.get());
        }
    }
}

bool InliningPass::checkRecursion(FnDecl* fn, const std::string& targetName) {
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

bool InliningPass::checkSideEffectsInExpr(Expression* expr) {
    if (!expr) return false;
    
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        if (auto* callee = dynamic_cast<Identifier*>(call->callee.get())) {
            // Built-in functions with side effects
            if (callee->name == "print" || callee->name == "println" ||
                callee->name == "sleep" || callee->name == "delete") {
                return true;
            }
        }
        // Check arguments for side effects
        for (auto& arg : call->args) {
            if (checkSideEffectsInExpr(arg.get())) return true;
        }
        return true;  // Assume unknown calls have side effects
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        return checkSideEffectsInExpr(binary->left.get()) || 
               checkSideEffectsInExpr(binary->right.get());
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        return checkSideEffectsInExpr(unary->operand.get());
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        return checkSideEffectsInExpr(ternary->condition.get()) ||
               checkSideEffectsInExpr(ternary->thenExpr.get()) ||
               checkSideEffectsInExpr(ternary->elseExpr.get());
    }
    return false;
}

bool InliningPass::checkSideEffects(Statement* stmt) {
    if (!stmt) return false;
    
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        return checkSideEffectsInExpr(exprStmt->expr.get());
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        return true;  // Assignments have side effects
    }
    else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        return checkSideEffectsInExpr(varDecl->initializer.get());
    }
    else if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            if (checkSideEffects(s.get())) return true;
        }
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        return checkSideEffectsInExpr(returnStmt->value.get());
    }
    
    return false;
}

bool InliningPass::isSingleReturnFunction(FnDecl* fn) {
    if (!fn || !fn->body) return false;
    
    auto* block = dynamic_cast<Block*>(fn->body.get());
    if (!block) {
        // Single statement body
        return dynamic_cast<ReturnStmt*>(fn->body.get()) != nullptr;
    }
    
    // Check if block has exactly one return statement
    if (block->statements.size() == 1) {
        return dynamic_cast<ReturnStmt*>(block->statements[0].get()) != nullptr;
    }
    
    return false;
}

Expression* InliningPass::getSingleReturnExpr(FnDecl* fn) {
    if (!fn || !fn->body) return nullptr;
    
    auto* block = dynamic_cast<Block*>(fn->body.get());
    if (!block) {
        if (auto* ret = dynamic_cast<ReturnStmt*>(fn->body.get())) {
            return ret->value.get();
        }
        return nullptr;
    }
    
    if (block->statements.size() == 1) {
        if (auto* ret = dynamic_cast<ReturnStmt*>(block->statements[0].get())) {
            return ret->value.get();
        }
    }
    
    return nullptr;
}

void InliningPass::analyzeFunctions() {
    // Count calls to each function
    for (auto& [name, info] : functions_) {
        if (info.decl && info.decl->body) {
            countCalls(info.decl->body.get());
        }
    }
    
    // Determine which functions are eligible for inlining
    for (auto& [name, info] : functions_) {
        if (!info.decl) continue;
        
        // Skip extern functions
        if (info.decl->isExtern) continue;
        
        // Skip async functions
        if (info.decl->isAsync) continue;
        
        // Skip main function
        if (name == "main") continue;
        
        // Check recursion
        info.hasRecursion = checkRecursion(info.decl, name);
        if (info.hasRecursion) continue;
        
        // Check size
        if (info.statementCount > maxInlineStatements_) continue;
        
        // Check side effects
        info.hasSideEffects = checkSideEffects(info.decl->body.get());
        
        // Check if single return function (can be inlined as expression)
        info.isSingleReturn = isSingleReturnFunction(info.decl);
        
        if (info.isSingleReturn) {
            Expression* retExpr = getSingleReturnExpr(info.decl);
            if (retExpr) {
                info.expressionComplexity = countExpressionComplexity(retExpr);
                info.isPureFunction = !info.hasSideEffects && 
                                      !checkSideEffectsInExpr(retExpr);
            }
        }
        
        // Mark as simple/inlinable
        info.isSimple = true;
        inlineCandidates_.insert(name);
        
        // Single-return pure functions can be inlined as expressions
        if (info.isSingleReturn && info.isPureFunction && 
            info.expressionComplexity <= maxExpressionComplexity_) {
            exprInlineCandidates_.insert(name);
        }
    }
}

void InliningPass::inlineCalls(Program& ast) {
    processBlock(ast.statements);
}

void InliningPass::processBlock(std::vector<StmtPtr>& statements) {
    for (auto& stmt : statements) {
        processStatement(stmt);
    }
}

void InliningPass::processStatement(StmtPtr& stmt) {
    if (!stmt) return;
    
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt.get())) {
        // Check if this is a call to an inlinable function
        if (auto* call = dynamic_cast<CallExpr*>(exprStmt->expr.get())) {
            if (auto* callee = dynamic_cast<Identifier*>(call->callee.get())) {
                if (inlineCandidates_.count(callee->name) && 
                    inlineCount_[callee->name] < maxInlineCallCount_) {
                    auto inlined = inlineCall(call, functions_[callee->name].decl);
                    if (inlined) {
                        stmt = std::move(inlined);
                        inlineCount_[callee->name]++;
                        transformations_++;
                        return;
                    }
                }
            }
        }
        // Process expression for nested calls
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

ExprPtr InliningPass::processExpression(ExprPtr& expr) {
    if (!expr) return nullptr;
    
    // Check if this is a call to an expression-inlinable function
    if (auto* call = dynamic_cast<CallExpr*>(expr.get())) {
        if (auto* callee = dynamic_cast<Identifier*>(call->callee.get())) {
            if (exprInlineCandidates_.count(callee->name) && 
                inlineCount_[callee->name] < maxInlineCallCount_) {
                auto inlined = inlineCallAsExpr(call, functions_[callee->name].decl);
                if (inlined) {
                    inlineCount_[callee->name]++;
                    transformations_++;
                    return inlined;
                }
            }
        }
        // Process arguments
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

ExprPtr InliningPass::inlineCallAsExpr(CallExpr* call, FnDecl* fn) {
    if (!call || !fn) return nullptr;
    
    Expression* retExpr = getSingleReturnExpr(fn);
    if (!retExpr) return nullptr;
    
    // Build argument map: parameter name -> argument expression
    std::map<std::string, Expression*> argMap;
    for (size_t i = 0; i < fn->params.size() && i < call->args.size(); ++i) {
        argMap[fn->params[i].first] = call->args[i].get();
    }
    
    // Clone the return expression with argument substitution
    return cloneExpression(retExpr, argMap);
}

StmtPtr InliningPass::inlineCall(CallExpr* call, FnDecl* fn) {
    if (!call || !fn || !fn->body) return nullptr;
    
    // Build argument map: parameter name -> argument expression
    std::map<std::string, Expression*> argMap;
    for (size_t i = 0; i < fn->params.size() && i < call->args.size(); ++i) {
        argMap[fn->params[i].first] = call->args[i].get();
    }
    
    // Clone the function body with argument substitution
    return cloneStatement(fn->body.get(), argMap);
}

ExprPtr InliningPass::cloneExpression(Expression* expr, const std::map<std::string, Expression*>& argMap) {
    if (!expr) return nullptr;
    
    if (auto* intLit = dynamic_cast<IntegerLiteral*>(expr)) {
        return std::make_unique<IntegerLiteral>(intLit->value, intLit->location);
    }
    else if (auto* floatLit = dynamic_cast<FloatLiteral*>(expr)) {
        return std::make_unique<FloatLiteral>(floatLit->value, floatLit->location);
    }
    else if (auto* strLit = dynamic_cast<StringLiteral*>(expr)) {
        return std::make_unique<StringLiteral>(strLit->value, strLit->location);
    }
    else if (auto* interp = dynamic_cast<InterpolatedString*>(expr)) {
        auto newInterp = std::make_unique<InterpolatedString>(interp->location);
        for (auto& part : interp->parts) {
            if (auto* str = std::get_if<std::string>(&part)) {
                newInterp->parts.push_back(*str);
            } else if (auto* exprPtr = std::get_if<ExprPtr>(&part)) {
                auto cloned = cloneExpression(exprPtr->get(), argMap);
                if (cloned) {
                    newInterp->parts.push_back(std::move(cloned));
                }
            }
        }
        return newInterp;
    }
    else if (auto* boolLit = dynamic_cast<BoolLiteral*>(expr)) {
        return std::make_unique<BoolLiteral>(boolLit->value, boolLit->location);
    }
    else if (auto* nilLit = dynamic_cast<NilLiteral*>(expr)) {
        return std::make_unique<NilLiteral>(nilLit->location);
    }
    else if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        // Check if this identifier is a parameter - substitute with argument
        auto it = argMap.find(ident->name);
        if (it != argMap.end()) {
            return cloneExpression(it->second, {});  // Clone the argument
        }
        return std::make_unique<Identifier>(ident->name, ident->location);
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        return std::make_unique<BinaryExpr>(
            cloneExpression(binary->left.get(), argMap),
            binary->op,
            cloneExpression(binary->right.get(), argMap),
            binary->location
        );
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        return std::make_unique<UnaryExpr>(
            unary->op,
            cloneExpression(unary->operand.get(), argMap),
            unary->location
        );
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        auto newCall = std::make_unique<CallExpr>(
            cloneExpression(call->callee.get(), argMap),
            call->location
        );
        for (auto& arg : call->args) {
            newCall->args.push_back(cloneExpression(arg.get(), argMap));
        }
        for (auto& namedArg : call->namedArgs) {
            newCall->namedArgs.push_back({namedArg.first, cloneExpression(namedArg.second.get(), argMap)});
        }
        newCall->isHotCallSite = call->isHotCallSite;
        return newCall;
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        return std::make_unique<TernaryExpr>(
            cloneExpression(ternary->condition.get(), argMap),
            cloneExpression(ternary->thenExpr.get(), argMap),
            cloneExpression(ternary->elseExpr.get(), argMap),
            ternary->location
        );
    }
    else if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        return std::make_unique<MemberExpr>(
            cloneExpression(member->object.get(), argMap),
            member->member,
            member->location
        );
    }
    else if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        return std::make_unique<IndexExpr>(
            cloneExpression(index->object.get(), argMap),
            cloneExpression(index->index.get(), argMap),
            index->location
        );
    }
    else if (auto* list = dynamic_cast<ListExpr*>(expr)) {
        auto newList = std::make_unique<ListExpr>(list->location);
        for (auto& elem : list->elements) {
            newList->elements.push_back(cloneExpression(elem.get(), argMap));
        }
        return newList;
    }
    else if (auto* record = dynamic_cast<RecordExpr*>(expr)) {
        auto newRecord = std::make_unique<RecordExpr>(record->location);
        for (auto& field : record->fields) {
            newRecord->fields.push_back({field.first, cloneExpression(field.second.get(), argMap)});
        }
        return newRecord;
    }
    else if (auto* range = dynamic_cast<RangeExpr*>(expr)) {
        return std::make_unique<RangeExpr>(
            cloneExpression(range->start.get(), argMap),
            cloneExpression(range->end.get(), argMap),
            range->step ? cloneExpression(range->step.get(), argMap) : nullptr,
            range->location
        );
    }
    else if (auto* lambda = dynamic_cast<LambdaExpr*>(expr)) {
        auto newLambda = std::make_unique<LambdaExpr>(lambda->location);
        newLambda->params = lambda->params;  // Copy params (name, type pairs)
        newLambda->body = cloneExpression(lambda->body.get(), argMap);
        return newLambda;
    }
    else if (auto* listComp = dynamic_cast<ListCompExpr*>(expr)) {
        return std::make_unique<ListCompExpr>(
            cloneExpression(listComp->expr.get(), argMap),
            listComp->var,
            cloneExpression(listComp->iterable.get(), argMap),
            listComp->condition ? cloneExpression(listComp->condition.get(), argMap) : nullptr,
            listComp->location
        );
    }
    else if (auto* addrOf = dynamic_cast<AddressOfExpr*>(expr)) {
        return std::make_unique<AddressOfExpr>(
            cloneExpression(addrOf->operand.get(), argMap),
            addrOf->location
        );
    }
    else if (auto* deref = dynamic_cast<DerefExpr*>(expr)) {
        return std::make_unique<DerefExpr>(
            cloneExpression(deref->operand.get(), argMap),
            deref->location
        );
    }
    else if (auto* newExpr = dynamic_cast<NewExpr*>(expr)) {
        auto cloned = std::make_unique<NewExpr>(newExpr->typeName, newExpr->location);
        for (auto& arg : newExpr->args) {
            cloned->args.push_back(cloneExpression(arg.get(), argMap));
        }
        return cloned;
    }
    else if (auto* cast = dynamic_cast<CastExpr*>(expr)) {
        return std::make_unique<CastExpr>(
            cloneExpression(cast->expr.get(), argMap),
            cast->targetType,
            cast->location
        );
    }
    else if (auto* await = dynamic_cast<AwaitExpr*>(expr)) {
        return std::make_unique<AwaitExpr>(
            cloneExpression(await->operand.get(), argMap),
            await->location
        );
    }
    else if (auto* spawn = dynamic_cast<SpawnExpr*>(expr)) {
        return std::make_unique<SpawnExpr>(
            cloneExpression(spawn->operand.get(), argMap),
            spawn->location
        );
    }
    else if (auto* dsl = dynamic_cast<DSLBlock*>(expr)) {
        return std::make_unique<DSLBlock>(dsl->dslName, dsl->rawContent, dsl->location);
    }
    else if (auto* assign = dynamic_cast<AssignExpr*>(expr)) {
        return std::make_unique<AssignExpr>(
            cloneExpression(assign->target.get(), argMap),
            assign->op,
            cloneExpression(assign->value.get(), argMap),
            assign->location
        );
    }
    
    // For other expression types, return nullptr (not supported for inlining)
    return nullptr;
}

StmtPtr InliningPass::cloneStatement(Statement* stmt, const std::map<std::string, Expression*>& argMap) {
    if (!stmt) return nullptr;
    
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        auto newBlock = std::make_unique<Block>(block->location);
        for (auto& s : block->statements) {
            auto cloned = cloneStatement(s.get(), argMap);
            if (cloned) {
                newBlock->statements.push_back(std::move(cloned));
            }
        }
        return newBlock;
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        auto clonedExpr = cloneExpression(exprStmt->expr.get(), argMap);
        if (clonedExpr) {
            return std::make_unique<ExprStmt>(std::move(clonedExpr), exprStmt->location);
        }
    }
    else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        // Generate unique name to avoid conflicts
        std::string uniqueName = generateUniqueName(varDecl->name);
        auto newDecl = std::make_unique<VarDecl>(
            uniqueName,
            varDecl->typeName,
            cloneExpression(varDecl->initializer.get(), argMap),
            varDecl->location
        );
        newDecl->isMutable = varDecl->isMutable;
        newDecl->isConst = varDecl->isConst;
        return newDecl;
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        return std::make_unique<AssignStmt>(
            cloneExpression(assignStmt->target.get(), argMap),
            assignStmt->op,
            cloneExpression(assignStmt->value.get(), argMap),
            assignStmt->location
        );
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        // For inlined functions, return becomes the result
        // Convert to expression statement (the value is the result)
        if (returnStmt->value) {
            return std::make_unique<ExprStmt>(
                cloneExpression(returnStmt->value.get(), argMap),
                returnStmt->location
            );
        }
        return nullptr;  // void return
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        auto newIf = std::make_unique<IfStmt>(
            cloneExpression(ifStmt->condition.get(), argMap),
            cloneStatement(ifStmt->thenBranch.get(), argMap),
            ifStmt->location
        );
        for (auto& elif : ifStmt->elifBranches) {
            newIf->elifBranches.push_back({
                cloneExpression(elif.first.get(), argMap),
                cloneStatement(elif.second.get(), argMap)
            });
        }
        newIf->elseBranch = cloneStatement(ifStmt->elseBranch.get(), argMap);
        return newIf;
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        return std::make_unique<WhileStmt>(
            cloneExpression(whileStmt->condition.get(), argMap),
            cloneStatement(whileStmt->body.get(), argMap),
            whileStmt->location
        );
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
        auto newFor = std::make_unique<ForStmt>(
            forStmt->var,
            cloneExpression(forStmt->iterable.get(), argMap),
            cloneStatement(forStmt->body.get(), argMap),
            forStmt->location
        );
        newFor->unrollHint = forStmt->unrollHint;
        return newFor;
    }
    else if (auto* matchStmt = dynamic_cast<MatchStmt*>(stmt)) {
        auto newMatch = std::make_unique<MatchStmt>(
            cloneExpression(matchStmt->value.get(), argMap),
            matchStmt->location
        );
        for (auto& c : matchStmt->cases) {
            newMatch->cases.push_back({
                cloneExpression(c.first.get(), argMap),
                cloneStatement(c.second.get(), argMap)
            });
        }
        newMatch->defaultCase = cloneStatement(matchStmt->defaultCase.get(), argMap);
        return newMatch;
    }
    else if (auto* breakStmt = dynamic_cast<BreakStmt*>(stmt)) {
        return std::make_unique<BreakStmt>(breakStmt->location);
    }
    else if (auto* continueStmt = dynamic_cast<ContinueStmt*>(stmt)) {
        return std::make_unique<ContinueStmt>(continueStmt->location);
    }
    else if (auto* tryStmt = dynamic_cast<TryStmt*>(stmt)) {
        return std::make_unique<TryStmt>(
            cloneExpression(tryStmt->tryExpr.get(), argMap),
            cloneExpression(tryStmt->elseExpr.get(), argMap),
            tryStmt->location
        );
    }
    else if (auto* unsafeBlock = dynamic_cast<UnsafeBlock*>(stmt)) {
        return std::make_unique<UnsafeBlock>(
            cloneStatement(unsafeBlock->body.get(), argMap),
            unsafeBlock->location
        );
    }
    else if (auto* destructDecl = dynamic_cast<DestructuringDecl*>(stmt)) {
        auto newDecl = std::make_unique<DestructuringDecl>(
            destructDecl->kind,
            destructDecl->names,
            cloneExpression(destructDecl->initializer.get(), argMap),
            destructDecl->location
        );
        newDecl->isMutable = destructDecl->isMutable;
        return newDecl;
    }
    else if (auto* deleteStmt = dynamic_cast<DeleteStmt*>(stmt)) {
        return std::make_unique<DeleteStmt>(
            cloneExpression(deleteStmt->expr.get(), argMap),
            deleteStmt->location
        );
    }
    
    return nullptr;
}

} // namespace flex
