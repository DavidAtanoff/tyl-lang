// Tyl Compiler - Function Inlining Optimization Pass
// Inlines small functions to eliminate call overhead
#ifndef TYL_INLINING_H
#define TYL_INLINING_H

#include "optimizer.h"
#include "frontend/ast/ast.h"
#include <map>
#include <set>

namespace tyl {

// Information about a function for inlining decisions
struct FunctionInfo {
    FnDecl* decl = nullptr;
    size_t statementCount = 0;
    size_t callCount = 0;
    size_t expressionComplexity = 0;  // Complexity score for expressions
    bool hasRecursion = false;
    bool hasSideEffects = false;
    bool isSimple = false;  // Can be inlined
    bool isSingleReturn = false;  // Function has single return statement
    bool isPureFunction = false;  // No side effects, deterministic
};

class InliningPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "Inlining"; }
    
    // Configuration
    void setMaxInlineStatements(size_t max) { maxInlineStatements_ = max; }
    void setMaxInlineCallCount(size_t max) { maxInlineCallCount_ = max; }
    void setMaxExpressionComplexity(size_t max) { maxExpressionComplexity_ = max; }
    void setAggressiveInlining(bool aggressive) { aggressiveInlining_ = aggressive; }
    
private:
    // Analysis phase
    void collectFunctions(Program& ast);
    void analyzeFunctions();
    size_t countStatements(Statement* stmt);
    size_t countExpressionComplexity(Expression* expr);
    void countCalls(Statement* stmt);
    bool checkRecursion(FnDecl* fn, const std::string& targetName);
    bool checkSideEffects(Statement* stmt);
    bool checkSideEffectsInExpr(Expression* expr);
    bool isSingleReturnFunction(FnDecl* fn);
    Expression* getSingleReturnExpr(FnDecl* fn);
    
    // Transformation phase
    void inlineCalls(Program& ast);
    void processStatement(StmtPtr& stmt);
    void processBlock(std::vector<StmtPtr>& statements);
    ExprPtr processExpression(ExprPtr& expr);
    
    // Inline a specific call
    StmtPtr inlineCall(CallExpr* call, FnDecl* fn);
    ExprPtr inlineCallAsExpr(CallExpr* call, FnDecl* fn);  // For single-return functions
    ExprPtr cloneExpression(Expression* expr, const std::map<std::string, Expression*>& argMap, 
                           std::map<std::string, std::string>& renameMap);
    StmtPtr cloneStatement(Statement* stmt, const std::map<std::string, Expression*>& argMap,
                          std::map<std::string, std::string>& renameMap);
    
    // Function database
    std::map<std::string, FunctionInfo> functions_;
    std::set<std::string> inlineCandidates_;
    std::set<std::string> exprInlineCandidates_;  // Functions that can be inlined as expressions
    
    // Configuration
    size_t maxInlineStatements_ = 10;   // Max statements in function to inline
    size_t maxInlineCallCount_ = 5;     // Max times a function can be inlined
    size_t maxExpressionComplexity_ = 20;  // Max expression complexity for inline
    bool aggressiveInlining_ = false;   // Inline even with side effects
    
    // State during inlining
    std::map<std::string, size_t> inlineCount_;  // How many times each function was inlined
    int uniqueVarCounter_ = 0;  // For generating unique variable names
    
    std::string generateUniqueName(const std::string& base);
};

} // namespace tyl

#endif // TYL_INLINING_H
