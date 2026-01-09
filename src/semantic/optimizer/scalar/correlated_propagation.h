// Tyl Compiler - Correlated Value Propagation
// Uses range analysis to simplify comparisons and eliminate branches
#ifndef TYL_CORRELATED_PROPAGATION_H
#define TYL_CORRELATED_PROPAGATION_H

#include "optimizer.h"
#include "frontend/ast/ast.h"
#include <set>
#include <map>
#include <vector>
#include <optional>
#include <cstdint>

namespace tyl {

// Represents a range of values [min, max]
struct ValueRange {
    int64_t min = INT64_MIN;
    int64_t max = INT64_MAX;
    bool isUnknown = true;
    
    ValueRange() = default;
    ValueRange(int64_t val) : min(val), max(val), isUnknown(false) {}
    ValueRange(int64_t lo, int64_t hi) : min(lo), max(hi), isUnknown(false) {}
    
    bool isConstant() const { return !isUnknown && min == max; }
    bool isEmpty() const { return !isUnknown && min > max; }
    bool contains(int64_t val) const { return !isUnknown && val >= min && val <= max; }
    bool isNonNegative() const { return !isUnknown && min >= 0; }
    bool isNonPositive() const { return !isUnknown && max <= 0; }
    bool isPositive() const { return !isUnknown && min > 0; }
    bool isNegative() const { return !isUnknown && max < 0; }
    
    // Range operations
    ValueRange intersect(const ValueRange& other) const;
    ValueRange unionWith(const ValueRange& other) const;
    
    // Arithmetic operations on ranges
    ValueRange add(const ValueRange& other) const;
    ValueRange sub(const ValueRange& other) const;
    ValueRange mul(const ValueRange& other) const;
    
    // Comparison result (true, false, or unknown)
    std::optional<bool> compareWith(const ValueRange& other, TokenType op) const;
    
    static ValueRange unknown() { return ValueRange(); }
    static ValueRange constant(int64_t val) { return ValueRange(val); }
    static ValueRange nonNegative() { return ValueRange(0, INT64_MAX); }
    static ValueRange nonPositive() { return ValueRange(INT64_MIN, 0); }
    static ValueRange positive() { return ValueRange(1, INT64_MAX); }
    static ValueRange negative() { return ValueRange(INT64_MIN, -1); }
};

// Constraint on a variable (e.g., x > 0, x <= 10)
struct ValueConstraint {
    std::string variable;
    TokenType op;           // Comparison operator
    int64_t value;          // Constant value being compared to
    
    ValueConstraint(const std::string& var, TokenType o, int64_t val)
        : variable(var), op(o), value(val) {}
    
    // Convert constraint to a range
    ValueRange toRange() const;
    
    // Check if constraint is satisfied by a range
    std::optional<bool> isSatisfiedBy(const ValueRange& range) const;
};

// Correlated Value Propagation Pass
// Tracks value ranges through control flow and uses them to:
// 1. Simplify comparisons that are always true/false
// 2. Convert signed operations to unsigned when values are non-negative
// 3. Narrow division/remainder operations
// 4. Remove redundant checks
class CorrelatedValuePropagationPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "CorrelatedValuePropagation"; }
    
private:
    // Value ranges for each variable at current program point
    std::map<std::string, ValueRange> valueRanges_;
    
    // Stack of range contexts for nested scopes
    std::vector<std::map<std::string, ValueRange>> rangeStack_;
    
    // Process a function
    void processFunction(FnDecl* fn);
    
    // Process statements with range tracking
    void processStatements(std::vector<StmtPtr>& stmts);
    void processStatement(StmtPtr& stmt);
    
    // Process expressions and return their range
    ValueRange processExpression(Expression* expr);
    
    // Extract constraints from a condition
    std::vector<ValueConstraint> extractConstraints(Expression* cond, bool negate = false);
    
    // Apply constraints to current range context
    void applyConstraints(const std::vector<ValueConstraint>& constraints);
    
    // Try to simplify a comparison using known ranges
    ExprPtr simplifyComparison(BinaryExpr* cmp);
    
    // Try to convert signed to unsigned operations
    ExprPtr convertSignedToUnsigned(BinaryExpr* expr);
    
    // Try to narrow division/remainder
    ExprPtr narrowDivRem(BinaryExpr* expr);
    
    // Scope management
    void pushScope();
    void popScope();
    
    // Get range for a variable
    ValueRange getRange(const std::string& var) const;
    
    // Set range for a variable
    void setRange(const std::string& var, const ValueRange& range);
    
    // Merge ranges from two branches
    void mergeRanges(const std::map<std::string, ValueRange>& other);
    
    // Helper: Get variable name from expression
    std::optional<std::string> getVariableName(Expression* expr);
    
    // Helper: Evaluate constant expression
    std::optional<int64_t> evaluateConstant(Expression* expr);
    
    // Helper: Check if expression is a comparison
    bool isComparison(Expression* expr);
};

} // namespace tyl

#endif // TYL_CORRELATED_PROPAGATION_H
