// Tyl Compiler - Jump Threading Optimization Pass
// Threads jumps through blocks with predictable conditions
// Example: if block A jumps to B, and B's condition is known from A, skip B
#ifndef TYL_JUMP_THREADING_H
#define TYL_JUMP_THREADING_H

#include "../optimizer.h"
#include "frontend/ast/ast.h"
#include <set>
#include <map>
#include <vector>

namespace tyl {

// Statistics for Jump Threading transformations
struct JumpThreadingStats {
    int jumpsThreaded = 0;
    int conditionsFolded = 0;
    int blocksEliminated = 0;
    int phiNodesSimplified = 0;
};

// Represents a known value for a variable at a specific point
struct KnownValue {
    std::string varName;
    bool isConstant = false;
    int64_t intValue = 0;
    bool boolValue = false;
    
    enum class Type { Unknown, Integer, Boolean };
    Type type = Type::Unknown;
};

// Jump Threading Pass
// Threads control flow through blocks where the branch condition can be
// determined from the incoming edge
class JumpThreadingPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "JumpThreading"; }
    
    // Get detailed statistics
    const JumpThreadingStats& stats() const { return stats_; }
    
private:
    JumpThreadingStats stats_;
    
    // Map of known values at each point (variable -> value)
    std::map<std::string, KnownValue> knownValues_;
    
    // Process a function
    void processFunction(FnDecl* fn);
    
    // Process a block of statements
    void processBlock(std::vector<StmtPtr>& stmts);
    
    // Process a single statement
    bool processStatement(StmtPtr& stmt, std::vector<StmtPtr>& insertBefore);
    
    // === Jump Threading Core ===
    
    // Try to thread a jump through an if statement
    // Returns true if threading was performed
    bool tryThreadJump(IfStmt* ifStmt, std::vector<StmtPtr>& stmts, size_t index);
    
    // Check if we can determine the condition value from known values
    bool canDetermineCondition(Expression* cond, bool& result);
    
    // Evaluate a condition with known values
    bool evaluateCondition(Expression* cond, bool& result);
    
    // === Value Tracking ===
    
    // Record a known value from an assignment or condition
    void recordKnownValue(const std::string& var, const KnownValue& value);
    
    // Get known value for a variable (if any)
    bool getKnownValue(const std::string& var, KnownValue& value);
    
    // Clear known values (at control flow merge points)
    void clearKnownValues();
    
    // Record values implied by a condition being true/false
    void recordImpliedValues(Expression* cond, bool condValue);
    
    // === Condition Analysis ===
    
    // Check if condition is a simple comparison (var == const, var < const, etc.)
    bool isSimpleComparison(Expression* cond, std::string& var, 
                            TokenType& op, int64_t& value);
    
    // Check if condition is a boolean variable
    bool isBooleanVar(Expression* cond, std::string& var);
    
    // Check if two conditions are equivalent
    bool conditionsEquivalent(Expression* a, Expression* b);
    
    // Check if condition A implies condition B
    bool conditionImplies(Expression* a, Expression* b, bool aValue);
    
    // === Transformation Helpers ===
    
    // Clone a statement
    StmtPtr cloneStatement(Statement* stmt);
    
    // Clone an expression
    ExprPtr cloneExpression(Expression* expr);
    
    // Simplify a condition given known values
    ExprPtr simplifyCondition(Expression* cond);
};

} // namespace tyl

#endif // TYL_JUMP_THREADING_H
