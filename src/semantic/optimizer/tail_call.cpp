// Tyl Compiler - Tail Call Optimization Implementation
// Transforms tail-recursive functions into iterative loops
#include "tail_call.h"

namespace tyl {

std::string TailCallOptimizationPass::newLabel(const std::string& prefix) {
    return "$tco_" + prefix + "_" + std::to_string(labelCounter_++);
}

void TailCallOptimizationPass::run(Program& ast) {
    transformations_ = 0;
    functions_.clear();
    labelCounter_ = 0;
    
    // Phase 1: Collect all function declarations
    collectFunctions(ast);
    
    // Phase 2: Analyze for tail calls
    analyzeTailCalls();
    
    // Phase 3: Optimize tail-recursive functions
    optimizeTailCalls(ast);
}

void TailCallOptimizationPass::collectFunctions(Program& ast) {
    for (auto& stmt : ast.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            TailCallInfo info;
            info.decl = fn;
            functions_[fn->name] = info;
        }
        else if (auto* module = dynamic_cast<ModuleDecl*>(stmt.get())) {
            for (auto& modStmt : module->body) {
                if (auto* fn = dynamic_cast<FnDecl*>(modStmt.get())) {
                    TailCallInfo info;
                    info.decl = fn;
                    functions_[fn->name] = info;
                }
            }
        }
    }
}

// Helper to check if an expression contains a recursive call
static bool containsRecursiveCall(Expression* expr, const std::string& fnName) {
    if (!expr) return false;
    
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        if (auto* callee = dynamic_cast<Identifier*>(call->callee.get())) {
            if (callee->name == fnName) return true;
        }
        // Check arguments for nested recursive calls
        for (auto& arg : call->args) {
            if (containsRecursiveCall(arg.get(), fnName)) return true;
        }
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        return containsRecursiveCall(binary->left.get(), fnName) ||
               containsRecursiveCall(binary->right.get(), fnName);
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        return containsRecursiveCall(unary->operand.get(), fnName);
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        return containsRecursiveCall(ternary->condition.get(), fnName) ||
               containsRecursiveCall(ternary->thenExpr.get(), fnName) ||
               containsRecursiveCall(ternary->elseExpr.get(), fnName);
    }
    
    return false;
}

bool TailCallOptimizationPass::isTailCall(ReturnStmt* ret, const std::string& fnName) {
    if (!ret || !ret->value) return false;
    
    // Check if return value is a DIRECT call to the same function
    // (not nested inside another expression or as an argument)
    if (auto* call = dynamic_cast<CallExpr*>(ret->value.get())) {
        if (auto* callee = dynamic_cast<Identifier*>(call->callee.get())) {
            if (callee->name == fnName) {
                // This is a direct recursive call, but we need to verify
                // that none of the arguments contain recursive calls.
                // e.g., ackermann(m - 1, ackermann(m, n - 1)) is NOT a tail call
                // because the inner ackermann call must complete first.
                for (auto& arg : call->args) {
                    if (containsRecursiveCall(arg.get(), fnName)) {
                        return false;  // Nested recursive call - not a true tail call
                    }
                }
                return true;  // True tail call
            }
        }
    }
    
    return false;
}

void TailCallOptimizationPass::findTailCalls(Statement* stmt, const std::string& fnName,
                                              std::vector<ReturnStmt*>& tailCalls) {
    if (!stmt) return;
    
    if (auto* ret = dynamic_cast<ReturnStmt*>(stmt)) {
        if (isTailCall(ret, fnName)) {
            tailCalls.push_back(ret);
        }
    }
    else if (auto* block = dynamic_cast<Block*>(stmt)) {
        // Only the last statement in a block can be in tail position
        if (!block->statements.empty()) {
            findTailCalls(block->statements.back().get(), fnName, tailCalls);
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        // Both branches can be in tail position
        findTailCalls(ifStmt->thenBranch.get(), fnName, tailCalls);
        for (auto& elif : ifStmt->elifBranches) {
            findTailCalls(elif.second.get(), fnName, tailCalls);
        }
        findTailCalls(ifStmt->elseBranch.get(), fnName, tailCalls);
    }
    else if (auto* matchStmt = dynamic_cast<MatchStmt*>(stmt)) {
        for (auto& case_ : matchStmt->cases) {
            findTailCalls(case_.body.get(), fnName, tailCalls);
        }
        findTailCalls(matchStmt->defaultCase.get(), fnName, tailCalls);
    }
}

void TailCallOptimizationPass::analyzeTailCalls() {
    for (auto& [name, info] : functions_) {
        if (!info.decl || !info.decl->body) continue;
        
        // Skip extern, async, and comptime functions
        // Comptime functions are evaluated by the CTFE interpreter, not compiled
        if (info.decl->isExtern || info.decl->isAsync || info.decl->isComptime) continue;
        
        // Find all tail calls
        findTailCalls(info.decl->body.get(), name, info.tailCalls);
        
        info.hasTailRecursion = !info.tailCalls.empty();
        
        // Can optimize if we have tail recursion and function has parameters
        info.canOptimize = info.hasTailRecursion && !info.decl->params.empty();
    }
}

void TailCallOptimizationPass::optimizeTailCalls(Program& ast) {
    for (auto& stmt : ast.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            auto it = functions_.find(fn->name);
            if (it != functions_.end() && it->second.canOptimize) {
                auto transformed = transformToLoop(fn);
                if (transformed) {
                    fn->body = std::move(transformed);
                    transformations_++;
                }
            }
        }
        else if (auto* module = dynamic_cast<ModuleDecl*>(stmt.get())) {
            for (auto& modStmt : module->body) {
                if (auto* fn = dynamic_cast<FnDecl*>(modStmt.get())) {
                    auto it = functions_.find(fn->name);
                    if (it != functions_.end() && it->second.canOptimize) {
                        auto transformed = transformToLoop(fn);
                        if (transformed) {
                            fn->body = std::move(transformed);
                            transformations_++;
                        }
                    }
                }
            }
        }
    }
}

