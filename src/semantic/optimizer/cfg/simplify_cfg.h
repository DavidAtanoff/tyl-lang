// Tyl Compiler - SimplifyCFG Optimization Pass
// Simplifies control flow graph: merges blocks, removes dead branches,
// hoists/sinks common code, converts if-chains to switches
#ifndef TYL_SIMPLIFY_CFG_H
#define TYL_SIMPLIFY_CFG_H

#include "../optimizer.h"
#include "frontend/ast/ast.h"
#include <set>
#include <map>
#include <vector>

namespace tyl {

// Statistics for SimplifyCFG transformations
struct SimplifyCFGStats {
    int constantConditionsSimplified = 0;
    int emptyBlocksRemoved = 0;
    int unreachableCodeRemoved = 0;
    int commonCodeHoisted = 0;
    int commonCodeSunk = 0;
    int ifChainsToSwitch = 0;
    int nestedIfsFlattened = 0;
    int redundantBranchesRemoved = 0;
};

class SimplifyCFGPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "SimplifyCFG"; }
    
    // Get detailed statistics
    const SimplifyCFGStats& stats() const { return stats_; }
    
private:
    SimplifyCFGStats stats_;
    
    // Process a function
    void processFunction(FnDecl* fn);
    
    // Process a block of statements
    void processBlock(std::vector<StmtPtr>& stmts);
    
    // Process a single statement (may modify or replace it)
    // Returns true if statement was modified
    bool processStatement(StmtPtr& stmt);
    
    // === Constant Condition Simplification ===
    // if (true) A else B → A
    // if (false) A else B → B
    // while (false) { ... } → remove
    bool simplifyConstantCondition(StmtPtr& stmt);
    
    // Check if expression is a constant boolean
    bool isConstantBool(Expression* expr, bool& value);
    
    // === Unreachable Code Removal ===
    // Remove code after return/break/continue
    bool removeUnreachableCode(std::vector<StmtPtr>& stmts);
    
    // Check if statement always terminates (return/break/continue)
    bool alwaysTerminates(Statement* stmt);
    
    // === Empty Block Removal ===
    // if (cond) { } → remove (if no else)
    // if (cond) { } else { B } → if (!cond) { B }
    bool removeEmptyBlocks(StmtPtr& stmt);
    
    // Check if a block is empty or contains only empty statements
    bool isEmptyBlock(const std::vector<StmtPtr>& stmts);
    bool isEmptyStatement(Statement* stmt);
    
    // === Common Code Hoisting ===
    // if (c) { A; X } else { A; Y } → A; if (c) { X } else { Y }
    bool hoistCommonCode(IfStmt* ifStmt);
    
    // === Common Code Sinking ===
    // if (c) { X; A } else { Y; A } → if (c) { X } else { Y }; A
    bool sinkCommonCode(IfStmt* ifStmt);
    
    // Check if two statements are equivalent
    bool statementsEqual(Statement* a, Statement* b);
    bool expressionsEqual(Expression* a, Expression* b);
    
    // === Nested If Flattening ===
    // if (a) { if (b) { X } } → if (a && b) { X }
    bool flattenNestedIfs(StmtPtr& stmt);
    
    // === If-Chain to Switch Conversion ===
    // if (x == 1) A else if (x == 2) B else if (x == 3) C
    // → match x { 1 => A, 2 => B, 3 => C }
    bool convertIfChainToSwitch(StmtPtr& stmt);
    
    // Analyze if-chain for switch conversion
    struct IfChainCase {
        int64_t value;
        StmtPtr body;
    };
    bool analyzeIfChain(IfStmt* ifStmt, std::string& switchVar, 
                        std::vector<IfChainCase>& cases,
                        std::vector<StmtPtr>& defaultBody);
    
    // === Redundant Branch Removal ===
    // if (c) { goto L } L: ... → remove if
    // if (c) { X } else { X } → X
    bool removeRedundantBranches(StmtPtr& stmt);
    
    // === Utility Functions ===
    // Clone a statement
    StmtPtr cloneStatement(Statement* stmt);
    ExprPtr cloneExpression(Expression* expr);
    
    // Create negated condition
    ExprPtr negateCondition(Expression* cond);
    
    // Create AND of two conditions
    ExprPtr createAnd(ExprPtr left, ExprPtr right);
};

} // namespace tyl

#endif // TYL_SIMPLIFY_CFG_H
