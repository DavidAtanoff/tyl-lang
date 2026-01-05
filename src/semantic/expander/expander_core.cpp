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
            MacroInfo info;
            info.name = macro->name;
            info.params = macro->params;
            info.body = &macro->body;
            info.layerName = "";
            info.isInfix = macro->isInfix;
            info.operatorSymbol = macro->operatorSymbol;
            info.precedence = macro->precedence;
            
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

} // namespace tyl
