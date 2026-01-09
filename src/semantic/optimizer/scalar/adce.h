// Tyl Compiler - Aggressive Dead Code Elimination (ADCE)
// Uses reverse dataflow analysis to find truly live code
#ifndef TYL_ADCE_H
#define TYL_ADCE_H

#include "optimizer.h"
#include "frontend/ast/ast.h"
#include <set>
#include <map>
#include <vector>
#include <queue>

namespace tyl {

// Liveness information for a statement
struct LivenessInfo {
    std::set<std::string> liveIn;   // Variables live at entry
    std::set<std::string> liveOut;  // Variables live at exit
    std::set<std::string> def;      // Variables defined
    std::set<std::string> use;      // Variables used
    bool isLive = false;            // Is this statement live?
    bool hasSideEffects = false;    // Does this have observable effects?
};

// Aggressive Dead Code Elimination Pass
// Uses liveness analysis to remove code that doesn't contribute to output
class ADCEPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "ADCE"; }
    
private:
    // Liveness info for each statement (indexed by pointer)
    std::map<Statement*, LivenessInfo> livenessInfo_;
    
    // Set of live statements
    std::set<Statement*> liveStatements_;
    
    // Worklist for propagation
    std::queue<Statement*> worklist_;
    
    // Phase 1: Compute def/use sets for each statement
    void computeDefUse(Statement* stmt, LivenessInfo& info);
    void computeDefUseExpr(Expression* expr, std::set<std::string>& uses);
    void computeDefUseRecursive(std::vector<StmtPtr>& stmts);  // Recurse into nested blocks
    
    // Phase 2: Mark initially live statements (side effects, returns)
    void markInitiallyLive(std::vector<StmtPtr>& stmts);
    bool hasSideEffects(Statement* stmt);
    bool exprHasSideEffects(Expression* expr);
    bool containsFunctionCall(Expression* expr);  // Helper to check for any function call
    
    // Phase 3: Propagate liveness backwards
    void propagateLiveness(std::vector<StmtPtr>& stmts);
    void markLive(Statement* stmt);
    
    // Phase 4: Remove dead statements
    void removeDeadStatements(std::vector<StmtPtr>& stmts);
    
    // Process nested structures
    void processFunction(FnDecl* fn);
    void processBlock(std::vector<StmtPtr>& stmts);
    
    // Find statements that define a variable
    void findDefiningStatements(const std::string& var, 
                                std::vector<StmtPtr>& stmts,
                                std::vector<Statement*>& result);
};

// Enhanced Dead Code Elimination that combines traditional DCE with ADCE
class EnhancedDCEPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "EnhancedDCE"; }
    
private:
    // Run traditional DCE first
    void runTraditionalDCE(Program& ast);
    
    // Then run ADCE for more aggressive elimination
    void runADCE(Program& ast);
};

} // namespace tyl

#endif // TYL_ADCE_H
