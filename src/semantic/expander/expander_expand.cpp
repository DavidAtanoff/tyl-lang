// Tyl Compiler - Macro Expander Statement/Expression Expansion

#include "expander_base.h"

namespace tyl {

void MacroExpander::expandStatements(std::vector<StmtPtr>& statements) {
    for (auto& stmt : statements) {
        expandStatement(stmt);
    }
}

void MacroExpander::expandStatement(StmtPtr& stmt) {
    if (!stmt) return;
    
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt.get())) {
        if (auto* call = dynamic_cast<CallExpr*>(exprStmt->expr.get())) {
            if (auto* ident = dynamic_cast<Identifier*>(call->callee.get())) {
                if (isMacroCall(ident->name) && isStatementMacro(ident->name)) {
                    auto expanded = expandStatementMacro(ident->name, call->args, nullptr, stmt->location);
                    if (!expanded.empty()) {
                        auto block = std::make_unique<Block>(stmt->location);
                        for (auto& s : expanded) {
                            block->statements.push_back(std::move(s));
                        }
                        stmt = std::move(block);
                        expandStatement(stmt);
                        return;
                    }
                }
            }
        }
        expandExpression(exprStmt->expr);
    }
    else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt.get())) {
        if (varDecl->initializer) expandExpression(varDecl->initializer);
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt.get())) {
        expandExpression(assignStmt->target);
        expandExpression(assignStmt->value);
    }
    else if (auto* block = dynamic_cast<Block*>(stmt.get())) {
        expandStatements(block->statements);
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
        expandExpression(ifStmt->condition);
        expandStatement(ifStmt->thenBranch);
        for (auto& [cond, branch] : ifStmt->elifBranches) {
            expandExpression(cond);
            expandStatement(branch);
        }
        if (ifStmt->elseBranch) expandStatement(ifStmt->elseBranch);
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt.get())) {
        expandExpression(whileStmt->condition);
        expandStatement(whileStmt->body);
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt.get())) {
        expandExpression(forStmt->iterable);
        expandStatement(forStmt->body);
    }
    else if (auto* matchStmt = dynamic_cast<MatchStmt*>(stmt.get())) {
        expandExpression(matchStmt->value);
        for (auto& case_ : matchStmt->cases) {
            expandExpression(case_.pattern);
            if (case_.guard) expandExpression(case_.guard);
            expandStatement(case_.body);
        }
        if (matchStmt->defaultCase) expandStatement(matchStmt->defaultCase);
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt.get())) {
        if (returnStmt->value) expandExpression(returnStmt->value);
    }
    else if (auto* fnDecl = dynamic_cast<FnDecl*>(stmt.get())) {
        if (fnDecl->body) expandStatement(fnDecl->body);
    }
    else if (auto* unsafeBlock = dynamic_cast<UnsafeBlock*>(stmt.get())) {
        expandStatement(unsafeBlock->body);
    }
    else if (auto* deleteStmt = dynamic_cast<DeleteStmt*>(stmt.get())) {
        expandExpression(deleteStmt->expr);
    }
}