StmtPtr TailCallOptimizationPass::transformToLoop(FnDecl* fn) {
    if (!fn || !fn->body) return nullptr;
    
    std::string loopLabel = newLabel("loop");
    
    // Collect parameter names
    std::vector<std::string> paramNames;
    for (auto& param : fn->params) {
        paramNames.push_back(param.first);
    }
    
    // Create the loop body by transforming the original body
    auto loopBody = transformStatement(fn->body.get(), fn->name, loopLabel, paramNames);
    if (!loopBody) return nullptr;
    
    // Wrap in while(true) loop
    auto trueExpr = std::make_unique<BoolLiteral>(true, fn->location);
    auto whileLoop = std::make_unique<WhileStmt>(
        std::move(trueExpr),
        std::move(loopBody),
        fn->location
    );
    
    // Create outer block
    auto outerBlock = std::make_unique<Block>(fn->location);
    outerBlock->statements.push_back(std::move(whileLoop));
    
    return outerBlock;
}

StmtPtr TailCallOptimizationPass::transformStatement(Statement* stmt, const std::string& fnName,
                                                      const std::string& loopLabel,
                                                      const std::vector<std::string>& paramNames) {
    if (!stmt) return nullptr;
    
    if (auto* ret = dynamic_cast<ReturnStmt*>(stmt)) {
        // Check if this is a tail call
        if (auto* call = dynamic_cast<CallExpr*>(ret->value.get())) {
            if (auto* callee = dynamic_cast<Identifier*>(call->callee.get())) {
                if (callee->name == fnName) {
                    // Transform tail call into parameter reassignment + continue
                    auto block = std::make_unique<Block>(ret->location);
                    
                    // Create temp variables for new argument values
                    std::vector<std::string> tempNames;
                    for (size_t i = 0; i < call->args.size() && i < paramNames.size(); ++i) {
                        std::string tempName = "$tco_temp_" + std::to_string(i);
                        tempNames.push_back(tempName);
                        
                        // Clone the argument expression
                        auto argClone = transformExpression(call->args[i].get());
                        if (!argClone) {
                            argClone = std::make_unique<IntegerLiteral>(0, ret->location);
                        }
                        
                        auto tempDecl = std::make_unique<VarDecl>(
                            tempName, "", std::move(argClone), ret->location
                        );
                        tempDecl->isMutable = true;
                        block->statements.push_back(std::move(tempDecl));
                    }
                    
                    // Assign temp values to parameters
                    for (size_t i = 0; i < tempNames.size(); ++i) {
                        auto target = std::make_unique<Identifier>(paramNames[i], ret->location);
                        auto value = std::make_unique<Identifier>(tempNames[i], ret->location);
                        auto assign = std::make_unique<AssignStmt>(
                            std::move(target),
                            TokenType::ASSIGN,
                            std::move(value),
                            ret->location
                        );
                        block->statements.push_back(std::move(assign));
                    }
                    
                    // Add continue to restart the loop
                    block->statements.push_back(std::make_unique<ContinueStmt>(ret->location));
                    
                    return block;
                }
            }
        }
        
        // Non-tail return: wrap in break to exit the loop
        auto block = std::make_unique<Block>(ret->location);
        
        // Store return value in temp variable
        if (ret->value) {
            auto retClone = transformExpression(ret->value.get());
            auto tempDecl = std::make_unique<VarDecl>(
                "$tco_result", "", std::move(retClone), ret->location
            );
            block->statements.push_back(std::move(tempDecl));
            
            // Return the temp variable
            auto retIdent = std::make_unique<Identifier>("$tco_result", ret->location);
            auto newRet = std::make_unique<ReturnStmt>(std::move(retIdent), ret->location);
            block->statements.push_back(std::move(newRet));
        } else {
            block->statements.push_back(std::make_unique<ReturnStmt>(nullptr, ret->location));
        }
        
        return block;
    }
    else if (auto* block = dynamic_cast<Block*>(stmt)) {
        auto newBlock = std::make_unique<Block>(block->location);
        for (auto& s : block->statements) {
            auto transformed = transformStatement(s.get(), fnName, loopLabel, paramNames);
            if (transformed) {
                newBlock->statements.push_back(std::move(transformed));
            }
        }
        return newBlock;
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        auto condClone = transformExpression(ifStmt->condition.get());
        auto thenTransformed = transformStatement(ifStmt->thenBranch.get(), fnName, loopLabel, paramNames);
        
        auto newIf = std::make_unique<IfStmt>(
            std::move(condClone),
            std::move(thenTransformed),
            ifStmt->location
        );
        
        for (auto& elif : ifStmt->elifBranches) {
            auto elifCond = transformExpression(elif.first.get());
            auto elifBody = transformStatement(elif.second.get(), fnName, loopLabel, paramNames);
            newIf->elifBranches.push_back({std::move(elifCond), std::move(elifBody)});
        }
        
        if (ifStmt->elseBranch) {
            newIf->elseBranch = transformStatement(ifStmt->elseBranch.get(), fnName, loopLabel, paramNames);
        }
        
        return newIf;
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        auto condClone = transformExpression(whileStmt->condition.get());
        auto bodyTransformed = transformStatement(whileStmt->body.get(), fnName, loopLabel, paramNames);
        return std::make_unique<WhileStmt>(std::move(condClone), std::move(bodyTransformed), whileStmt->location);
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
        auto iterClone = transformExpression(forStmt->iterable.get());
        auto bodyTransformed = transformStatement(forStmt->body.get(), fnName, loopLabel, paramNames);
        return std::make_unique<ForStmt>(forStmt->var, std::move(iterClone), std::move(bodyTransformed), forStmt->location);
    }
    else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        auto initClone = varDecl->initializer ? transformExpression(varDecl->initializer.get()) : nullptr;
        auto newDecl = std::make_unique<VarDecl>(varDecl->name, varDecl->typeName, std::move(initClone), varDecl->location);
        newDecl->isMutable = varDecl->isMutable;
        newDecl->isConst = varDecl->isConst;
        return newDecl;
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        auto exprClone = transformExpression(exprStmt->expr.get());
        return std::make_unique<ExprStmt>(std::move(exprClone), exprStmt->location);
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        auto targetClone = transformExpression(assignStmt->target.get());
        auto valueClone = transformExpression(assignStmt->value.get());
        return std::make_unique<AssignStmt>(std::move(targetClone), assignStmt->op, std::move(valueClone), assignStmt->location);
    }
    else if (auto* breakStmt = dynamic_cast<BreakStmt*>(stmt)) {
        return std::make_unique<BreakStmt>(breakStmt->location);
    }
    else if (auto* continueStmt = dynamic_cast<ContinueStmt*>(stmt)) {
        return std::make_unique<ContinueStmt>(continueStmt->location);
    }
    
    return nullptr;
}

