// Tyl Compiler - Dead Code Elimination Implementation
#include "dead_code.h"
#include <queue>

namespace tyl {

void DeadCodeEliminationPass::run(Program& ast) {
    transformations_ = 0;
    usedIdentifiers_.clear();
    calledFunctions_.clear();
    declaredVariables_.clear();
    reachableFunctions_.clear();
    functionDecls_.clear();
    
    // First pass: collect all used identifiers and called functions
    collectUsedIdentifiers(ast);
    
    // Build call graph and compute reachable functions (tree shaking)
    buildCallGraph(ast);
    computeReachableFunctions(ast);
    
    // Second pass: remove dead code
    removeDeadCode(ast);
}

void DeadCodeEliminationPass::buildCallGraph(Program& ast) {
    // First, collect all function declarations
    for (auto& stmt : ast.statements) {
        if (auto* fnDecl = dynamic_cast<FnDecl*>(stmt.get())) {
            functionDecls_[fnDecl->name] = fnDecl;
        }
        else if (auto* moduleDecl = dynamic_cast<ModuleDecl*>(stmt.get())) {
            for (auto& s : moduleDecl->body) {
                if (auto* fn = dynamic_cast<FnDecl*>(s.get())) {
                    // Store with mangled name (module.function)
                    std::string mangledName = moduleDecl->name + "." + fn->name;
                    functionDecls_[mangledName] = fn;
                }
            }
        }
    }
}

void DeadCodeEliminationPass::collectCallsFromStatement(Statement* stmt, std::unordered_set<std::string>& calls) {
    if (!stmt) return;
    
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        collectCallsFromExpression(exprStmt->expr.get(), calls);
    }
    else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        collectCallsFromExpression(varDecl->initializer.get(), calls);
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        collectCallsFromExpression(assignStmt->target.get(), calls);
        collectCallsFromExpression(assignStmt->value.get(), calls);
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        collectCallsFromExpression(ifStmt->condition.get(), calls);
        collectCallsFromStatement(ifStmt->thenBranch.get(), calls);
        for (auto& elif : ifStmt->elifBranches) {
            collectCallsFromExpression(elif.first.get(), calls);
            collectCallsFromStatement(elif.second.get(), calls);
        }
        collectCallsFromStatement(ifStmt->elseBranch.get(), calls);
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        collectCallsFromExpression(whileStmt->condition.get(), calls);
        collectCallsFromStatement(whileStmt->body.get(), calls);
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
        collectCallsFromExpression(forStmt->iterable.get(), calls);
        collectCallsFromStatement(forStmt->body.get(), calls);
    }
    else if (auto* matchStmt = dynamic_cast<MatchStmt*>(stmt)) {
        collectCallsFromExpression(matchStmt->value.get(), calls);
        for (auto& case_ : matchStmt->cases) {
            collectCallsFromExpression(case_.pattern.get(), calls);
            if (case_.guard) collectCallsFromExpression(case_.guard.get(), calls);
            collectCallsFromStatement(case_.body.get(), calls);
        }
        collectCallsFromStatement(matchStmt->defaultCase.get(), calls);
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        collectCallsFromExpression(returnStmt->value.get(), calls);
    }
    else if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            collectCallsFromStatement(s.get(), calls);
        }
    }
    else if (auto* fnDecl = dynamic_cast<FnDecl*>(stmt)) {
        collectCallsFromStatement(fnDecl->body.get(), calls);
    }
    else if (auto* tryStmt = dynamic_cast<TryStmt*>(stmt)) {
        collectCallsFromExpression(tryStmt->tryExpr.get(), calls);
        collectCallsFromExpression(tryStmt->elseExpr.get(), calls);
    }
    else if (auto* unsafeBlock = dynamic_cast<UnsafeBlock*>(stmt)) {
        collectCallsFromStatement(unsafeBlock->body.get(), calls);
    }
}

