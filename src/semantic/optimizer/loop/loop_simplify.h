// Tyl Compiler - Loop Simplify Optimization Pass
// Canonicalizes loop structure for better optimization
// - Single entry (preheader)
// - Single backedge (latch)
// - Dedicated exit blocks
#ifndef TYL_LOOP_SIMPLIFY_H
#define TYL_LOOP_SIMPLIFY_H

#include "../optimizer.h"
#include "frontend/ast/ast.h"
#include <set>
#include <map>
#include <vector>

namespace tyl {

// Statistics for Loop Simplify transformations
struct LoopSimplifyStats {
    int preheadersInserted = 0;
    int latchesSimplified = 0;
    int exitBlocksCreated = 0;
    int loopsCanonalized = 0;
};

// Loop Simplify Pass
// Transforms loops into canonical form:
// 1. Single entry block (preheader) - all non-backedge entries go through preheader
// 2. Single backedge (latch) - only one edge back to loop header
// 3. Dedicated exit blocks - exit blocks only have predecessors from within the loop
//
// This canonical form enables:
// - Better LICM (clear preheader for hoisted code)
// - Better loop unrolling (predictable structure)
// - Better induction variable analysis
// - Better vectorization
class LoopSimplifyPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "LoopSimplify"; }
    
    // Get detailed statistics
    const LoopSimplifyStats& stats() const { return stats_; }
    
private:
    LoopSimplifyStats stats_;
    int uniqueCounter_ = 0;
    
    // Process functions
    void processFunction(FnDecl* fn);
    
    // Process statements recursively
    void processStatements(std::vector<StmtPtr>& stmts);
    
    // Process a single statement
    void processStatement(StmtPtr& stmt, std::vector<StmtPtr>& stmts, size_t index);
    
    // === Loop Canonicalization ===
    
    // Canonicalize a while loop
    bool canonicalizeWhileLoop(WhileStmt* loop, std::vector<StmtPtr>& stmts, size_t index);
    
    // Canonicalize a for loop
    bool canonicalizeForLoop(ForStmt* loop, std::vector<StmtPtr>& stmts, size_t index);
    
    // === Preheader Insertion ===
    
    // Check if loop needs a preheader
    bool needsPreheader(Statement* loop, std::vector<StmtPtr>& stmts, size_t index);
    
    // Insert a preheader block before the loop
    void insertPreheader(std::vector<StmtPtr>& stmts, size_t loopIndex);
    
    // === Latch Simplification ===
    
    // Check if loop has multiple backedges (continue statements)
    bool hasMultipleBackedges(Statement* body);
    
    // Simplify multiple backedges into single latch
    void simplifyBackedges(Statement* loop);
    
    // === Exit Block Handling ===
    
    // Check if loop has dedicated exit blocks
    bool hasDedicatedExits(Statement* loop);
    
    // Create dedicated exit blocks
    void createDedicatedExits(Statement* loop);
    
    // === Analysis Helpers ===
    
    // Count break statements in loop body
    int countBreaks(Statement* body);
    
    // Count continue statements in loop body
    int countContinues(Statement* body);
    
    // Check if statement contains loop-exiting control flow
    bool hasLoopExitingFlow(Statement* stmt);
    
    // Check if expression is loop-invariant
    bool isLoopInvariant(Expression* expr, const std::set<std::string>& loopVars);
    
    // Collect variables modified in loop
    std::set<std::string> collectModifiedVars(Statement* body);
    
    // === Transformation Helpers ===
    
    // Generate unique variable name
    std::string generateUniqueName(const std::string& base);
    
    // Clone an expression
    ExprPtr cloneExpression(Expression* expr);
    
    // Clone a statement
    StmtPtr cloneStatement(Statement* stmt);
    
    // Hoist loop-invariant initializations to preheader
    void hoistInvariantInits(std::vector<StmtPtr>& preheader, Statement* loop,
                            const std::set<std::string>& loopVars);
};

} // namespace tyl

#endif // TYL_LOOP_SIMPLIFY_H
