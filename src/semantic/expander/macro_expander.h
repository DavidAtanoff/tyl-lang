// Tyl Compiler - Macro Expander
#ifndef TYL_MACRO_EXPANDER_H
#define TYL_MACRO_EXPANDER_H

#include "frontend/ast/ast.h"
#include <unordered_map>
#include <unordered_set>

namespace tyl {

struct MacroInfo {
    std::string name;
    std::vector<std::string> params;
    std::vector<StmtPtr>* body;
    std::string layerName;
    bool isStatementMacro = false;
    bool hasBlock = false;
    bool isInfix = false;
    std::string operatorSymbol;
    int precedence = 0;
};

struct DSLTransformInfo {
    std::string name;
    std::string transformExpr;
    std::vector<StmtPtr>* body;
};

class MacroExpander {
public:
    MacroExpander() = default;
    void expand(Program& program);
    const std::vector<std::string>& getErrors() const { return errors_; }
    bool hasErrors() const { return !errors_.empty(); }

private:
    void collectMacros(Program& program);
    void collectLayerMacros(LayerDecl& layer);
    void processUseStatements(Program& program);
    void expandStatements(std::vector<StmtPtr>& statements);
    void expandStatement(StmtPtr& stmt);
    void expandExpression(ExprPtr& expr);
    bool isMacroCall(const std::string& name) const;
    bool isStatementMacro(const std::string& name) const;
    ExprPtr expandMacroCall(const std::string& name, const std::vector<ExprPtr>& args, SourceLocation loc);
    std::vector<StmtPtr> expandStatementMacro(const std::string& name, const std::vector<ExprPtr>& args, StmtPtr blockArg, SourceLocation loc);
    ExprPtr expandInfixMacro(const MacroInfo& macro, ExprPtr left, ExprPtr right, SourceLocation loc);
    ExprPtr convertIfToTernary(IfStmt* ifStmt, const std::unordered_map<std::string, Expression*>& params, SourceLocation loc);
    ExprPtr transformDSLBlock(const std::string& dslName, const std::string& content, SourceLocation loc);
    ExprPtr cloneExpr(Expression* expr, const std::unordered_map<std::string, Expression*>& params);
    StmtPtr cloneStmt(Statement* stmt, const std::unordered_map<std::string, Expression*>& params);
    std::vector<StmtPtr> cloneStmts(const std::vector<StmtPtr>& stmts, const std::unordered_map<std::string, Expression*>& params, Statement* blockParam = nullptr);
    void error(const std::string& msg, SourceLocation loc);
    
    std::unordered_map<std::string, MacroInfo> allMacros_;
    std::unordered_map<std::string, MacroInfo*> activeMacros_;
    std::unordered_map<std::string, MacroInfo*> infixOperators_;
    std::unordered_map<std::string, DSLTransformInfo> dslTransformers_;
    std::unordered_set<std::string> activeLayers_;
    std::unordered_set<std::string> registeredDSLs_;
    std::vector<std::string> errors_;
};

} // namespace tyl

#endif // TYL_MACRO_EXPANDER_H
