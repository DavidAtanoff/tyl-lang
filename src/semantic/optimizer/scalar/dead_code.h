// Tyl Compiler - Dead Code Elimination Pass
// Removes unreachable code and unused declarations
#ifndef TYL_DEAD_CODE_H
#define TYL_DEAD_CODE_H

#include "optimizer.h"
#include "frontend/ast/ast.h"
#include <unordered_set>
#include <unordered_map>
#include <queue>

namespace tyl {

class DeadCodeEliminationPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "DeadCodeElimination"; }
    
private:
    // Track which identifiers are used
    std::unordered_set<std::string> usedIdentifiers_;
    
    // Track which functions are called
    std::unordered_set<std::string> calledFunctions_;
    
    // Track which functions are reachable from entry point (tree shaking)
    std::unordered_set<std::string> reachableFunctions_;
    
    // Map function names to their declarations for call graph analysis
    std::unordered_map<std::string, FnDecl*> functionDecls_;
    
    // Track variable declarations for potential removal
    std::unordered_map<std::string, bool> declaredVariables_;
    
    // First pass: collect all used identifiers
    void collectUsedIdentifiers(Program& ast);
    void collectFromStatement(Statement* stmt);
    void collectFromExpression(Expression* expr);
    
    // Tree shaking: build call graph and find reachable functions
    void buildCallGraph(Program& ast);
    void collectCallsFromStatement(Statement* stmt, std::unordered_set<std::string>& calls);
    void collectCallsFromExpression(Expression* expr, std::unordered_set<std::string>& calls);
    void computeReachableFunctions(Program& ast);
    
    // Second pass: remove dead code
    void removeDeadCode(Program& ast);
    void removeDeadFromBlock(std::vector<StmtPtr>& statements);
    
    // Check if a statement is dead (unreachable after return/break/continue)
    bool isDeadAfterTerminator(std::vector<StmtPtr>& statements);
    
    // Check if a variable is used
    bool isVariableUsed(const std::string& name);
    
    // Check if a function is called (or is main/entry point)
    bool isFunctionUsed(const std::string& name);
    
    // Check if a function is reachable from entry point
    bool isFunctionReachable(const std::string& name);
    
    // Remove code after unconditional return/break/continue
    void removeUnreachableCode(std::vector<StmtPtr>& statements);
    
    // Simplify if statements with constant conditions
    void simplifyConstantConditions(std::vector<StmtPtr>& statements);
    
    // Remove unused function declarations
    void removeUnusedFunctions(std::vector<StmtPtr>& statements);
    
    // Remove unused variable declarations
    void removeUnusedVariables(std::vector<StmtPtr>& statements);
    
    // Check if expression has side effects
    bool hasSideEffects(Expression* expr);
};

} // namespace tyl

#endif // TYL_DEAD_CODE_H
