// Tyl Compiler - Common Subexpression Elimination (CSE)
// Identifies and eliminates redundant computations
#ifndef TYL_CSE_H
#define TYL_CSE_H

#include "optimizer.h"
#include "frontend/ast/ast.h"
#include <map>
#include <set>
#include <string>
#include <functional>

namespace tyl {

// Hash for expressions to identify common subexpressions
struct ExprHash {
    std::string hash;
    Expression* expr = nullptr;
    std::string tempVar;  // Temporary variable holding the result
};

class CSEPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "CSE"; }
    
private:
    // Map of expression hash -> temp variable name
    std::map<std::string, std::string> exprToTemp_;
    
    // Set of variables that have been modified (invalidates expressions using them)
    std::set<std::string> modifiedVars_;
    
    // Counter for generating temp variable names
    int tempCounter_ = 0;
    
    // Compute a hash string for an expression
    std::string hashExpression(Expression* expr);
    
    // Check if an expression is eligible for CSE
    bool isCSECandidate(Expression* expr);
    
    // Check if expression uses any modified variables
    bool usesModifiedVar(Expression* expr);
    
    // Get all variables used in an expression
    void collectUsedVars(Expression* expr, std::set<std::string>& vars);
    
    // Process statements
    void processBlock(std::vector<StmtPtr>& statements);
    void processStatement(StmtPtr& stmt);
    
    // Process expressions - returns replacement if CSE applied
    ExprPtr processExpression(ExprPtr& expr, std::vector<StmtPtr>& insertBefore);
    
    // Generate a new temp variable name
    std::string newTempVar();
    
    // Clear CSE state (for new scope)
    void clearState();
    
    // Invalidate expressions using a variable
    void invalidateVar(const std::string& varName);
};

} // namespace tyl

#endif // TYL_CSE_H
