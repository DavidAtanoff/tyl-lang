// Tyl Compiler - Constraint Elimination
// Uses constraint solving to eliminate redundant checks
#ifndef TYL_CONSTRAINT_ELIMINATION_H
#define TYL_CONSTRAINT_ELIMINATION_H

#include "optimizer.h"
#include "frontend/ast/ast.h"
#include <set>
#include <map>
#include <vector>
#include <optional>
#include <cstdint>

namespace tyl {

// Represents a linear constraint: c0 + c1*v1 + c2*v2 + ... <= 0
// or c0 + c1*v1 + c2*v2 + ... < 0
struct LinearConstraint {
    std::map<std::string, int64_t> coefficients;  // Variable -> coefficient
    int64_t constant = 0;                          // Constant term
    bool isStrict = false;                         // < vs <=
    
    LinearConstraint() = default;
    LinearConstraint(int64_t c) : constant(c) {}
    
    // Add a term: coeff * var
    void addTerm(const std::string& var, int64_t coeff);
    
    // Negate the constraint
    LinearConstraint negate() const;
    
    // Check if constraint is trivially true (e.g., -5 <= 0)
    std::optional<bool> isTriviallyTrue() const;
    
    // Check if constraint is trivially false (e.g., 5 <= 0)
    std::optional<bool> isTriviallyFalse() const;
    
    // Substitute known values
    LinearConstraint substitute(const std::map<std::string, int64_t>& values) const;
};

// Simple constraint system for tracking and checking constraints
class ConstraintSystem {
public:
    // Add a constraint to the system
    void addConstraint(const LinearConstraint& constraint);
    
    // Check if a constraint is implied by the current system
    std::optional<bool> isImplied(const LinearConstraint& constraint) const;
    
    // Check if a constraint's negation is implied (constraint is false)
    std::optional<bool> isContradicted(const LinearConstraint& constraint) const;
    
    // Clear all constraints
    void clear();
    
    // Get number of constraints
    size_t size() const { return constraints_.size(); }
    
    // Push/pop constraint scope
    void pushScope();
    void popScope();
    
private:
    std::vector<LinearConstraint> constraints_;
    std::vector<size_t> scopeStack_;  // Stack of constraint counts
    
    // Simple implication check using transitivity
    bool checkImplication(const LinearConstraint& constraint) const;
};

// Fact or check entry for worklist processing
struct ConstraintFactOrCheck {
    enum class Type {
        Fact,   // A constraint that holds at this point
        Check   // A condition to try to simplify
    };
    
    Type type;
    LinearConstraint constraint;
    Expression* expr = nullptr;      // For checks: the expression to simplify
    Statement* stmt = nullptr;       // For facts: the statement that establishes the fact
    unsigned domIn = 0;              // Dominator tree DFS in number
    unsigned domOut = 0;             // Dominator tree DFS out number
    
    static ConstraintFactOrCheck makeFact(const LinearConstraint& c, unsigned in, unsigned out) {
        ConstraintFactOrCheck f;
        f.type = Type::Fact;
        f.constraint = c;
        f.domIn = in;
        f.domOut = out;
        return f;
    }
    
    static ConstraintFactOrCheck makeCheck(Expression* e, unsigned in, unsigned out) {
        ConstraintFactOrCheck c;
        c.type = Type::Check;
        c.expr = e;
        c.domIn = in;
        c.domOut = out;
        return c;
    }
};

// Constraint Elimination Pass
// Collects constraints from dominating conditions and uses them to:
// 1. Eliminate redundant checks (bounds checks, null checks, etc.)
// 2. Simplify comparisons that are always true/false
// 3. Remove dead branches
class ConstraintEliminationPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "ConstraintElimination"; }
    
private:
    ConstraintSystem constraintSystem_;
    
    // Worklist of facts and checks
    std::vector<ConstraintFactOrCheck> worklist_;
    
    // DFS numbering for dominance
    unsigned currentDFSIn_ = 0;
    unsigned currentDFSOut_ = 0;
    
    // Process a function
    void processFunction(FnDecl* fn);
    
    // Build worklist by traversing the AST
    void buildWorklist(std::vector<StmtPtr>& stmts);
    void visitStatement(Statement* stmt);
    void visitExpression(Expression* expr);
    
    // Process the worklist
    void processWorklist(std::vector<StmtPtr>& stmts);
    
    // Try to simplify a check using current constraints
    ExprPtr trySimplifyCheck(Expression* expr);
    
    // Convert a comparison to a linear constraint
    std::optional<LinearConstraint> toConstraint(Expression* expr);
    std::optional<LinearConstraint> toConstraint(BinaryExpr* cmp);
    
    // Extract constraints from a condition (for facts)
    std::vector<LinearConstraint> extractFacts(Expression* cond, bool negate = false);
    
    // Apply transformations to statements
    void transformStatements(std::vector<StmtPtr>& stmts);
    
    // Helper: decompose expression into linear form
    bool decompose(Expression* expr, std::map<std::string, int64_t>& coeffs, int64_t& constant);
    
    // Helper: get variable name
    std::optional<std::string> getVariableName(Expression* expr);
    
    // Helper: evaluate constant
    std::optional<int64_t> evaluateConstant(Expression* expr);
};

} // namespace tyl

#endif // TYL_CONSTRAINT_ELIMINATION_H
