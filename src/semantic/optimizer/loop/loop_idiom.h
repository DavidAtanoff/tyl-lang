// Tyl Compiler - Loop Idiom Recognition Pass
// Recognizes loops that implement memset/memcpy patterns and replaces them
// LLVM equivalent: loop-idiom
#ifndef TYL_LOOP_IDIOM_H
#define TYL_LOOP_IDIOM_H

#include "../optimizer.h"
#include "frontend/ast/ast.h"
#include <set>
#include <map>
#include <vector>

namespace tyl {

// Statistics for Loop Idiom Recognition transformations
struct LoopIdiomStats {
    int memsetPatterns = 0;    // for i in 0..n { a[i] = value }
    int memcpyPatterns = 0;    // for i in 0..n { a[i] = b[i] }
    int loopsSkipped = 0;      // Loops not matching any pattern
};

// Recognized idiom types
enum class LoopIdiom {
    None,
    Memset,     // a[i] = constant_value
    Memcpy,     // a[i] = b[i]
    Memmove,    // a[i] = b[i] with potential overlap
    Strlen,     // while (s[i] != 0) i++
};

// Information about a recognized loop idiom
struct IdiomInfo {
    LoopIdiom type = LoopIdiom::None;
    std::string destArray;      // Destination array name
    std::string srcArray;       // Source array name (for memcpy)
    ExprPtr fillValue;          // Fill value (for memset)
    ExprPtr startIndex;         // Start index
    ExprPtr count;              // Number of elements
    std::string inductionVar;   // Loop induction variable
    bool isConstantFill = false;
    int64_t constantFillValue = 0;
};

// Loop Idiom Recognition Pass
// Transforms:
//   for i in 0..n { a[i] = 0 }     -> memset(a, 0, n * sizeof(element))
//   for i in 0..n { a[i] = b[i] }  -> memcpy(a, b, n * sizeof(element))
//
// This optimization is beneficial because:
// 1. memset/memcpy are highly optimized (use SIMD, cache prefetch)
// 2. Reduces loop overhead
// 3. Enables further optimizations by the backend
class LoopIdiomRecognitionPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "LoopIdiomRecognition"; }
    
    // Get detailed statistics
    const LoopIdiomStats& stats() const { return stats_; }
    
    // Configuration
    void setMinIterations(int min) { minIterations_ = min; }
    
private:
    LoopIdiomStats stats_;
    int minIterations_ = 4;  // Minimum iterations to consider transformation
    
    // Process statements recursively
    void processStatements(std::vector<StmtPtr>& stmts);
    
    // Process a single statement
    void processStatement(StmtPtr& stmt);
    
    // === Pattern Recognition ===
    
    // Try to recognize a loop idiom in a for loop
    IdiomInfo recognizeIdiom(ForStmt* loop);
    
    // Check for memset pattern: a[i] = value
    bool isMemsetPattern(ForStmt* loop, IdiomInfo& info);
    
    // Check for memcpy pattern: a[i] = b[i]
    bool isMemcpyPattern(ForStmt* loop, IdiomInfo& info);
    
    // Check if loop iterates from 0 to n with step 1
    bool isSimpleCountingLoop(ForStmt* loop, ExprPtr& count);
    
    // Check if expression is array[inductionVar]
    bool isArrayIndexByInductionVar(Expression* expr, const std::string& inductionVar,
                                     std::string& arrayName);
    
    // Check if expression is loop-invariant (doesn't depend on induction var)
    bool isLoopInvariant(Expression* expr, const std::string& inductionVar);
    
    // === Transformation ===
    
    // Transform a recognized idiom to intrinsic call
    StmtPtr transformIdiom(ForStmt* loop, const IdiomInfo& info);
    
    // Create memset call: memset(dest, value, count)
    StmtPtr createMemsetCall(const IdiomInfo& info, SourceLocation loc);
    
    // Create memcpy call: memcpy(dest, src, count)
    StmtPtr createMemcpyCall(const IdiomInfo& info, SourceLocation loc);
    
    // === Helper Functions ===
    
    // Clone an expression
    ExprPtr cloneExpression(Expression* expr);
    
    // Get the element count from a range expression
    ExprPtr getRangeCount(Expression* range);
    
    // Check if loop body is a single assignment
    AssignStmt* getSingleAssignment(Statement* body);
    
    // Check if loop body is a single expression statement with assignment
    AssignExpr* getSingleAssignExpr(Statement* body);
};

} // namespace tyl

#endif // TYL_LOOP_IDIOM_H
