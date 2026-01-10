// Tyl Compiler - Macro Expander Core
// Main expand entry point and collection

#include "expander_base.h"
#include "frontend/macro/syntax_macro.h"

namespace tyl {

void MacroExpander::expand(Program& program) {
    collectMacros(program);
    processUseStatements(program);
    expandStatements(program.statements);
}

void MacroExpander::collectMacros(Program& program) {
    for (auto& stmt : program.statements) {
        if (auto* layer = dynamic_cast<LayerDecl*>(stmt.get())) {
            collectLayerMacros(*layer);
        }
        if (auto* macro = dynamic_cast<MacroDecl*>(stmt.get())) {
            // Check if macro name is a reserved keyword
            if (RESERVED_KEYWORDS.count(macro->name)) {
                error("Cannot define macro with reserved keyword name '" + macro->name + "'", macro->location);
                continue;
            }
            
            // Check if any parameter names are reserved keywords
            for (const auto& param : macro->params) {
                if (RESERVED_KEYWORDS.count(param)) {
                    error("Cannot use reserved keyword '" + param + "' as macro parameter name", macro->location);
                    continue;
                }
            }
            
            MacroInfo info;
            info.name = macro->name;
            info.params = macro->params;
            info.body = &macro->body;
            info.layerName = "";
            info.isInfix = macro->isInfix;
            info.operatorSymbol = macro->operatorSymbol;
            info.precedence = macro->precedence;
            
            // Determine if this is a statement macro (multi-statement body)
            if (macro->body.size() > 1) {
                info.isStatementMacro = true;
            } else if (macro->body.size() == 1) {
                Statement* bodyStmt = macro->body[0].get();
                if (dynamic_cast<IfStmt*>(bodyStmt) || dynamic_cast<WhileStmt*>(bodyStmt) ||
                    dynamic_cast<ForStmt*>(bodyStmt) || dynamic_cast<Block*>(bodyStmt)) {
                    info.isStatementMacro = true;
                }
            }
            
            allMacros_[macro->name] = info;
            activeMacros_[macro->name] = &allMacros_[macro->name];
            
            if (macro->isInfix && !macro->operatorSymbol.empty()) {
                std::string infixName = "__infix_" + macro->operatorSymbol;
                infixOperators_[macro->operatorSymbol] = &allMacros_[macro->name];
                allMacros_[infixName] = info;
                allMacros_[infixName].name = infixName;
                activeMacros_[infixName] = &allMacros_[infixName];
                
                SyntaxMacroRegistry::instance().registerUserInfixOperator(
                    macro->operatorSymbol, macro->precedence,
                    macro->params.size() > 0 ? macro->params[0] : "left",
                    macro->params.size() > 1 ? macro->params[1] : "right",
                    &macro->body
                );
            }
        }
        if (auto* syntaxMacro = dynamic_cast<SyntaxMacroDecl*>(stmt.get())) {
            SyntaxMacroRegistry::instance().registerDSLName(syntaxMacro->name);
            registeredDSLs_.insert(syntaxMacro->name);
            
            if (!syntaxMacro->transformExpr.empty()) {
                DSLTransformInfo transformInfo;
                transformInfo.name = syntaxMacro->name;
                transformInfo.transformExpr = syntaxMacro->transformExpr;
                transformInfo.body = &syntaxMacro->body;
                dslTransformers_[syntaxMacro->name] = transformInfo;
                
                SyntaxMacroRegistry::instance().registerUserDSLTransformer(
                    syntaxMacro->name, syntaxMacro->transformExpr, &syntaxMacro->body
                );
            }
        }
    }
}

void MacroExpander::collectLayerMacros(LayerDecl& layer) {
    for (auto& decl : layer.declarations) {
        if (auto* macro = dynamic_cast<MacroDecl*>(decl.get())) {
            // Check if macro name is a reserved keyword
            if (RESERVED_KEYWORDS.count(macro->name)) {
                error("Cannot define macro with reserved keyword name '" + macro->name + "'", macro->location);
                continue;
            }
            
            // Check if any parameter names are reserved keywords
            for (const auto& param : macro->params) {
                if (RESERVED_KEYWORDS.count(param)) {
                    error("Cannot use reserved keyword '" + param + "' as macro parameter name", macro->location);
                    continue;
                }
            }
            
            std::string fullName = layer.name + "." + macro->name;
            MacroInfo info;
            info.name = macro->name;
            info.params = macro->params;
            info.body = &macro->body;
            info.layerName = layer.name;
            
            if (macro->body.size() > 1) {
                info.isStatementMacro = true;
            } else if (macro->body.size() == 1) {
                Statement* stmt = macro->body[0].get();
                if (dynamic_cast<IfStmt*>(stmt) || dynamic_cast<WhileStmt*>(stmt) ||
                    dynamic_cast<ForStmt*>(stmt) || dynamic_cast<Block*>(stmt)) {
                    info.isStatementMacro = true;
                }
            }
            
            if (!macro->params.empty()) {
                const std::string& lastParam = macro->params.back();
                if (lastParam == "body" || lastParam == "block" || lastParam == "content") {
                    info.hasBlock = true;
                }
            }
            
            allMacros_[fullName] = info;
            allMacros_[macro->name + "@" + layer.name] = info;
        }
    }
}

void MacroExpander::processUseStatements(Program& program) {
    for (auto& stmt : program.statements) {
        if (auto* use = dynamic_cast<UseStmt*>(stmt.get())) {
            activeLayers_.insert(use->layerName);
            
            for (auto& [name, info] : allMacros_) {
                if (info.layerName == use->layerName) {
                    activeMacros_[info.name] = &allMacros_[name];
                }
            }
        }
    }
}

bool MacroExpander::isMacroCall(const std::string& name) const {
    return activeMacros_.find(name) != activeMacros_.end();
}

bool MacroExpander::isStatementMacro(const std::string& name) const {
    auto it = activeMacros_.find(name);
    if (it != activeMacros_.end()) {
        return it->second->isStatementMacro;
    }
    return false;
}

void MacroExpander::error(const std::string& msg, SourceLocation loc) {
    errors_.push_back("Macro expansion error at line " + std::to_string(loc.line) + ": " + msg);
}

std::string MacroExpander::gensym(const std::string& prefix) {
    std::string name = "_gensym_";
    if (!prefix.empty()) {
        name += prefix + "_";
    }
    name += std::to_string(gensymCounter_++);
    return name;
}

std::string MacroExpander::renameHygienic(const std::string& name) {
    return "_hyg_" + name + "_" + std::to_string(gensymCounter_++);
}

void MacroExpander::collectLocalVars(Statement* stmt, std::unordered_set<std::string>& vars) {
    if (!stmt) return;
    
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        vars.insert(varDecl->name);
    }
    else if (auto* multiVar = dynamic_cast<MultiVarDecl*>(stmt)) {
        for (const auto& name : multiVar->names) {
            vars.insert(name);
        }
    }
    else if (auto* destruct = dynamic_cast<DestructuringDecl*>(stmt)) {
        for (const auto& name : destruct->names) {
            vars.insert(name);
        }
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        // Check for assignment expressions that create new variables
        if (auto* assign = dynamic_cast<AssignExpr*>(exprStmt->expr.get())) {
            if (auto* ident = dynamic_cast<Identifier*>(assign->target.get())) {
                vars.insert(ident->name);
            }
        }
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        // Check for assignment statements that create new variables
        if (auto* ident = dynamic_cast<Identifier*>(assignStmt->target.get())) {
            vars.insert(ident->name);
        }
    }
    else if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            collectLocalVars(s.get(), vars);
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        collectLocalVars(ifStmt->thenBranch.get(), vars);
        for (auto& [cond, branch] : ifStmt->elifBranches) {
            collectLocalVars(branch.get(), vars);
        }
        if (ifStmt->elseBranch) collectLocalVars(ifStmt->elseBranch.get(), vars);
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        collectLocalVars(whileStmt->body.get(), vars);
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
        vars.insert(forStmt->var);
        collectLocalVars(forStmt->body.get(), vars);
    }
}

