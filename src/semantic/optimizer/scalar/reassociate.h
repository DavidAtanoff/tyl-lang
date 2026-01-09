// Tyl Compiler - Reassociate Pass
// Reorders commutative/associative operations to expose more constant folding and CSE opportunities
// Example: (a + 1) + 2 → a + 3, (a * b) * a → (a * a) * b
#ifndef TYL_REASSOCIATE_H
#define TYL_REASSOCIATE_H

#include "optimizer.h"
#include "frontend/ast/ast.h"
#include <vector>
#include <map>
#include <set>

namespace tyl {

// Operand with its rank for sorting
struct RankedOperand {
    ExprPtr expr;
    int rank;
    bool isConstant;
    int64_t constValue;  // Only valid if isConstant
    
    bool operator<(const RankedOperand& other) const {
        // Constants go last (so they can be folded together)
        if (isConstant != other.isConstant) {
            return !isConstant;  // Non-constants first
        }
        // Among non-constants, sort by rank (lower rank first)
        return rank < other.rank;
    }
};

class ReassociatePass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "Reassociate"; }
    
private:
    // Variable rank map (computed per function)
    std::map<std::string, int> varRanks_;
    int nextRank_ = 0;
    
    // Process statements recursively
    void processStatements(std::vector<StmtPtr>& stmts);
    void processStatement(StmtPtr& stmt);
    
    // Process and potentially reassociate an expression
    ExprPtr processExpression(ExprPtr& expr);
    
    // Check if operator is commutative and associative
    bool isReassociable(TokenType op);
    
    // Linearize expression tree for commutative ops
    // Collects all operands of the same associative operation
    void linearize(Expression* expr, TokenType op, std::vector<RankedOperand>& operands);
    
    // Compute rank for operand ordering
    // Lower rank = closer to leaves, should be computed first
    int computeRank(Expression* expr);
    
    // Fold constants in operand list
    // Returns true if any constants were folded
    bool foldConstants(std::vector<RankedOperand>& operands, TokenType op);
    
    // Rebuild balanced tree from sorted operands
    ExprPtr rebuildTree(std::vector<RankedOperand>& operands, TokenType op, const SourceLocation& loc);
    
    // Clone an expression
    ExprPtr cloneExpr(Expression* expr);
    
    // Check if expression is a constant
    bool isConstant(Expression* expr);
    bool isConstant(Expression* expr, int64_t& value);
    
    // Evaluate constant binary operation
    int64_t evalConstant(TokenType op, int64_t left, int64_t right);
    
    // Assign ranks to variables in a function
    void assignRanks(Statement* stmt);
};

std::unique_ptr<ReassociatePass> createReassociatePass();

} // namespace tyl

#endif // TYL_REASSOCIATE_H