void DeadCodeEliminationPass::collectCallsFromExpression(Expression* expr, std::unordered_set<std::string>& calls) {
    if (!expr) return;
    
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        if (auto* callee = dynamic_cast<Identifier*>(call->callee.get())) {
            calls.insert(callee->name);
        }
        // Handle module function calls (e.g., math.add)
        else if (auto* member = dynamic_cast<MemberExpr*>(call->callee.get())) {
            if (auto* moduleId = dynamic_cast<Identifier*>(member->object.get())) {
                // Track as "module.function" format
                calls.insert(moduleId->name + "." + member->member);
            }
            // UFCS: x.f() where x is not an identifier (e.g., 5.double())
            // Also track the member name as a potential function call
            calls.insert(member->member);
        }
        collectCallsFromExpression(call->callee.get(), calls);
        for (auto& arg : call->args) {
            collectCallsFromExpression(arg.get(), calls);
        }
        for (auto& namedArg : call->namedArgs) {
            collectCallsFromExpression(namedArg.second.get(), calls);
        }
    }
    else if (auto* assignExpr = dynamic_cast<AssignExpr*>(expr)) {
        // Handle assignment expressions (x = func())
        collectCallsFromExpression(assignExpr->target.get(), calls);
        collectCallsFromExpression(assignExpr->value.get(), calls);
    }
    else if (auto* propagate = dynamic_cast<PropagateExpr*>(expr)) {
        // Handle error propagation expressions (expr?)
        collectCallsFromExpression(propagate->operand.get(), calls);
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        collectCallsFromExpression(binary->left.get(), calls);
        collectCallsFromExpression(binary->right.get(), calls);
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        collectCallsFromExpression(unary->operand.get(), calls);
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        collectCallsFromExpression(ternary->condition.get(), calls);
        collectCallsFromExpression(ternary->thenExpr.get(), calls);
        collectCallsFromExpression(ternary->elseExpr.get(), calls);
    }
    else if (auto* lambda = dynamic_cast<LambdaExpr*>(expr)) {
        collectCallsFromExpression(lambda->body.get(), calls);
    }
    else if (auto* list = dynamic_cast<ListExpr*>(expr)) {
        for (auto& elem : list->elements) {
            collectCallsFromExpression(elem.get(), calls);
        }
    }
    else if (auto* record = dynamic_cast<RecordExpr*>(expr)) {
        for (auto& field : record->fields) {
            collectCallsFromExpression(field.second.get(), calls);
        }
    }
    else if (auto* map = dynamic_cast<MapExpr*>(expr)) {
        for (auto& entry : map->entries) {
            collectCallsFromExpression(entry.first.get(), calls);
            collectCallsFromExpression(entry.second.get(), calls);
        }
    }
    else if (auto* listComp = dynamic_cast<ListCompExpr*>(expr)) {
        collectCallsFromExpression(listComp->expr.get(), calls);
        collectCallsFromExpression(listComp->iterable.get(), calls);
        collectCallsFromExpression(listComp->condition.get(), calls);
    }
    else if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        collectCallsFromExpression(member->object.get(), calls);
    }
    else if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        collectCallsFromExpression(index->object.get(), calls);
        collectCallsFromExpression(index->index.get(), calls);
    }
    else if (auto* range = dynamic_cast<RangeExpr*>(expr)) {
        collectCallsFromExpression(range->start.get(), calls);
        collectCallsFromExpression(range->end.get(), calls);
        collectCallsFromExpression(range->step.get(), calls);
    }
    else if (auto* interp = dynamic_cast<InterpolatedString*>(expr)) {
        for (auto& part : interp->parts) {
            if (std::holds_alternative<ExprPtr>(part)) {
                collectCallsFromExpression(std::get<ExprPtr>(part).get(), calls);
            }
        }
    }
    else if (auto* spawn = dynamic_cast<SpawnExpr*>(expr)) {
        collectCallsFromExpression(spawn->operand.get(), calls);
    }
    else if (auto* await = dynamic_cast<AwaitExpr*>(expr)) {
        collectCallsFromExpression(await->operand.get(), calls);
    }
    else if (auto* addressOf = dynamic_cast<AddressOfExpr*>(expr)) {
        // When taking address of a function, mark it as called/referenced
        if (auto* ident = dynamic_cast<Identifier*>(addressOf->operand.get())) {
            calls.insert(ident->name);
        }
        collectCallsFromExpression(addressOf->operand.get(), calls);
    }
    else if (auto* deref = dynamic_cast<DerefExpr*>(expr)) {
        collectCallsFromExpression(deref->operand.get(), calls);
    }
    else if (auto* handle = dynamic_cast<HandleExpr*>(expr)) {
        // Handle algebraic effects - collect calls from the expression being handled
        collectCallsFromExpression(handle->expr.get(), calls);
        // Also collect calls from handler bodies
        for (auto& handler : handle->handlers) {
            if (handler.body) {
                collectCallsFromStatement(handler.body.get(), calls);
            }
        }
    }
    else if (auto* perform = dynamic_cast<PerformEffectExpr*>(expr)) {
        // Collect calls from perform arguments
        for (auto& arg : perform->args) {
            collectCallsFromExpression(arg.get(), calls);
        }
    }
    else if (auto* resume = dynamic_cast<ResumeExpr*>(expr)) {
        // Collect calls from resume value
        collectCallsFromExpression(resume->value.get(), calls);
    }
}

