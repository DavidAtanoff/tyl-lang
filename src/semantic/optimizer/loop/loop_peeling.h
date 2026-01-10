// Tyl Compiler - Loop Peeling Optimization Pass
// Peels the first/last iterations of a loop to enable other optimizations
#ifndef TYL_LOOP_PEELING_H
#define TYL_LOOP_PEELING_H

#include "../optimizer.h"
#include "frontend/ast/ast.h"
#include <set>
#include <vector>

namespace tyl {

// Statistics for Loop Peeling
struct LoopPeelingStats {
    int loopsPeeled = 0;
    int iterationsPeeled = 0;
    int firstIterationsPeeled = 0;
    int lastIterationsPeeled = 0;
};

// Loop Peeling Pass
// Transforms:
//   for i in 0..n:
//       body(i)
// Into:
//   body(0)  // Peeled first iteration
//   for i in 1..n:
//       body(i)
// Or:
//   for i in 0..(n-1):
//       body(i)
//   body(n-1)  // Peeled last iteration
class LoopPeelingPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "LoopPeeling"; }
    
    // Get detailed statistics
    const LoopPeelingStats& stats() const { return stats_; }
    
    // Configuration
    void setMaxPeelCount(int count) { maxPeelCount_ = count; }
    void setPeelFirst(bool peel) { peelFirst_ = peel; }
    void setPeelLast(bool peel) { peelLast_ = peel; }
    
private:
    LoopPeelingStats stats_;
    int maxPeelCount_ = 2;    // Max iterations to peel
    bool peelFirst_ = true;   // Peel first iterations
    bool peelLast_ = true;    // Peel last iterations
    
    // Process a function
    void processFunction(FnDecl* fn);
    
    // Process statements recursively
    void processStatements(std::vector<StmtPtr>& stmts);
    
    // Try to peel a for loop
    bool tryPeelForLoop(std::vector<StmtPtr>& stmts, size_t index, ForStmt* loop);
    
    // Check if peeling would be beneficial
    bool shouldPeelLoop(ForStmt* loop);
    
    // Check if the loop has a known trip count
    bool hasKnownTripCount(ForStmt* loop, int64_t& start, int64_t& end, int64_t& step);
    
    // Create a peeled iteration (substitute loop var with constant)
    StmtPtr createPeeledIteration(Statement* body, const std::string& loopVar, int64_t value);
    
    // Substitute a variable with a constant in an expression
    ExprPtr substituteVar(Expression* expr, const std::string& var, int64_t value);
    
    // Substitute a variable with a constant in a statement
    StmtPtr substituteVarInStmt(Statement* stmt, const std::string& var, int64_t value);
    
    // Clone a statement
    StmtPtr cloneStatement(Statement* stmt);
    
    // Clone an expression
    ExprPtr cloneExpression(Expression* expr);
    
    // Evaluate a constant expression
    bool evaluateConstant(Expression* expr, int64_t& value);
    
    // Check if expression uses the loop variable
    bool usesLoopVar(Expression* expr, const std::string& var);
    
    // Check if statement contains array index with loop variable
    bool containsIndexWithVar(Statement* stmt, const std::string& var);
    
    // Check if expression contains array index with loop variable
    bool containsIndexExprWithVar(Expression* expr, const std::string& var);
};

} // namespace tyl

#endif // TYL_LOOP_PEELING_H
