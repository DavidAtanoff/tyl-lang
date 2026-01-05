// Tyl Compiler - Algebraic Simplification and Advanced Strength Reduction
// Applies algebraic identities and replaces expensive operations with cheaper ones
#ifndef TYL_ALGEBRAIC_H
#define TYL_ALGEBRAIC_H

#include "optimizer.h"
#include "frontend/ast/ast.h"

namespace tyl {

class AlgebraicSimplificationPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "AlgebraicSimplification"; }
    
private:
    // Process statements
    void processBlock(std::vector<StmtPtr>& statements);
    void processStatement(StmtPtr& stmt);
    
    // Process expressions - returns replacement if simplified
    ExprPtr simplifyExpression(ExprPtr& expr);
    
    // Specific simplification rules
    ExprPtr simplifyBinary(BinaryExpr* binary);
    ExprPtr simplifyUnary(UnaryExpr* unary);
    
    // Algebraic identity checks
    bool isZero(Expression* expr);
    bool isOne(Expression* expr);
    bool isNegOne(Expression* expr);
    bool isPowerOfTwo(Expression* expr, int& power);
    
    // Clone an expression
    ExprPtr cloneExpr(Expression* expr);
    
    // Create shift expression (for strength reduction)
    ExprPtr createShift(ExprPtr base, int shiftAmount, bool isLeft, const SourceLocation& loc);
};

// Advanced Strength Reduction Pass
// Converts expensive operations to cheaper equivalents
class AdvancedStrengthReductionPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "AdvancedStrengthReduction"; }
    
private:
    // Process statements
    void processBlock(std::vector<StmtPtr>& statements);
    void processStatement(StmtPtr& stmt);
    
    // Process expressions
    ExprPtr reduceExpression(ExprPtr& expr);
    
    // Specific reductions
    ExprPtr reduceMultiply(BinaryExpr* binary);
    ExprPtr reduceDivide(BinaryExpr* binary);
    ExprPtr reduceModulo(BinaryExpr* binary);
    
    // Helper functions
    bool isPowerOfTwo(int64_t value);
    int log2(int64_t value);
    ExprPtr cloneExpr(Expression* expr);
};

} // namespace tyl

#endif // TYL_ALGEBRAIC_H
