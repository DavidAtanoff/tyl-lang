// Tyl Compiler - Macro Expander
#ifndef TYL_MACRO_EXPANDER_H
#define TYL_MACRO_EXPANDER_H

#include "frontend/ast/ast.h"
#include <unordered_map>
#include <unordered_set>

namespace tyl {

// Reserved keywords that cannot be used as macro names or parameters
// This prevents macros from interfering with language syntax
inline const std::unordered_set<std::string> RESERVED_KEYWORDS = {
    // Control flow
    "fn", "if", "else", "elif", "for", "while", "match", "return",
    "break", "continue", "loop", "unless", "then", "do", "end",
    // Literals
    "true", "false", "nil", "null",
    // Logical operators
    "and", "or", "not", "in", "to", "by", "is",
    // Error handling
    "try",
    // Modules and macros
    "use", "layer", "macro", "import", "module", "extern", "export", "from",
    // Async
    "async", "await", "spawn",
    // Types
    "record", "enum", "union", "type", "alias", "syntax",
    // Variables
    "let", "mut", "const",
    // Memory
    "unsafe", "ptr", "ref", "new", "delete", "asm",
    // Visibility
    "pub", "priv",
    // OOP
    "self", "super", "trait", "impl",
    // Concurrency
    "chan", "Mutex", "RWLock", "Cond", "Semaphore", "lock", "Atomic",
    // Smart pointers
    "Box", "Rc", "Arc", "Weak", "Cell", "RefCell",
    // Attributes
    "inline", "noinline", "packed", "align", "repr", "hidden",
    "cdecl", "stdcall", "fastcall", "naked", "comptime",
    // Contracts
    "assert", "require", "ensure", "invariant",
    // Scoping
    "scope", "with", "where",
    // Effects
    "effect", "handle", "perform", "resume",
    // Concepts
    "concept"
};

struct MacroInfo {
    std::string name;
    std::vector<std::string> params;
    std::vector<StmtPtr>* body;
    std::string layerName;
    bool isStatementMacro = false;
    bool hasBlock = false;
    bool isInfix = false;
    bool isHygienic = true;  // Default to hygienic macros
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
    
    // Generate a unique symbol name for hygienic macros
    std::string gensym(const std::string& prefix = "");

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
    
    // Hygienic macro support
    std::string renameHygienic(const std::string& name);
    void collectLocalVars(Statement* stmt, std::unordered_set<std::string>& vars);
    ExprPtr cloneExprHygienic(Expression* expr, const std::unordered_map<std::string, Expression*>& params, 
                              const std::unordered_map<std::string, std::string>& renames,
                              const std::unordered_set<std::string>& injected);
    StmtPtr cloneStmtHygienic(Statement* stmt, const std::unordered_map<std::string, Expression*>& params,
                              const std::unordered_map<std::string, std::string>& renames,
                              const std::unordered_set<std::string>& injected);
    
    void error(const std::string& msg, SourceLocation loc);
    
    std::unordered_map<std::string, MacroInfo> allMacros_;
    std::unordered_map<std::string, MacroInfo*> activeMacros_;
    std::unordered_map<std::string, MacroInfo*> infixOperators_;
    std::unordered_map<std::string, DSLTransformInfo> dslTransformers_;
    std::unordered_set<std::string> activeLayers_;
    std::unordered_set<std::string> registeredDSLs_;
    std::vector<std::string> errors_;
    
    // Gensym counter for unique symbol generation
    uint64_t gensymCounter_ = 0;
};

} // namespace tyl

#endif // TYL_MACRO_EXPANDER_H