void MacroExpander::expandExpression(ExprPtr& expr) {
    if (!expr) return;
    
    if (auto* dslBlock = dynamic_cast<DSLBlock*>(expr.get())) {
        auto it = dslTransformers_.find(dslBlock->dslName);
        if (it != dslTransformers_.end()) {
            expr = transformDSLBlock(dslBlock->dslName, dslBlock->rawContent, dslBlock->location);
            return;
        }
        
        if (dslBlock->dslName == "sql") {
            auto dbIdent = std::make_unique<Identifier>("db", dslBlock->location);
            auto queryMember = std::make_unique<MemberExpr>(std::move(dbIdent), "query", dslBlock->location);
            auto call = std::make_unique<CallExpr>(std::move(queryMember), dslBlock->location);
            call->args.push_back(std::make_unique<StringLiteral>(dslBlock->rawContent, dslBlock->location));
            expr = std::move(call);
        } else {
            expr = std::make_unique<StringLiteral>(dslBlock->rawContent, dslBlock->location);
        }
        return;
    }
    
    if (auto* call = dynamic_cast<CallExpr*>(expr.get())) {
        if (auto* ident = dynamic_cast<Identifier*>(call->callee.get())) {
            if (isMacroCall(ident->name)) {
                auto expanded = expandMacroCall(ident->name, call->args, expr->location);
                if (expanded) {
                    expr = std::move(expanded);
                    expandExpression(expr);
                    return;
                }
            }
        }
        expandExpression(call->callee);
        for (auto& arg : call->args) expandExpression(arg);
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr.get())) {
        expandExpression(binary->left);
        expandExpression(binary->right);
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr.get())) {
        expandExpression(unary->operand);
    }
    else if (auto* member = dynamic_cast<MemberExpr*>(expr.get())) {
        expandExpression(member->object);
    }
    else if (auto* index = dynamic_cast<IndexExpr*>(expr.get())) {
        expandExpression(index->object);
        expandExpression(index->index);
    }
    else if (auto* list = dynamic_cast<ListExpr*>(expr.get())) {
        for (auto& elem : list->elements) expandExpression(elem);
    }
    else if (auto* record = dynamic_cast<RecordExpr*>(expr.get())) {
        for (auto& [name, value] : record->fields) expandExpression(value);
    }
    else if (auto* map = dynamic_cast<MapExpr*>(expr.get())) {
        for (auto& [key, value] : map->entries) {
            expandExpression(key);
            expandExpression(value);
        }
    }
    else if (auto* range = dynamic_cast<RangeExpr*>(expr.get())) {
        expandExpression(range->start);
        expandExpression(range->end);
        if (range->step) expandExpression(range->step);
    }
    else if (auto* lambda = dynamic_cast<LambdaExpr*>(expr.get())) {
        expandExpression(lambda->body);
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr.get())) {
        expandExpression(ternary->condition);
        expandExpression(ternary->thenExpr);
        expandExpression(ternary->elseExpr);
    }
    else if (auto* listComp = dynamic_cast<ListCompExpr*>(expr.get())) {
        expandExpression(listComp->expr);
        expandExpression(listComp->iterable);
        if (listComp->condition) expandExpression(listComp->condition);
    }
    else if (auto* addrOf = dynamic_cast<AddressOfExpr*>(expr.get())) {
        expandExpression(addrOf->operand);
    }
    else if (auto* deref = dynamic_cast<DerefExpr*>(expr.get())) {
        expandExpression(deref->operand);
    }
    else if (auto* newExpr = dynamic_cast<NewExpr*>(expr.get())) {
        for (auto& arg : newExpr->args) expandExpression(arg);
    }
    else if (auto* cast = dynamic_cast<CastExpr*>(expr.get())) {
        expandExpression(cast->expr);
    }
    else if (auto* assign = dynamic_cast<AssignExpr*>(expr.get())) {
        expandExpression(assign->target);
        expandExpression(assign->value);
    }
    else if (auto* propagate = dynamic_cast<PropagateExpr*>(expr.get())) {
        expandExpression(propagate->operand);
    }
    else if (auto* await = dynamic_cast<AwaitExpr*>(expr.get())) {
        expandExpression(await->operand);
    }
    else if (auto* spawn = dynamic_cast<SpawnExpr*>(expr.get())) {
        expandExpression(spawn->operand);
    }
}

ExprPtr MacroExpander::expandMacroCall(const std::string& name, const std::vector<ExprPtr>& args, SourceLocation loc) {
    auto it = activeMacros_.find(name);
    if (it == activeMacros_.end()) {
        error("Unknown macro: " + name, loc);
        return nullptr;
    }
    
    MacroInfo* macro = it->second;
    
    if (args.size() != macro->params.size()) {
        error("Macro '" + name + "' expects " + std::to_string(macro->params.size()) + 
              " arguments, got " + std::to_string(args.size()), loc);
        return nullptr;
    }
    
    std::unordered_map<std::string, Expression*> paramMap;
    for (size_t i = 0; i < macro->params.size(); i++) {
        paramMap[macro->params[i]] = args[i].get();
    }
    
    if (macro->body && !macro->body->empty()) {
        Statement* lastStmt = macro->body->back().get();
        
        if (auto* exprStmt = dynamic_cast<ExprStmt*>(lastStmt)) {
            return cloneExpr(exprStmt->expr.get(), paramMap);
        }
        if (auto* retStmt = dynamic_cast<ReturnStmt*>(lastStmt)) {
            if (retStmt->value) return cloneExpr(retStmt->value.get(), paramMap);
        }
        if (auto* ifStmt = dynamic_cast<IfStmt*>(lastStmt)) {
            return convertIfToTernary(ifStmt, paramMap, loc);
        }
    }
    
    error("Macro '" + name + "' does not produce a value", loc);
    return nullptr;
}