ExprPtr MacroExpander::cloneExprHygienic(Expression* expr, 
                                          const std::unordered_map<std::string, Expression*>& params,
                                          const std::unordered_map<std::string, std::string>& renames,
                                          const std::unordered_set<std::string>& injected) {
    if (!expr) return nullptr;
    
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        // Check if this is a macro parameter
        auto paramIt = params.find(ident->name);
        if (paramIt != params.end()) {
            return cloneExpr(paramIt->second, {});
        }
        
        // Check if this identifier should be renamed (hygienic)
        auto renameIt = renames.find(ident->name);
        if (renameIt != renames.end()) {
            return std::make_unique<Identifier>(renameIt->second, ident->location);
        }
        
        // Keep original name (either injected or external reference)
        return std::make_unique<Identifier>(ident->name, ident->location);
    }
    
    // For other expression types, recursively clone with hygiene
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        return std::make_unique<BinaryExpr>(
            cloneExprHygienic(binary->left.get(), params, renames, injected), binary->op,
            cloneExprHygienic(binary->right.get(), params, renames, injected), binary->location);
    }
    
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        return std::make_unique<UnaryExpr>(
            unary->op, cloneExprHygienic(unary->operand.get(), params, renames, injected), unary->location);
    }
    
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        auto newCall = std::make_unique<CallExpr>(
            cloneExprHygienic(call->callee.get(), params, renames, injected), call->location);
        for (auto& arg : call->args) {
            newCall->args.push_back(cloneExprHygienic(arg.get(), params, renames, injected));
        }
        return newCall;
    }
    
    if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        return std::make_unique<MemberExpr>(
            cloneExprHygienic(member->object.get(), params, renames, injected), 
            member->member, member->location);
    }
    
    if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        return std::make_unique<IndexExpr>(
            cloneExprHygienic(index->object.get(), params, renames, injected),
            cloneExprHygienic(index->index.get(), params, renames, injected), index->location);
    }
    
    if (auto* assign = dynamic_cast<AssignExpr*>(expr)) {
        return std::make_unique<AssignExpr>(
            cloneExprHygienic(assign->target.get(), params, renames, injected),
            assign->op,
            cloneExprHygienic(assign->value.get(), params, renames, injected),
            assign->location);
    }
    
    // Fall back to non-hygienic clone for other types
    return cloneExpr(expr, params);
}

