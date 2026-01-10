// Tyl Compiler - Induction Variable Simplification Pass
// Canonicalizes induction variables and computes trip counts
// Enables better loop unrolling decisions and strength reduction
// Enhanced with loop closed-form recognition for O3 optimization
#ifndef TYL_INDVAR_SIMPLIFY_H
#define TYL_INDVAR_SIMPLIFY_H

#include "../optimizer.h"
#include "frontend/ast/ast.h"
#include <set>
#include <map>
#include <vector>
#include <optional>

namespace tyl {

// Represents an induction variable
struct InductionVariable {
    std::string name;           // Variable name
    int64_t startValue = 0;     // Initial value
    int64_t stepValue = 1;      // Increment per iteration
    bool startKnown = false;    // Is start value a constant?
    bool stepKnown = false;     // Is step value a constant?
    
    // For derived induction variables (e.g., j = i * 2 + 1)
    bool isDerived = false;
    std::string baseVar;        // Base induction variable
    int64_t scale = 1;          // Multiplier
    int64_t offset = 0;         // Additive offset
};

// Represents loop bounds and trip count
struct LoopBounds {
    int64_t start = 0;
    int64_t end = 0;
    int64_t step = 1;
    bool isInclusive = false;   // true for .., false for range()
    bool boundsKnown = false;
    int64_t tripCount = 0;      // Number of iterations (if known)
    bool tripCountKnown = false;
};

// Closed-form loop pattern types
enum class ClosedFormPattern {
    None,
    TriangularSum,      // sum = 0; for i in 0..n: sum += i  => n*(n-1)/2
    ArithmeticSum,      // sum = 0; for i in 0..n: sum += c  => n*c
    GeometricSum,       // sum = 1; for i in 0..n: sum *= c  => c^n
    SquareSum,          // sum = 0; for i in 0..n: sum += i*i => n*(n-1)*(2n-1)/6
    LinearAccum,        // sum = 0; for i in 0..n: sum += a*i+b => closed form
    CountingLoop,       // count = 0; for i in 0..n: count++ => n
    ConstantAssign      // x = c; for i in 0..n: x = c => c (dead loop)
};

// Information about a closed-form reducible loop
struct ClosedFormInfo {
    ClosedFormPattern pattern = ClosedFormPattern::None;
    std::string accumVar;       // The accumulator variable
    std::string ivName;         // Induction variable name
    int64_t coefficient = 1;    // Coefficient for linear patterns
    int64_t constant = 0;       // Constant term
    int64_t initialValue = 0;   // Initial value of accumulator
    bool canReduce = false;     // Can this loop be reduced to closed form?
};

// Statistics for IndVar Simplification
struct IndVarSimplifyStats {
    int inductionVarsSimplified = 0;
    int tripCountsComputed = 0;
    int derivedIVsEliminated = 0;
    int exitConditionsSimplified = 0;
    int loopExitsOptimized = 0;
    int closedFormReductions = 0;  // Loops replaced with closed-form expressions
};

// Induction Variable Simplification Pass
class IndVarSimplifyPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "IndVarSimplify"; }
    
    // Get detailed statistics
    const IndVarSimplifyStats& stats() const { return stats_; }
    
