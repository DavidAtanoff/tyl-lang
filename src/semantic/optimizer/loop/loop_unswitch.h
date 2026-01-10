// Tyl Compiler - Loop Unswitching Optimization Pass
// Moves loop-invariant conditionals out of loops by duplicating the loop body
#ifndef TYL_LOOP_UNSWITCH_H
#define TYL_LOOP_UNSWITCH_H

#include "../optimizer.h"
#include "frontend/ast/ast.h"
#include <set>
#include <map>
#include <vector>

namespace tyl {

// Statistics for Loop Unswitching
struct LoopUnswitchStats {
    int loopsUnswitched = 0;
    int conditionsHoisted = 0;
    int loopsDuplicated = 0;
};

// Loop Unswitching Pass
// Transforms:
//   for i in range(n):
//       if (invariant_cond):
//           A(i)
//       else:
//           B(i)
// Into:
//   if (invariant_cond):
//       for i in range(n):
//           A(i)
//   else:
//       for i in range(n):
//           B(i)
class LoopUnswitchPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "LoopUnswitch"; }
    
    // Get detailed statistics
    const LoopUnswitchStats& stats() const { return stats_; }
    
    // Configuration
    void setMaxLoopSize(size_t size) { maxLoopSize_ = size; }
    void setMaxUnswitchCount(int count) { maxUnswitchCount_ = count; }
    
private:
    LoopUnswitchStats stats_;
    size_t maxLoopSize_ = 50;      // Max statements in loop to consider
    int maxUnswitchCount_ = 3;     // Max times to unswitch a single loop
    
    // Process a function
    void processFunction(FnDecl* fn);
    
    // Process statements recursively
    void processStatements(std::vector<StmtPtr>& stmts);
    
    // Try to unswitch a for loop
    bool tryUnswitchForLoop(std::vector<StmtPtr>& stmts, size_t index, ForStmt* loop);
    
    // Try to unswitch a while loop
    bool tryUnswitchWhileLoop(std::vector<StmtPtr>& stmts, size_t index, WhileStmt* loop);
    
    // Find loop-invariant conditions in a loop body
    std::vector<IfStmt*> findInvariantConditions(Statement* body, 
                                                  const std::set<std::string>& loopVars);
    
    // Check if an expression is loop-invariant
    bool isLoopInvariant(Expression* expr, const std::set<std::string>& loopVars);
    
    // Collect variables modified in a statement
    void collectModifiedVars(Statement* stmt, std::set<std::string>& vars);
    
    // Collect variables used in an expression
    void collectUsedVars(Expression* expr, std::set<std::string>& vars);
    
    // Count statements in a block
    size_t countStatements(Statement* stmt);
    
    // Clone a statement
    StmtPtr cloneStatement(Statement* stmt);
    
    // Clone an expression
    ExprPtr cloneExpression(Expression* expr);
    
    // Extract the then-branch version of a loop (condition is true)
    StmtPtr createThenVersionLoop(ForStmt* loop, IfStmt* cond);
    StmtPtr createThenVersionLoop(WhileStmt* loop, IfStmt* cond);
    
    // Extract the else-branch version of a loop (condition is false)
    StmtPtr createElseVersionLoop(ForStmt* loop, IfStmt* cond);
    StmtPtr createElseVersionLoop(WhileStmt* loop, IfStmt* cond);
    
    // Replace an if statement in a block with its then/else branch
    void replaceIfWithBranch(Block* block, IfStmt* ifStmt, bool useThenBranch);
    
    // Compare two expressions for structural equality
    bool conditionsMatch(Expression* a, Expression* b);
};

} // namespace tyl

#endif // TYL_LOOP_UNSWITCH_H
