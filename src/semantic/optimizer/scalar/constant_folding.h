// Tyl Compiler - Constant Folding Optimization Pass
// Evaluates constant expressions at compile time
#ifndef TYL_CONSTANT_FOLDING_H
#define TYL_CONSTANT_FOLDING_H

#include "optimizer.h"
#include "frontend/ast/ast.h"
#include <optional>
#include <variant>

namespace tyl {

// Result of constant evaluation
using ConstValue = std::variant<std::monostate, int64_t, double, bool, std::string>;

class ConstantFoldingPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "ConstantFolding"; }
    
private:
    // Try to evaluate an expression to a constant
    std::optional<ConstValue> tryEvaluate(Expression* expr);
    
    // Fold binary operations
    std::optional<ConstValue> foldBinary(TokenType op, const ConstValue& left, const ConstValue& right);
    
    // Fold unary operations
    std::optional<ConstValue> foldUnary(TokenType op, const ConstValue& operand);
    
    // Create a literal node from a constant value
    ExprPtr createLiteral(const ConstValue& value, const SourceLocation& loc);
    
    // Recursive folding on expressions (returns replacement if folded)
    ExprPtr foldExpression(ExprPtr& expr);
    
    // Process statements recursively
    void processStatement(StmtPtr& stmt);
    void processBlock(std::vector<StmtPtr>& statements);
};

} // namespace tyl

#endif // TYL_CONSTANT_FOLDING_H