private:
    IndVarSimplifyStats stats_;
    
    // Current function's induction variables
    std::map<std::string, InductionVariable> inductionVars_;
    
    // Process statements recursively
    void processStatements(std::vector<StmtPtr>& stmts);
    
    // Process a function
    void processFunction(FnDecl* fn);
    
    // === Induction Variable Analysis ===
    
    // Analyze a for loop to find induction variables
    void analyzeForLoop(ForStmt* loop);
    
    // Analyze a while loop to find induction variables
    void analyzeWhileLoop(WhileStmt* loop);
    
    // Find the primary induction variable of a loop
    std::optional<InductionVariable> findPrimaryIV(ForStmt* loop);
    
    // Find derived induction variables in the loop body
    std::vector<InductionVariable> findDerivedIVs(Statement* body, 
                                                   const std::string& primaryIV);
    
    // Check if an expression is an induction variable update
    // e.g., i = i + 1, i += 2, i++
    bool isIVUpdate(Expression* expr, std::string& var, int64_t& step);
    
    // Check if a variable is modified in a statement
    bool isModifiedIn(const std::string& var, Statement* stmt);
    
    // === Trip Count Computation ===
    
    // Compute trip count for a for loop
    LoopBounds computeForLoopBounds(ForStmt* loop);
    
    // Compute trip count for a while loop
    LoopBounds computeWhileLoopBounds(WhileStmt* loop, const InductionVariable& iv);
    
    // Compute trip count from bounds
    int64_t computeTripCount(int64_t start, int64_t end, int64_t step, bool inclusive);
    
    // === Simplification Transformations ===
    
    // Simplify derived induction variables
    // e.g., j = i * 4 can be replaced with direct computation
    bool simplifyDerivedIV(std::vector<StmtPtr>& stmts, const InductionVariable& derived,
                           const InductionVariable& primary);
    
    // Canonicalize the loop exit condition
    // e.g., i * i < 1000 -> i < 32 (if trip count is known)
    bool canonicalizeExitCondition(ForStmt* loop, const LoopBounds& bounds);
    
    // Replace uses of IV outside loop with final value
    bool replaceExitValue(std::vector<StmtPtr>& stmts, size_t loopIndex,
                          const std::string& iv, const LoopBounds& bounds);
    
    // Helper to replace IV uses in a statement
    bool replaceIVUsesInStatement(Statement* stmt, const std::string& iv, int64_t value);
    
    // Helper to replace IV uses in an expression
    bool replaceIVUsesInExpr(ExprPtr& expr, const std::string& iv, int64_t value);
    
    // Widen narrow induction variables to eliminate sign/zero extends
    bool widenInductionVariable(ForStmt* loop, InductionVariable& iv);
    
    // === Expression Analysis ===
    
    // Check if expression is affine in the induction variable
    // i.e., expr = a * iv + b for constants a, b
    bool isAffineInIV(Expression* expr, const std::string& iv,
                      int64_t& scale, int64_t& offset);
    
    // Evaluate a constant expression
    bool evaluateConstant(Expression* expr, int64_t& value);
    
    // === Closed-Form Loop Recognition ===
    
    // Analyze a loop for closed-form reduction patterns
    ClosedFormInfo analyzeClosedForm(ForStmt* loop, const LoopBounds& bounds);
    
    // Check if loop body is a simple accumulation pattern
    bool isSimpleAccumulation(Statement* body, const std::string& iv,
                              std::string& accumVar, TokenType& op,
                              int64_t& coefficient, int64_t& constant);
    
    // Try to reduce a loop to closed-form expression
    bool reduceToClosedForm(std::vector<StmtPtr>& stmts, size_t loopIndex,
                            ForStmt* loop, const ClosedFormInfo& info,
                            const LoopBounds& bounds);
    
    // Compute closed-form value for triangular sum: sum(0..n-1) = n*(n-1)/2
    int64_t computeTriangularSum(int64_t n);
    
    // Compute closed-form value for arithmetic sum: n * constant
    int64_t computeArithmeticSum(int64_t n, int64_t constant);
    
    // Compute closed-form for linear accumulation: sum(a*i + b) for i in 0..n
    int64_t computeLinearAccum(int64_t n, int64_t a, int64_t b, int64_t initial);
    
    // Find accumulator initialization before loop
    bool findAccumulatorInit(std::vector<StmtPtr>& stmts, size_t loopIndex,
                             const std::string& accumVar, int64_t& initValue);
    
    // === Utility Functions ===
    
    // Clone an expression
    ExprPtr cloneExpression(Expression* expr);
    
    // Create an integer literal
    ExprPtr makeIntLiteral(int64_t value, SourceLocation loc);
    
    // Create a comparison expression
    ExprPtr makeComparison(ExprPtr left, TokenType op, ExprPtr right, SourceLocation loc);
    
    // Create a binary expression
    ExprPtr makeBinaryExpr(ExprPtr left, TokenType op, ExprPtr right, SourceLocation loc);
};

} // namespace tyl

#endif // TYL_INDVAR_SIMPLIFY_H