void DeadCodeEliminationPass::computeReachableFunctions(Program& ast) {
    // Start with entry points: main function and top-level code
    std::queue<std::string> worklist;
    
    // main is always reachable
    reachableFunctions_.insert("main");
    worklist.push("main");
    
    // Collect calls from top-level statements (not inside functions)
    std::unordered_set<std::string> topLevelCalls;
    for (auto& stmt : ast.statements) {
        if (!dynamic_cast<FnDecl*>(stmt.get())) {
            collectCallsFromStatement(stmt.get(), topLevelCalls);
        }
    }
    
    // Add top-level calls to worklist
    for (const auto& call : topLevelCalls) {
        if (reachableFunctions_.find(call) == reachableFunctions_.end()) {
            reachableFunctions_.insert(call);
            worklist.push(call);
        }
    }
    
    // BFS to find all reachable functions
    while (!worklist.empty()) {
        std::string funcName = worklist.front();
        worklist.pop();
        
        auto it = functionDecls_.find(funcName);
        if (it == functionDecls_.end()) continue;
        
        FnDecl* fn = it->second;
        std::unordered_set<std::string> calls;
        collectCallsFromStatement(fn->body.get(), calls);
        
        for (const auto& call : calls) {
            if (reachableFunctions_.find(call) == reachableFunctions_.end()) {
                reachableFunctions_.insert(call);
                worklist.push(call);
            }
        }
    }
}

void DeadCodeEliminationPass::collectUsedIdentifiers(Program& ast) {
    for (auto& stmt : ast.statements) {
        collectFromStatement(stmt.get());
    }
}

void DeadCodeEliminationPass::collectFromStatement(Statement* stmt) {
    if (!stmt) return;
    
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        declaredVariables_[varDecl->name] = false;  // Declared but not yet known if used
        collectFromExpression(varDecl->initializer.get());
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        collectFromExpression(exprStmt->expr.get());
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        collectFromExpression(assignStmt->target.get());
        collectFromExpression(assignStmt->value.get());
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        collectFromExpression(ifStmt->condition.get());
        collectFromStatement(ifStmt->thenBranch.get());
        for (auto& elif : ifStmt->elifBranches) {
            collectFromExpression(elif.first.get());
            collectFromStatement(elif.second.get());
        }
        collectFromStatement(ifStmt->elseBranch.get());
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        collectFromExpression(whileStmt->condition.get());
        collectFromStatement(whileStmt->body.get());
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
        usedIdentifiers_.insert(forStmt->var);  // Loop variable is used
        collectFromExpression(forStmt->iterable.get());
        collectFromStatement(forStmt->body.get());
    }
    else if (auto* matchStmt = dynamic_cast<MatchStmt*>(stmt)) {
        collectFromExpression(matchStmt->value.get());
        for (auto& case_ : matchStmt->cases) {
            collectFromExpression(case_.pattern.get());
            if (case_.guard) collectFromExpression(case_.guard.get());
            collectFromStatement(case_.body.get());
        }
        collectFromStatement(matchStmt->defaultCase.get());
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        collectFromExpression(returnStmt->value.get());
    }
    else if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            collectFromStatement(s.get());
        }
    }
    else if (auto* fnDecl = dynamic_cast<FnDecl*>(stmt)) {
        // Mark parameters as used within the function
        for (auto& param : fnDecl->params) {
            usedIdentifiers_.insert(param.first);
        }
        collectFromStatement(fnDecl->body.get());
    }
    else if (auto* moduleDecl = dynamic_cast<ModuleDecl*>(stmt)) {
        for (auto& s : moduleDecl->body) {
            collectFromStatement(s.get());
        }
    }
    else if (auto* tryStmt = dynamic_cast<TryStmt*>(stmt)) {
        collectFromExpression(tryStmt->tryExpr.get());
        collectFromExpression(tryStmt->elseExpr.get());
    }
    else if (auto* unsafeBlock = dynamic_cast<UnsafeBlock*>(stmt)) {
        collectFromStatement(unsafeBlock->body.get());
    }
}