std::vector<StmtPtr> MacroExpander::expandStatementMacro(const std::string& name, 
                                                          const std::vector<ExprPtr>& args,
                                                          StmtPtr blockArg,
                                                          SourceLocation loc) {
    auto it = activeMacros_.find(name);
    if (it == activeMacros_.end()) {
        error("Unknown macro: " + name, loc);
        return {};
    }
    
    MacroInfo* macro = it->second;
    
    std::unordered_map<std::string, Expression*> paramMap;
    size_t argCount = args.size();
    size_t paramCount = macro->params.size();
    
    if (macro->hasBlock && blockArg) paramCount--;
    
    if (argCount != paramCount) {
        error("Macro '" + name + "' expects " + std::to_string(paramCount) + 
              " arguments, got " + std::to_string(argCount), loc);
        return {};
    }
    
    for (size_t i = 0; i < argCount; i++) {
        paramMap[macro->params[i]] = args[i].get();
    }
    
    return cloneStmts(*macro->body, paramMap, blockArg.get());
}

ExprPtr MacroExpander::expandInfixMacro(const MacroInfo& macro, ExprPtr left, ExprPtr right, SourceLocation loc) {
    std::unordered_map<std::string, Expression*> paramMap;
    
    if (macro.params.size() >= 1) paramMap[macro.params[0]] = left.get();
    if (macro.params.size() >= 2) paramMap[macro.params[1]] = right.get();
    
    if (macro.body && !macro.body->empty()) {
        Statement* lastStmt = macro.body->back().get();
        
        if (auto* exprStmt = dynamic_cast<ExprStmt*>(lastStmt)) {
            return cloneExpr(exprStmt->expr.get(), paramMap);
        }
        if (auto* retStmt = dynamic_cast<ReturnStmt*>(lastStmt)) {
            if (retStmt->value) return cloneExpr(retStmt->value.get(), paramMap);
        }
    }
    
    error("Infix macro '" + macro.operatorSymbol + "' does not produce a value", loc);
    return nullptr;
}

ExprPtr MacroExpander::transformDSLBlock(const std::string& dslName, const std::string& content, SourceLocation loc) {
    auto it = dslTransformers_.find(dslName);
    if (it == dslTransformers_.end()) {
        return std::make_unique<StringLiteral>(content, loc);
    }
    
    const DSLTransformInfo& transformer = it->second;
    std::string transformExpr = transformer.transformExpr;
    
    // Look for function call pattern: func_name(content) or func_name($content)
    size_t parenPos = transformExpr.find('(');
    if (parenPos != std::string::npos) {
        std::string calleeStr = transformExpr.substr(0, parenPos);
        
        // Trim whitespace
        while (!calleeStr.empty() && calleeStr.back() == ' ') calleeStr.pop_back();
        while (!calleeStr.empty() && calleeStr.front() == ' ') calleeStr.erase(0, 1);
        
        // Replace dots with underscores for member access
        for (char& c : calleeStr) {
            if (c == '.') c = '_';
        }
        
        // Create the function call with content as argument
        auto callee = std::make_unique<Identifier>(calleeStr, loc);
        auto call = std::make_unique<CallExpr>(std::move(callee), loc);
        call->args.push_back(std::make_unique<StringLiteral>(content, loc));
        return call;
    }
    
    // If no parentheses, just return the content as a string
    return std::make_unique<StringLiteral>(content, loc);
}

} // namespace tyl