StmtPtr MacroExpander::cloneStmtHygienic(Statement* stmt, 
                                          const std::unordered_map<std::string, Expression*>& params,
                                          const std::unordered_map<std::string, std::string>& renames,
                                          const std::unordered_set<std::string>& injected) {
    if (!stmt) return nullptr;
    
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        // Check if this variable name is a macro parameter - if so, convert to assignment
        auto paramIt = params.find(varDecl->name);
        if (paramIt != params.end() && varDecl->initializer) {
            // This is an assignment to a macro parameter, convert to AssignStmt
            return std::make_unique<AssignStmt>(
                cloneExpr(paramIt->second, {}), TokenType::ASSIGN,
                cloneExprHygienic(varDecl->initializer.get(), params, renames, injected), varDecl->location);
        }
        
        std::string newName = varDecl->name;
        auto renameIt = renames.find(varDecl->name);
        if (renameIt != renames.end()) {
            newName = renameIt->second;
        }
        
        auto newDecl = std::make_unique<VarDecl>(
            newName, varDecl->typeName,
            varDecl->initializer ? cloneExprHygienic(varDecl->initializer.get(), params, renames, injected) : nullptr,
            varDecl->location);
        newDecl->isMutable = varDecl->isMutable;
        newDecl->isConst = varDecl->isConst;
        return newDecl;
    }
    
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        return std::make_unique<ExprStmt>(
            cloneExprHygienic(exprStmt->expr.get(), params, renames, injected), exprStmt->location);
    }
    
    if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        return std::make_unique<AssignStmt>(
            cloneExprHygienic(assignStmt->target.get(), params, renames, injected), assignStmt->op,
            cloneExprHygienic(assignStmt->value.get(), params, renames, injected), assignStmt->location);
    }
    
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        auto newBlock = std::make_unique<Block>(block->location);
        for (auto& s : block->statements) {
            newBlock->statements.push_back(cloneStmtHygienic(s.get(), params, renames, injected));
        }
        return newBlock;
    }
    
    if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        auto newIf = std::make_unique<IfStmt>(
            cloneExprHygienic(ifStmt->condition.get(), params, renames, injected),
            cloneStmtHygienic(ifStmt->thenBranch.get(), params, renames, injected), ifStmt->location);
        for (auto& [cond, branch] : ifStmt->elifBranches) {
            newIf->elifBranches.emplace_back(
                cloneExprHygienic(cond.get(), params, renames, injected), 
                cloneStmtHygienic(branch.get(), params, renames, injected));
        }
        if (ifStmt->elseBranch) {
            newIf->elseBranch = cloneStmtHygienic(ifStmt->elseBranch.get(), params, renames, injected);
        }
        return newIf;
    }
    
    if (auto* retStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        return std::make_unique<ReturnStmt>(
            retStmt->value ? cloneExprHygienic(retStmt->value.get(), params, renames, injected) : nullptr, 
            retStmt->location);
    }
    
    // Fall back to non-hygienic clone for other types
    return cloneStmt(stmt, params);
}

} // namespace tyl