void DeadCodeEliminationPass::collectFromExpression(Expression* expr) {
    if (!expr) return;
    
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        usedIdentifiers_.insert(ident->name);
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        collectFromExpression(binary->left.get());
        collectFromExpression(binary->right.get());
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        collectFromExpression(unary->operand.get());
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        // Track function calls
        if (auto* callee = dynamic_cast<Identifier*>(call->callee.get())) {
            calledFunctions_.insert(callee->name);
        }
        collectFromExpression(call->callee.get());
        for (auto& arg : call->args) {
            collectFromExpression(arg.get());
        }
        for (auto& namedArg : call->namedArgs) {
            collectFromExpression(namedArg.second.get());
        }
    }
    else if (auto* assignExpr = dynamic_cast<AssignExpr*>(expr)) {
        // Handle assignment expressions (x = value)
        collectFromExpression(assignExpr->target.get());
        collectFromExpression(assignExpr->value.get());
    }
    else if (auto* propagate = dynamic_cast<PropagateExpr*>(expr)) {
        // Handle error propagation expressions (expr?)
        collectFromExpression(propagate->operand.get());
    }
    else if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        collectFromExpression(member->object.get());
    }
    else if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        collectFromExpression(index->object.get());
        collectFromExpression(index->index.get());
    }
    else if (auto* list = dynamic_cast<ListExpr*>(expr)) {
        for (auto& elem : list->elements) {
            collectFromExpression(elem.get());
        }
    }
    else if (auto* record = dynamic_cast<RecordExpr*>(expr)) {
        for (auto& field : record->fields) {
            collectFromExpression(field.second.get());
        }
    }
    else if (auto* map = dynamic_cast<MapExpr*>(expr)) {
        for (auto& entry : map->entries) {
            collectFromExpression(entry.first.get());
            collectFromExpression(entry.second.get());
        }
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        collectFromExpression(ternary->condition.get());
        collectFromExpression(ternary->thenExpr.get());
        collectFromExpression(ternary->elseExpr.get());
    }
    else if (auto* lambda = dynamic_cast<LambdaExpr*>(expr)) {
        collectFromExpression(lambda->body.get());
    }
    else if (auto* listComp = dynamic_cast<ListCompExpr*>(expr)) {
        usedIdentifiers_.insert(listComp->var);
        collectFromExpression(listComp->expr.get());
        collectFromExpression(listComp->iterable.get());
        collectFromExpression(listComp->condition.get());
    }
    else if (auto* range = dynamic_cast<RangeExpr*>(expr)) {
        collectFromExpression(range->start.get());
        collectFromExpression(range->end.get());
        collectFromExpression(range->step.get());
    }
    else if (auto* interp = dynamic_cast<InterpolatedString*>(expr)) {
        for (auto& part : interp->parts) {
            if (std::holds_alternative<ExprPtr>(part)) {
                collectFromExpression(std::get<ExprPtr>(part).get());
            }
        }
    }
    else if (auto* spawn = dynamic_cast<SpawnExpr*>(expr)) {
        collectFromExpression(spawn->operand.get());
    }
    else if (auto* await = dynamic_cast<AwaitExpr*>(expr)) {
        collectFromExpression(await->operand.get());
    }
    else if (auto* addressOf = dynamic_cast<AddressOfExpr*>(expr)) {
        // When taking address of a function, mark it as used
        if (auto* ident = dynamic_cast<Identifier*>(addressOf->operand.get())) {
            calledFunctions_.insert(ident->name);
        }
        collectFromExpression(addressOf->operand.get());
    }
    else if (auto* deref = dynamic_cast<DerefExpr*>(expr)) {
        collectFromExpression(deref->operand.get());
    }
    else if (auto* handle = dynamic_cast<HandleExpr*>(expr)) {
        // Handle algebraic effects - collect from the expression being handled
        collectFromExpression(handle->expr.get());
        // Also collect from handler bodies
        for (auto& handler : handle->handlers) {
            if (handler.body) {
                collectFromStatement(handler.body.get());
            }
        }
    }
    else if (auto* perform = dynamic_cast<PerformEffectExpr*>(expr)) {
        // Collect from perform arguments
        for (auto& arg : perform->args) {
            collectFromExpression(arg.get());
        }
    }
    else if (auto* resume = dynamic_cast<ResumeExpr*>(expr)) {
        // Collect from resume value
        collectFromExpression(resume->value.get());
    }
}

