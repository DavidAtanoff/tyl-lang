// Tyl Compiler - Loop Optimizer
// Loop unrolling, loop invariant code motion (LICM), and strength reduction
#ifndef TYL_LOOP_OPTIMIZER_H
#define TYL_LOOP_OPTIMIZER_H

#include "optimizer.h"
#include "frontend/ast/ast.h"
#include <set>
#include <map>
#include <vector>

namespace tyl {

// Information about a loop for optimization
struct LoopInfo {
    ForStmt* loop = nullptr;
    WhileStmt* whileLoop = nullptr;
    std::string inductionVar;
    int64_t startValue = 0;
    int64_t endValue = 0;
    int64_t stepValue = 1;
    bool boundsKnown = false;
    int64_t tripCount = 0;
    bool isInclusive = false;  // True for RangeExpr (..), false for range() function
    
    // Variables modified in the loop
    std::set<std::string> modifiedVars;
    // Variables read in the loop
    std::set<std::string> readVars;
    // Invariant expressions (can be hoisted)
    std::vector<Statement*> invariantStmts;
    // Is the loop vectorizable?
    bool isVectorizable = false;
};

// Loop Unrolling Pass
// Unrolls small loops with known trip counts to reduce loop overhead
class LoopUnrollingPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "LoopUnrolling"; }
    
    // Configuration
    void setUnrollFactor(int factor) { unrollFactor_ = factor; }
    void setMaxTripCount(int max) { maxTripCount_ = max; }
    void setMinTripCount(int min) { minTripCount_ = min; }
    
private:
    int unrollFactor_ = 4;      // Default unroll factor
    int maxTripCount_ = 64;     // Don't unroll loops with more iterations
    int minTripCount_ = 4;      // Don't unroll loops with fewer iterations
    
    // Analyze a for loop
    bool analyzeLoop(ForStmt* loop, LoopInfo& info);
    
    // Unroll a loop
    StmtPtr unrollLoop(ForStmt* loop, const LoopInfo& info);
    
    // Clone a statement, replacing induction variable references
    StmtPtr cloneStatement(Statement* stmt, const std::string& inductionVar, int64_t offset);
    ExprPtr cloneExpression(Expression* expr, const std::string& inductionVar, int64_t offset);
    
    // Process statements recursively
    void processStatements(std::vector<StmtPtr>& stmts);
};

// Loop Invariant Code Motion (LICM) Pass
// Moves computations that don't change inside a loop to outside the loop
class LICMPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "LICM"; }
    
private:
    // Analyze which variables are modified in a loop
    void analyzeModifiedVars(Statement* stmt, std::set<std::string>& modified);
    
    // Check if an expression is loop-invariant
    bool isLoopInvariant(Expression* expr, const std::set<std::string>& modified,
                         const std::string& inductionVar);
    
    // Check if a statement is loop-invariant
    bool isStatementInvariant(Statement* stmt, const std::set<std::string>& modified,
                              const std::string& inductionVar);
    
    // Hoist invariant code out of a loop
    void hoistInvariantCode(ForStmt* loop, std::vector<StmtPtr>& hoisted);
    void hoistInvariantCode(WhileStmt* loop, std::vector<StmtPtr>& hoisted);
    
    // Process statements recursively
    void processStatements(std::vector<StmtPtr>& stmts);
};

// Strength Reduction Pass
// Replaces expensive operations with cheaper ones (e.g., multiply -> shift)
class StrengthReductionPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "StrengthReduction"; }
    
private:
    // Check if a value is a power of 2
    bool isPowerOf2(int64_t value);
    int log2(int64_t value);
    
    // Transform expressions
    ExprPtr transformExpression(Expression* expr);
    
    // Process statements recursively
    void processStatements(std::vector<StmtPtr>& stmts);
    void processStatement(Statement* stmt);
};

// Combined Loop Optimization Pass
class LoopOptimizationPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "LoopOptimization"; }
    
    // Enable/disable individual optimizations
    void enableUnrolling(bool enable) { unrollingEnabled_ = enable; }
    void enableLICM(bool enable) { licmEnabled_ = enable; }
    void enableStrengthReduction(bool enable) { strengthReductionEnabled_ = enable; }
    
private:
    bool unrollingEnabled_ = true;
    bool licmEnabled_ = true;
    bool strengthReductionEnabled_ = true;
};

} // namespace tyl

#endif // TYL_LOOP_OPTIMIZER_H
