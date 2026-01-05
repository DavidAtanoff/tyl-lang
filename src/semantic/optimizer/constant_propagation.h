// Tyl Compiler - Constant Propagation Optimization Pass
// Tracks variable values through the program and eliminates redundant comparisons
#ifndef TYL_CONSTANT_PROPAGATION_H
#define TYL_CONSTANT_PROPAGATION_H

#include "optimizer.h"
#include "frontend/ast/ast.h"
#include <map>
#include <optional>
#include <variant>
#include <set>

namespace tyl {

// Value that can be tracked for a variable
using PropValue = std::variant<std::monostate, int64_t, double, bool, std::string>;

class ConstantPropagationPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "ConstantPropagation"; }
    
private:
    // Map of variable name -> known constant value
    std::map<std::string, PropValue> knownValues_;
    
    // Set of variables that have been modified (invalidated)
    std::set<std::string> modifiedVars_;
    
    // Track mutable variable values for accumulator optimization
    // This tracks values even for mutable variables in sequential code
    std::map<std::string, PropValue> mutableValues_;
    
    // Try to get the known value of an expression
    std::optional<PropValue> tryGetValue(Expression* expr);
    
    // Try to evaluate a binary expression with known values
    std::optional<PropValue> evalBinary(TokenType op, const PropValue& left, const PropValue& right);
    
    // Create a literal from a propagated value
    ExprPtr createLiteral(const PropValue& value, const SourceLocation& loc);
    
    // Process statements - returns true if statement should be removed
    bool processStatement(StmtPtr& stmt);
    void processBlock(std::vector<StmtPtr>& statements);
    
    // Propagate through expressions - returns replacement if optimized
    ExprPtr propagateExpression(ExprPtr& expr);
    
    // Check if a condition is always true/false
    std::optional<bool> evaluateCondition(Expression* cond);
    
    // Invalidate variables modified in a block (for loops)
    void invalidateModifiedVars(Statement* stmt);
    
    // Optimize accumulator patterns (e.g., x = x + 1 repeated N times -> x = N)
    void optimizeAccumulators(std::vector<StmtPtr>& statements);
    
    // Dead Store Elimination - remove assignments that are overwritten before being read
    void eliminateDeadStores(std::vector<StmtPtr>& statements);
};

} // namespace tyl

#endif // TYL_CONSTANT_PROPAGATION_H