ExprPtr TailCallOptimizationPass::transformExpression(Expression* expr) {
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
    else if (auto* boolLit = dynamic_cast<BoolLiteral*>(expr)) {
        return std::make_unique<BoolLiteral>(boolLit->value, boolLit->location);
    }
    else if (auto* nilLit = dynamic_cast<NilLiteral*>(expr)) {
        return std::make_unique<NilLiteral>(nilLit->location);
    }
    else if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        return std::make_unique<Identifier>(ident->name, ident->location);
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        return std::make_unique<BinaryExpr>(
            transformExpression(binary->left.get()),
            binary->op,
            transformExpression(binary->right.get()),
            binary->location
        );
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        return std::make_unique<UnaryExpr>(
            unary->op,
            transformExpression(unary->operand.get()),
            unary->location
        );
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        auto newCall = std::make_unique<CallExpr>(
            transformExpression(call->callee.get()),
            call->location
        );
        for (auto& arg : call->args) {
            newCall->args.push_back(transformExpression(arg.get()));
        }
        return newCall;
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        return std::make_unique<TernaryExpr>(
            transformExpression(ternary->condition.get()),
            transformExpression(ternary->thenExpr.get()),
            transformExpression(ternary->elseExpr.get()),
            ternary->location
        );
    }
    else if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        return std::make_unique<MemberExpr>(
            transformExpression(member->object.get()),
            member->member,
            member->location
        );
    }
    else if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        return std::make_unique<IndexExpr>(
            transformExpression(index->object.get()),
            transformExpression(index->index.get()),
            index->location
        );
    }
    
    return nullptr;
}

} // namespace tyl
