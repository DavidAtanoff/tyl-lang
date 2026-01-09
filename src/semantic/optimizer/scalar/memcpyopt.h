// Tyl Compiler - Memory Copy Optimization Pass
// Optimizes memory operations: merges adjacent stores, converts to memset/memcpy
// LLVM equivalent: memcpyopt
#ifndef TYL_MEMCPYOPT_H
#define TYL_MEMCPYOPT_H

#include "../optimizer.h"
#include "frontend/ast/ast.h"
#include <set>
#include <map>
#include <vector>

namespace tyl {

// Statistics for MemCpyOpt transformations
struct MemCpyOptStats {
    int storesMergedToMemset = 0;   // Adjacent stores merged to memset
    int storesMergedToMemcpy = 0;   // Adjacent stores merged to memcpy
    int memcpyToMemset = 0;         // memcpy of constant converted to memset
    int deadMemcpyRemoved = 0;      // Redundant memcpy removed
};

// Represents a range of memory being written to
struct MemoryRange {
    std::string arrayName;
    int64_t startIndex;
    int64_t endIndex;  // Exclusive
    bool hasConstantValue;
    int64_t constantValue;
    std::string sourceArray;  // For memcpy patterns
    int64_t sourceStartIndex;
    std::vector<size_t> stmtIndices;  // Indices of statements in this range
};

// Information about a store operation
struct StoreOp {
    size_t stmtIndex;
    std::string arrayName;
    int64_t index;
    bool hasConstantIndex;
    bool hasConstantValue;
    int64_t constantValue;
    std::string sourceArray;  // If copying from another array
    int64_t sourceIndex;
    bool isFromArray;
};

// Memory Copy Optimization Pass
// Optimizations performed:
// 1. Merge adjacent stores to same array with same value -> memset
//    a[0] = 0; a[1] = 0; a[2] = 0; -> memset(a, 0, 3)
// 2. Merge adjacent stores copying from another array -> memcpy
//    a[0] = b[0]; a[1] = b[1]; -> memcpy(a, b, 2)
// 3. Convert memcpy of constant data to memset
// 4. Remove dead memcpy (overwritten before read)
class MemCpyOptPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "MemCpyOpt"; }
    
    // Get detailed statistics
    const MemCpyOptStats& stats() const { return stats_; }
    
    // Configuration
    void setMinStoresForMemset(int min) { minStoresForMemset_ = min; }
    void setMinStoresForMemcpy(int min) { minStoresForMemcpy_ = min; }
    
private:
    MemCpyOptStats stats_;
    int minStoresForMemset_ = 3;  // Minimum stores to merge into memset
    int minStoresForMemcpy_ = 2;  // Minimum stores to merge into memcpy
    
    // Process statements recursively
    void processStatements(std::vector<StmtPtr>& stmts);
    
    // Process a single statement
    void processStatement(StmtPtr& stmt);
    
    // === Store Analysis ===
    
    // Analyze stores in a block
    std::vector<StoreOp> analyzeStores(const std::vector<StmtPtr>& stmts);
    
    // Check if statement is an array store
    bool isArrayStore(Statement* stmt, StoreOp& info);
    
    // Check if expression is an array store
    bool isArrayStoreExpr(Expression* expr, StoreOp& info);
    
    // === Range Merging ===
    
    // Find mergeable ranges of stores
    std::vector<MemoryRange> findMergeableRanges(const std::vector<StoreOp>& stores);
    
    // Check if two stores are adjacent and compatible
    bool areAdjacentStores(const StoreOp& a, const StoreOp& b);
    
    // Merge a range of stores
    void mergeRange(MemoryRange& range, const StoreOp& store);
    
    // === Transformation ===
    
    // Apply transformations to a block
    void applyTransformations(std::vector<StmtPtr>& stmts, 
                              const std::vector<MemoryRange>& ranges);
    
    // Create memset call for a range
    StmtPtr createMemsetForRange(const MemoryRange& range, SourceLocation loc);
    
    // Create memcpy call for a range
    StmtPtr createMemcpyForRange(const MemoryRange& range, SourceLocation loc);
    
    // === Dead Store Analysis ===
    
    // Find and remove dead memcpy operations
    void removeDeadMemcpy(std::vector<StmtPtr>& stmts);
    
    // Check if a memcpy result is overwritten before being read
    bool isMemcpyDead(const std::vector<StmtPtr>& stmts, size_t memcpyIndex);
    
    // === Helper Functions ===
    
    // Clone an expression
    ExprPtr cloneExpression(Expression* expr);
    
    // Get constant value from expression if possible
    bool getConstantValue(Expression* expr, int64_t& value);
    
    // Get array name and index from index expression
    bool parseArrayAccess(Expression* expr, std::string& arrayName, 
                          int64_t& index, bool& isConstantIndex);
};

} // namespace tyl

#endif // TYL_MEMCPYOPT_H
