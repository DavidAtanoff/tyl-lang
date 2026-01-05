// Tyl Compiler - Compile-Time Function Execution (CTFE)
// Evaluates pure functions with constant arguments at compile time
#ifndef TYL_CTFE_H
#define TYL_CTFE_H

#include "optimizer.h"
#include "frontend/ast/ast.h"
#include <map>
#include <set>
#include <variant>
#include <optional>

namespace tyl {

// Value types that can be computed at compile time
using CTFEValue = std::variant<int64_t, double, bool, std::string>;

// Special result type for loop control flow
enum class LoopControl { None, Continue, Break };

// Information about a function for CTFE
struct CTFEFunctionInfo {
    FnDecl* decl = nullptr;
    bool isPure = false;           // No side effects, deterministic
    bool isRecursive = false;      // Contains recursive calls
    bool canCTFE = false;          // Can be executed at compile time
    size_t maxRecursionDepth = 100; // Limit for recursive evaluation
};

class CTFEPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "CTFE"; }
    
    // Configuration
    void setMaxRecursionDepth(size_t depth) { maxRecursionDepth_ = depth; }
    void setMaxIterations(size_t iters) { maxIterations_ = iters; }
    
private:
    // Analysis phase
    void collectFunctions(Program& ast);
    void analyzeFunctions();
    bool checkPurity(Statement* stmt);
    bool checkPurityExpr(Expression* expr);
    bool checkRecursion(FnDecl* fn, const std::string& targetName);
    
    // Transformation phase
    void transformProgram(Program& ast);
    void processStatement(StmtPtr& stmt);
    void processBlock(std::vector<StmtPtr>& statements);
    ExprPtr processExpression(ExprPtr& expr);
    
    // CTFE evaluation
    std::optional<CTFEValue> evaluateCall(CallExpr* call);
    std::optional<CTFEValue> evaluateFunction(FnDecl* fn, 
        const std::vector<CTFEValue>& args, size_t depth = 0);
    std::optional<CTFEValue> evaluateStatement(Statement* stmt,
        std::map<std::string, CTFEValue>& env, size_t depth);
    std::optional<CTFEValue> evaluateExpression(Expression* expr,
        const std::map<std::string, CTFEValue>& env, size_t depth);
    
    // Helper to create literal from CTFE value
    ExprPtr createLiteral(const CTFEValue& value, const SourceLocation& loc);
    
    // Function database
    std::map<std::string, CTFEFunctionInfo> functions_;
    std::set<std::string> ctfeCandidates_;
    
    // Configuration
    size_t maxRecursionDepth_ = 100;
    size_t maxIterations_ = 10000;
    
    // State during evaluation
    size_t currentIterations_ = 0;
    LoopControl loopControl_ = LoopControl::None;  // Track continue/break
};

} // namespace tyl

#endif // TYL_CTFE_H
