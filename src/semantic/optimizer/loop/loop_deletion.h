// Tyl Compiler - Loop Deletion Optimization Pass
// Removes loops that have no side effects and unused results
// LLVM equivalent: loop-deletion
#ifndef TYL_LOOP_DELETION_H
#define TYL_LOOP_DELETION_H

#include "../optimizer.h"
#include "frontend/ast/ast.h"
#include <set>
#include <map>
#include <vector>

namespace tyl {

// Statistics for Loop Deletion transformations
struct LoopDeletionStats {
    int forLoopsDeleted = 0;
    int whileLoopsDeleted = 0;
    int loopsSkipped = 0;  // Loops not deleted (have side effects, etc.)
};

// Loop Deletion Pass
// Removes loops that:
// 1. Have no side effects (no function calls, no I/O, no stores to non-local vars)
// 2. Have unused results (induction variable not used after loop)
// 3. Have computable trip count (we know the loop terminates)
//
// Example transformations:
//   for i in 0..10 { let x = i * 2 }  // x is unused -> delete loop
//   while (i < 10) { i = i + 1 }      // i not used after -> delete loop
class LoopDeletionPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "LoopDeletion"; }
    
    // Get detailed statistics
    const LoopDeletionStats& stats() const { return stats_; }
    
private:
    LoopDeletionStats stats_;
    
    // Variables used after each loop (for liveness analysis)
    std::set<std::string> liveAfterLoop_;
    
    // Process statements recursively
    void processStatements(std::vector<StmtPtr>& stmts);
    
    // Process a single statement (recurse into nested structures)
    void processStatement(StmtPtr& stmt);
    
    // === Loop Deletion Analysis ===
    
    // Check if a for loop can be deleted
    bool canDeleteForLoop(ForStmt* loop, const std::set<std::string>& liveAfter);
    
    // Check if a while loop can be deleted
    bool canDeleteWhileLoop(WhileStmt* loop, const std::set<std::string>& liveAfter);
    
    // Check if loop body has side effects
    bool hasSideEffects(Statement* stmt);
    
    // Check if an expression has side effects
    bool exprHasSideEffects(Expression* expr);
    
    // Collect variables defined in a loop
    void collectDefinedVars(Statement* stmt, std::set<std::string>& defined);
    
    // Collect variables used in a statement
    void collectUsedVars(Statement* stmt, std::set<std::string>& used);
    
    // Collect variables used in an expression
    void collectUsedVarsExpr(Expression* expr, std::set<std::string>& used);
    
    // Compute variables live after a given index in statement list
    std::set<std::string> computeLiveAfter(const std::vector<StmtPtr>& stmts, size_t index);
    
    // Check if loop has computable trip count (terminates)
    bool hasComputableTripCount(ForStmt* loop);
    bool hasComputableTripCount(WhileStmt* loop);
    
    // Check if induction variable escapes the loop
    bool inductionVarEscapes(ForStmt* loop, const std::set<std::string>& liveAfter);
};

} // namespace tyl

#endif // TYL_LOOP_DELETION_H
