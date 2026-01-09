// Tyl Compiler - Partial Inlining Pass
// Inlines only the hot path of functions, keeping cold paths as separate calls
#ifndef TYL_PARTIAL_INLINING_H
#define TYL_PARTIAL_INLINING_H

#include "../optimizer.h"
#include "frontend/ast/ast.h"
#include <map>
#include <set>
#include <vector>

namespace tyl {

// Information about a region that can be outlined
struct OutlineRegionInfo {
    std::vector<Statement*> coldStatements;  // Statements in the cold region
    Statement* entryCondition = nullptr;     // The condition guarding the cold region
    bool isEarlyReturn = false;              // Is this an early return pattern?
    size_t regionCost = 0;                   // Estimated cost of the region
};

// Information about a function for partial inlining
struct PartialInlineCandidate {
    FnDecl* decl = nullptr;
    std::string name;
    size_t totalCost = 0;                    // Total function cost
    size_t hotPathCost = 0;                  // Cost of hot path only
    size_t coldPathCost = 0;                 // Cost of cold path
    bool hasEarlyReturn = false;             // Has early return pattern
    bool hasColdBranch = false;              // Has cold branch pattern
    std::vector<OutlineRegionInfo> coldRegions;
    
    // The condition that guards the main body (for early return)
    Expression* guardCondition = nullptr;
    Statement* earlyReturnStmt = nullptr;
    Statement* mainBody = nullptr;
};

// Statistics for Partial Inlining
struct PartialInliningStats {
    int candidatesFound = 0;          // Functions eligible for partial inlining
    int functionsPartiallyInlined = 0; // Functions that were partially inlined
    int coldRegionsOutlined = 0;       // Cold regions moved to separate functions
    int callSitesOptimized = 0;        // Call sites that benefited
};

// Partial Inlining Pass
// Performs partial inlining by:
// 1. Identifying functions with early return patterns or cold branches
// 2. Inlining only the hot path (guard + early return)
// 3. Keeping the cold path as a separate function call
//
// Example transformation:
//   fn foo(x: int) -> int {
//       if x < 0 { return -1; }  // Early return (hot path - inline this)
//       // ... complex computation (cold path - keep as call)
//   }
//
// After partial inlining at call site:
//   if x < 0 { result = -1; }
//   else { result = foo_cold(x); }
class PartialInliningPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "PartialInlining"; }
    
    // Get detailed statistics
    const PartialInliningStats& stats() const { return stats_; }
    
    // Configuration
    void setMinColdRegionCost(size_t cost) { minColdRegionCost_ = cost; }
    void setMaxInlineHotPathCost(size_t cost) { maxInlineHotPathCost_ = cost; }
    void setMinCostSavingsRatio(float ratio) { minCostSavingsRatio_ = ratio; }
    
private:
    PartialInliningStats stats_;
    
    // Candidates for partial inlining
    std::map<std::string, PartialInlineCandidate> candidates_;
    
    // Generated cold functions
    std::map<std::string, FnDecl*> coldFunctions_;
    
    // Configuration
    size_t minColdRegionCost_ = 20;      // Minimum cost for cold region to outline
    size_t maxInlineHotPathCost_ = 15;   // Maximum cost for hot path to inline
    float minCostSavingsRatio_ = 0.3f;   // Minimum 30% cost savings
    
    // Unique counter for generated names
    int uniqueCounter_ = 0;
    
    // === Phase 1: Analysis ===
    
    // Find candidates for partial inlining
    void findCandidates(Program& ast);
    
    // Analyze a function for partial inlining opportunities
    bool analyzeFunction(FnDecl* fn, PartialInlineCandidate& candidate);
    
    // Check for early return pattern: if (cond) return X; <body>
    bool hasEarlyReturnPattern(FnDecl* fn, PartialInlineCandidate& candidate);
    
    // Check for cold branch pattern: if (cond) { <hot> } else { <cold> }
    bool hasColdBranchPattern(FnDecl* fn, PartialInlineCandidate& candidate);
    
    // Estimate cost of a statement
    size_t estimateCost(Statement* stmt);
    
    // Estimate cost of an expression
    size_t estimateExprCost(Expression* expr);
    
    // === Phase 2: Transformation ===
    
    // Apply partial inlining transformations
    void applyTransformations(Program& ast);
    
    // Create a cold function from the cold region
    FnDecl* createColdFunction(const std::string& baseName, FnDecl* original,
                               Statement* coldBody);
    
    // Inline the hot path at call sites
    void inlineHotPaths(Program& ast);
    void inlineHotPathsInStmt(StmtPtr& stmt);
    void inlineHotPathsInExpr(ExprPtr& expr);
    
    // Create inlined hot path code
    StmtPtr createInlinedHotPath(CallExpr* call, PartialInlineCandidate& candidate);
    
    // Clone expression with argument substitution
    ExprPtr cloneExpr(Expression* expr, const std::map<std::string, Expression*>& argMap);
    
    // Clone statement with argument substitution
    StmtPtr cloneStmt(Statement* stmt, const std::map<std::string, Expression*>& argMap);
    
    // Generate unique name
    std::string generateUniqueName(const std::string& base);
};

} // namespace tyl

#endif // TYL_PARTIAL_INLINING_H