void DeadCodeEliminationPass::removeDeadCode(Program& ast) {
    // Remove unreachable code after terminators
    removeUnreachableCode(ast.statements);
    
    // Simplify constant conditions
    simplifyConstantConditions(ast.statements);
    
    // Remove dead code from blocks
    removeDeadFromBlock(ast.statements);
    
    // Remove unused function declarations
    removeUnusedFunctions(ast.statements);
}

void DeadCodeEliminationPass::removeDeadFromBlock(std::vector<StmtPtr>& statements) {
    // Process nested blocks first
    for (auto& stmt : statements) {
        if (auto* block = dynamic_cast<Block*>(stmt.get())) {
            removeUnreachableCode(block->statements);
            simplifyConstantConditions(block->statements);
            removeDeadFromBlock(block->statements);
        }
        else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
            if (auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
                removeUnreachableCode(thenBlock->statements);
                simplifyConstantConditions(thenBlock->statements);
                removeDeadFromBlock(thenBlock->statements);
            }
            for (auto& elif : ifStmt->elifBranches) {
                if (auto* elifBlock = dynamic_cast<Block*>(elif.second.get())) {
                    removeUnreachableCode(elifBlock->statements);
                    simplifyConstantConditions(elifBlock->statements);
                    removeDeadFromBlock(elifBlock->statements);
                }
            }
            if (auto* elseBlock = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
                removeUnreachableCode(elseBlock->statements);
                simplifyConstantConditions(elseBlock->statements);
                removeDeadFromBlock(elseBlock->statements);
            }
        }
        else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt.get())) {
            if (auto* body = dynamic_cast<Block*>(whileStmt->body.get())) {
                removeUnreachableCode(body->statements);
                simplifyConstantConditions(body->statements);
                removeDeadFromBlock(body->statements);
            }
        }
        else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt.get())) {
            if (auto* body = dynamic_cast<Block*>(forStmt->body.get())) {
                removeUnreachableCode(body->statements);
                simplifyConstantConditions(body->statements);
                removeDeadFromBlock(body->statements);
            }
        }
        else if (auto* fnDecl = dynamic_cast<FnDecl*>(stmt.get())) {
            if (auto* body = dynamic_cast<Block*>(fnDecl->body.get())) {
                removeUnreachableCode(body->statements);
                simplifyConstantConditions(body->statements);
                removeDeadFromBlock(body->statements);
            }
        }
        else if (auto* moduleDecl = dynamic_cast<ModuleDecl*>(stmt.get())) {
            removeUnreachableCode(moduleDecl->body);
            simplifyConstantConditions(moduleDecl->body);
            removeDeadFromBlock(moduleDecl->body);
        }
        else if (auto* unsafeBlock = dynamic_cast<UnsafeBlock*>(stmt.get())) {
            if (auto* body = dynamic_cast<Block*>(unsafeBlock->body.get())) {
                removeUnreachableCode(body->statements);
                simplifyConstantConditions(body->statements);
                removeDeadFromBlock(body->statements);
            }
        }
    }
}

void DeadCodeEliminationPass::removeUnreachableCode(std::vector<StmtPtr>& statements) {
    bool foundTerminator = false;
    size_t terminatorIndex = 0;
    
    for (size_t i = 0; i < statements.size(); ++i) {
        auto* stmt = statements[i].get();
        
        if (dynamic_cast<ReturnStmt*>(stmt) ||
            dynamic_cast<BreakStmt*>(stmt) ||
            dynamic_cast<ContinueStmt*>(stmt)) {
            foundTerminator = true;
            terminatorIndex = i;
            break;
        }
    }
    
    if (foundTerminator && terminatorIndex + 1 < statements.size()) {
        // Count how many statements we're removing
        size_t removed = statements.size() - terminatorIndex - 1;
        transformations_ += static_cast<int>(removed);
        
        // Remove all statements after the terminator
        statements.erase(statements.begin() + terminatorIndex + 1, statements.end());
    }
}

