// Tyl Compiler - Loop Rotation Optimization Pass
// Transforms loops to have exit condition at bottom (do-while form)
// This enables better LICM, loop unrolling, and vectorization
#ifndef TYL_LOOP_ROTATION_H
#define TYL_LOOP_ROTATION_H

#include "../optimizer.h"
#include "frontend/ast/ast.h"
#include <set>
#include <map>
#include <vector>

namespace tyl {

// Statistics for Loop Rotation transformations
struct LoopRotationStats {
    int whileLoopsRotated = 0;
    int forLoopsRotated = 0;
    int loopsSkipped = 0;  // Loops not rotated (already rotated, too complex, etc.)
};

// Loop Rotation Pass
// Transforms: while(cond) { body }
// To:         if(cond) { do { body } while(cond) }
//
// This puts the exit test at the bottom of the loop, which:
// 1. Reduces branch instructions in the common case
// 2. Enables better LICM (loop header is simpler)
// 3. Enables better loop unrolling
// 4. Improves instruction scheduling
class LoopRotationPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "LoopRotation"; }
    
    // Get detailed statistics
    const LoopRotationStats& stats() const { return stats_; }
    
    // Configuration
    void setMaxHeaderSize(int size) { maxHeaderSize_ = size; }
    
private:
    LoopRotationStats stats_;
    int maxHeaderSize_ = 16;  // Max statements to duplicate in header
    
    // Process statements recursively
    void processStatements(std::vector<StmtPtr>& stmts);
    
    // Process a single statement
    void processStatement(StmtPtr& stmt);
    
    // === Loop Rotation Core ===
    
    // Try to rotate a while loop
    // Returns the rotated loop (or nullptr if not rotated)
    StmtPtr tryRotateWhileLoop(WhileStmt* loop);
    
    // Try to rotate a for loop
    // Returns the rotated loop (or nullptr if not rotated)
    StmtPtr tryRotateForLoop(ForStmt* loop);
    
    // === Profitability Analysis ===
    
    // Check if rotation is profitable for this loop
    bool shouldRotate(Statement* loop);
    
    // Check if the loop condition is simple enough to duplicate
    bool isSimpleCondition(Expression* cond);
    
    // Count the "cost" of duplicating an expression
    int expressionCost(Expression* expr);
    
    // Check if loop body contains break/continue that would complicate rotation
    bool hasComplexControlFlow(Statement* body);
    
    // Check if loop is already in rotated form (condition at bottom)
    bool isAlreadyRotated(Statement* loop);
    
    // === Transformation Helpers ===
    
    // Clone the loop condition
    ExprPtr cloneCondition(Expression* cond);
    
    // Clone a statement
    StmtPtr cloneStatement(Statement* stmt);
    
    // Clone an expression
    ExprPtr cloneExpression(Expression* expr);
    
    // Create a do-while style loop from a while loop
    // while(cond) { body } -> if(cond) { do { body } while(cond) }
    StmtPtr createRotatedWhileLoop(WhileStmt* original);
    
    // Create a rotated for loop
    // for x in range { body } -> if(range not empty) { do { body; x++ } while(x < end) }
    StmtPtr createRotatedForLoop(ForStmt* original);
};

} // namespace tyl

#endif // TYL_LOOP_ROTATION_H
