// Tyl Compiler - Enhanced Loop Invariant Code Motion
// More aggressive hoisting with alias analysis
#ifndef TYL_ENHANCED_LICM_H
#define TYL_ENHANCED_LICM_H

#include "optimizer.h"
#include "frontend/ast/ast.h"
#include <set>
#include <map>
#include <vector>

namespace tyl {

// Alias analysis result
enum class AliasResult {
    NoAlias,        // Definitely don't alias
    MayAlias,       // Might alias
    MustAlias,      // Definitely alias
    PartialAlias    // Partially overlap
};

// Memory location representation
struct MemoryLocation {
    std::string base;           // Base variable name
    bool isArray = false;       // Is this an array access?
    Expression* index = nullptr; // Index expression (if array)
    
    bool operator<(const MemoryLocation& other) const {
        if (base != other.base) return base < other.base;
        return isArray < other.isArray;
    }
};

// Simple alias analysis
class AliasAnalysis {
public:
    // Check if two memory locations may alias
    AliasResult alias(const MemoryLocation& loc1, const MemoryLocation& loc2);
    
    // Check if an expression may read from a memory location
    bool mayReadFrom(Expression* expr, const MemoryLocation& loc);
    
    // Check if a statement may write to a memory location
    bool mayWriteTo(Statement* stmt, const MemoryLocation& loc);
    
    // Get memory locations read by an expression
    std::set<MemoryLocation> getReads(Expression* expr);
    
    // Get memory locations written by a statement
    std::set<MemoryLocation> getWrites(Statement* stmt);
};

// Enhanced LICM Pass
class EnhancedLICMPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "EnhancedLICM"; }
    
private:
    AliasAnalysis aliasAnalysis_;
    
    // Set of variables modified in current loop
    std::set<std::string> modifiedVars_;
    
    // Set of memory locations modified in current loop
    std::set<MemoryLocation> modifiedLocations_;
    
    // Set of pure functions (no side effects)
    std::set<std::string> pureFunctions_;
    
    // Process statements
    void processStatements(std::vector<StmtPtr>& stmts);
    
    // Analyze a loop for modified variables and memory
    void analyzeLoop(Statement* loop);
    void analyzeModifiedVars(Statement* stmt);
    void analyzeModifiedMemory(Statement* stmt);
    
    // Check if expression is loop invariant
    bool isLoopInvariant(Expression* expr, const std::string& inductionVar);
    
    // Check if statement is safe to hoist
    bool isSafeToHoist(Statement* stmt, const std::string& inductionVar);
    
    // Check if expression has side effects
    bool hasSideEffects(Expression* expr);
    
    // Check if function is pure
    bool isPureFunction(const std::string& name);
    
    // Hoist invariant code from for loop
    void hoistFromForLoop(ForStmt* loop, std::vector<StmtPtr>& hoisted);
    
    // Hoist invariant code from while loop
    void hoistFromWhileLoop(WhileStmt* loop, std::vector<StmtPtr>& hoisted);
    
    // Hoist invariant expressions within a statement
    void hoistInvariantExpressions(Statement* stmt, const std::string& inductionVar,
                                    std::vector<StmtPtr>& hoisted);
    
    // Create a temporary variable for hoisted expression
    StmtPtr createTempVar(Expression* expr, std::string& tempName);
    
    // Replace expression with identifier
    ExprPtr replaceWithTemp(Expression* expr, const std::string& tempName);
    
    int tempCounter_ = 0;
    std::string generateTempName();
};

// Loop Invariant Expression Hoisting
// Hoists invariant sub-expressions even when full statement can't be hoisted
class InvariantExpressionHoistingPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "InvariantExprHoist"; }
    
private:
    std::set<std::string> modifiedVars_;
    int tempCounter_ = 0;
    
    void processStatements(std::vector<StmtPtr>& stmts);
    void processLoop(ForStmt* loop, std::vector<StmtPtr>& insertBefore);
    void processLoop(WhileStmt* loop, std::vector<StmtPtr>& insertBefore);
    
    void analyzeModifiedVars(Statement* stmt);
    bool isInvariant(Expression* expr, const std::string& inductionVar);
    
    // Find and hoist invariant sub-expressions
    void findInvariantSubExprs(Expression* expr, const std::string& inductionVar,
                               std::vector<std::pair<Expression*, std::string>>& toHoist);
    
    std::string generateTempName();
};

} // namespace tyl

#endif // TYL_ENHANCED_LICM_H
