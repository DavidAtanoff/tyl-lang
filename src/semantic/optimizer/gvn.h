// Tyl Compiler - Global Value Numbering (GVN)
// Assigns unique numbers to values and eliminates redundant computations
#ifndef TYL_GVN_H
#define TYL_GVN_H

#include "optimizer.h"
#include "frontend/ast/ast.h"
#include <map>
#include <set>
#include <vector>
#include <string>

namespace tyl {

// Value number representation
using ValueNumber = uint32_t;
constexpr ValueNumber INVALID_VN = 0;

// Expression key for value numbering
struct VNKey {
    TokenType op;
    ValueNumber left;
    ValueNumber right;
    std::string literal;  // For constants
    
    bool operator<(const VNKey& other) const {
        if (op != other.op) return op < other.op;
        if (left != other.left) return left < other.left;
        if (right != other.right) return right < other.right;
        return literal < other.literal;
    }
};

class GVNPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "GVN"; }
    
private:
    // Value number counter
    ValueNumber nextVN_ = 1;
    
    // Map expression key -> value number
    std::map<VNKey, ValueNumber> exprToVN_;
    
    // Map variable name -> current value number
    std::map<std::string, ValueNumber> varToVN_;
    
    // Map value number -> canonical expression (for replacement)
    std::map<ValueNumber, ExprPtr> vnToExpr_;
    
    // Map value number -> known constant value (if any)
    std::map<ValueNumber, int64_t> vnToConst_;
    
    // Get or create value number for an expression
    ValueNumber getValueNumber(Expression* expr);
    
    // Create a key for an expression
    VNKey makeKey(Expression* expr);
    
    // Process statements
    void processBlock(std::vector<StmtPtr>& statements);
    void processStatement(StmtPtr& stmt);
    
    // Process expressions - returns replacement if optimized
    ExprPtr processExpression(ExprPtr& expr);
    
    // Reset state for new scope
    void resetState();
    
    // Invalidate a variable's value number
    void invalidateVar(const std::string& name);
};

// Copy Propagation Pass
// Replaces uses of variables with their values when possible
class CopyPropagationPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "CopyPropagation"; }
    
private:
    // Map variable -> source variable (for copies like x = y)
    std::map<std::string, std::string> copies_;
    
    // Map variable -> constant value
    std::map<std::string, int64_t> constants_;
    
    // Set of modified variables
    std::set<std::string> modified_;
    
    // Process statements
    void processBlock(std::vector<StmtPtr>& statements);
    void processStatement(StmtPtr& stmt);
    
    // Process expressions - returns replacement if propagated
    ExprPtr processExpression(ExprPtr& expr);
    
    // Get the ultimate source of a copy chain
    std::string getUltimateSource(const std::string& var);
    
    // Invalidate copies involving a variable
    void invalidateCopies(const std::string& var);
};

} // namespace tyl

#endif // TYL_GVN_H
