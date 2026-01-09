// Tyl Compiler - Tail Call Optimization Pass
// Converts tail-recursive calls into loops to prevent stack overflow
#ifndef TYL_TAIL_CALL_H
#define TYL_TAIL_CALL_H

#include "optimizer.h"
#include "frontend/ast/ast.h"
#include <map>
#include <set>

namespace tyl {

// Information about tail calls in a function
struct TailCallInfo {
    FnDecl* decl = nullptr;
    bool hasTailRecursion = false;
    bool canOptimize = false;
    std::vector<ReturnStmt*> tailCalls;  // Return statements with tail calls
};

class TailCallOptimizationPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "TailCallOptimization"; }
    
private:
    // Analysis phase
    void collectFunctions(Program& ast);
    void analyzeTailCalls();
    bool isTailCall(ReturnStmt* ret, const std::string& fnName);
    bool isTailPosition(Statement* stmt, Statement* parent);
    void findTailCalls(Statement* stmt, const std::string& fnName, 
                       std::vector<ReturnStmt*>& tailCalls);
    
    // Transformation phase
    void optimizeTailCalls(Program& ast);
    StmtPtr transformToLoop(FnDecl* fn);
    StmtPtr transformStatement(Statement* stmt, const std::string& fnName,
                               const std::string& loopLabel,
                               const std::vector<std::string>& paramNames);
    ExprPtr transformExpression(Expression* expr);
    
    // Function database
    std::map<std::string, TailCallInfo> functions_;
    
    // Label generation
    int labelCounter_ = 0;
    std::string newLabel(const std::string& prefix);
};

} // namespace tyl

#endif // TYL_TAIL_CALL_H