void DeadCodeEliminationPass::simplifyConstantConditions(std::vector<StmtPtr>& statements) {
    for (size_t i = 0; i < statements.size(); ++i) {
        auto* stmt = statements[i].get();
        
        if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
            // Check if condition is a constant boolean
            if (auto* boolLit = dynamic_cast<BoolLiteral*>(ifStmt->condition.get())) {
                if (boolLit->value) {
                    // Condition is always true - replace if with then branch
                    if (ifStmt->thenBranch) {
                        transformations_++;
                        statements[i] = std::move(ifStmt->thenBranch);
                    }
                } else {
                    // Condition is always false - use else branch or remove
                    if (ifStmt->elseBranch) {
                        transformations_++;
                        statements[i] = std::move(ifStmt->elseBranch);
                    } else if (!ifStmt->elifBranches.empty()) {
                        // Convert first elif to if
                        transformations_++;
                        auto& firstElif = ifStmt->elifBranches[0];
                        ifStmt->condition = std::move(firstElif.first);
                        ifStmt->thenBranch = std::move(firstElif.second);
                        ifStmt->elifBranches.erase(ifStmt->elifBranches.begin());
                    }
                    // If no else and no elif, we could remove the if entirely
                    // but that's more aggressive - skip for now
                }
            }
        }
        else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
            // Check for while(false) - dead loop
            if (auto* boolLit = dynamic_cast<BoolLiteral*>(whileStmt->condition.get())) {
                if (!boolLit->value) {
                    // while(false) never executes - mark for removal
                    // For safety, we'll just leave it as is for now
                    // A more aggressive pass could remove it entirely
                }
            }
        }
    }
}

bool DeadCodeEliminationPass::isVariableUsed(const std::string& name) {
    return usedIdentifiers_.count(name) > 0;
}

bool DeadCodeEliminationPass::isFunctionUsed(const std::string& name) {
    // main is always used
    if (name == "main") return true;
    return calledFunctions_.count(name) > 0;
}

bool DeadCodeEliminationPass::isFunctionReachable(const std::string& name) {
    // main is always reachable
    if (name == "main") return true;
    // Built-in functions are always reachable
    if (name == "print" || name == "println" || name == "str" || 
        name == "len" || name == "push" || name == "pop" ||
        name == "hostname" || name == "username" || name == "cpu_count" ||
        name == "year" || name == "month" || name == "day" ||
        name == "hour" || name == "minute" || name == "second" ||
        name == "now" || name == "now_ms" || name == "sleep" ||
        name == "sqrt" || name == "abs" || name == "floor" || name == "ceil") {
        return true;
    }
    return reachableFunctions_.count(name) > 0;
}

void DeadCodeEliminationPass::removeUnusedFunctions(std::vector<StmtPtr>& statements) {
    // Remove function declarations that are not reachable from entry point
    auto it = statements.begin();
    while (it != statements.end()) {
        if (auto* fnDecl = dynamic_cast<FnDecl*>(it->get())) {
            if (!isFunctionReachable(fnDecl->name)) {
                it = statements.erase(it);
                transformations_++;
                continue;
            }
        }
        else if (auto* moduleDecl = dynamic_cast<ModuleDecl*>(it->get())) {
            // Remove unused functions inside modules using mangled names
            auto modIt = moduleDecl->body.begin();
            while (modIt != moduleDecl->body.end()) {
                if (auto* fn = dynamic_cast<FnDecl*>(modIt->get())) {
                    std::string mangledName = moduleDecl->name + "." + fn->name;
                    if (!isFunctionReachable(mangledName)) {
                        modIt = moduleDecl->body.erase(modIt);
                        transformations_++;
                        continue;
                    }
                }
                ++modIt;
            }
        }
        ++it;
    }
}

void DeadCodeEliminationPass::removeUnusedVariables(std::vector<StmtPtr>& statements) {
    // Remove variable declarations that are never used
    auto it = statements.begin();
    while (it != statements.end()) {
        if (auto* varDecl = dynamic_cast<VarDecl*>(it->get())) {
            // Only remove if variable is never referenced AND has no side effects in initializer
            if (!isVariableUsed(varDecl->name) && !hasSideEffects(varDecl->initializer.get())) {
                it = statements.erase(it);
                transformations_++;
                continue;
            }
        }
        else if (auto* block = dynamic_cast<Block*>(it->get())) {
            removeUnusedVariables(block->statements);
        }
        else if (auto* fnDecl = dynamic_cast<FnDecl*>(it->get())) {
            if (auto* body = dynamic_cast<Block*>(fnDecl->body.get())) {
                removeUnusedVariables(body->statements);
            }
        }
        ++it;
    }
}

bool DeadCodeEliminationPass::hasSideEffects(Expression* expr) {
    if (!expr) return false;
    
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        // Function calls may have side effects
        return true;
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        return hasSideEffects(binary->left.get()) || hasSideEffects(binary->right.get());
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        return hasSideEffects(unary->operand.get());
    }
    
    return false;
}

} // namespace tyl
