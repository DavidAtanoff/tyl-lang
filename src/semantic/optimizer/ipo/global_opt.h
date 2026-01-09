// Tyl Compiler - Global Optimization Pass
// Optimizes global variables: constify, internalize, eliminate unused
#ifndef TYL_GLOBAL_OPT_H
#define TYL_GLOBAL_OPT_H

#include "../optimizer.h"
#include "frontend/ast/ast.h"
#include <map>
#include <set>
#include <vector>

namespace tyl {

// Information about a global variable
struct GlobalVarInfo {
    VarDecl* decl = nullptr;
    std::string name;
    bool isRead = false;           // Is the global read anywhere?
    bool isWritten = false;        // Is the global written after initialization?
    bool isAddressTaken = false;   // Is the address of the global taken?
    bool isExported = false;       // Is it visible outside the module?
    bool canConstify = false;      // Can be marked as constant
    bool canEliminate = false;     // Can be completely removed
    bool hasConstantInit = false;  // Has a constant initializer
    int64_t constantIntValue = 0;  // If constant int, its value
    double constantFloatValue = 0; // If constant float, its value
    bool constantBoolValue = false;// If constant bool, its value
    std::string constantStrValue;  // If constant string, its value
    enum class ConstType { None, Int, Float, Bool, String } constType = ConstType::None;
    std::set<std::string> readInFunctions;   // Functions that read this global
    std::set<std::string> writtenInFunctions; // Functions that write this global
};

// Statistics for Global Optimization
struct GlobalOptStats {
    int globalsConstified = 0;      // Globals marked as constant
    int globalsEliminated = 0;      // Unused globals removed
    int globalsInternalized = 0;    // Globals made internal/static
    int loadsReplaced = 0;          // Loads replaced with constants
    int storesEliminated = 0;       // Stores to constant globals removed
    int globalsSRAd = 0;            // Globals broken into scalars
};

// Global Optimization Pass
// Performs several optimizations on global variables:
// 1. Constify: Mark globals that are never written as constant
// 2. Eliminate: Remove globals that are never read
// 3. Internalize: Make globals that are only used in one module internal
// 4. Constant propagation: Replace loads from constant globals with values
// 5. SRA: Break up aggregate globals into scalars when beneficial
class GlobalOptPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "GlobalOpt"; }
    
    // Get detailed statistics
    const GlobalOptStats& stats() const { return stats_; }
    
private:
    GlobalOptStats stats_;
    
    // Global variable information
    std::map<std::string, GlobalVarInfo> globals_;
    
    // Functions in the program
    std::set<std::string> functions_;
    
    // === Phase 1: Collection ===
    
    // Collect all global variables
    void collectGlobals(Program& ast);
    
    // Collect all functions
    void collectFunctions(Program& ast);
    
    // === Phase 2: Analysis ===
    
    // Analyze usage of all globals
    void analyzeGlobalUsage(Program& ast);
    
    // Analyze usage in a function
    void analyzeUsageInFunction(FnDecl* fn);
    
    // Analyze usage in a statement
    void analyzeUsageInStmt(Statement* stmt, const std::string& funcName);
    
    // Analyze usage in an expression
    void analyzeUsageInExpr(Expression* expr, const std::string& funcName, bool isWrite = false);
    
    // Determine which globals can be optimized
    void determineOptimizations();
    
    // Check if an expression is a constant
    bool isConstantExpr(Expression* expr);
    
    // Evaluate a constant expression
    bool evaluateConstant(Expression* expr, GlobalVarInfo& info);
    
    // === Phase 3: Transformation ===
    
    // Apply all optimizations
    void applyOptimizations(Program& ast);
    
    // Eliminate unused globals
    void eliminateUnusedGlobals(Program& ast);
    
    // Replace loads from constant globals
    void replaceConstantLoads(Program& ast);
    void replaceConstantLoadsInStmt(Statement* stmt);
    void replaceConstantLoadsInExpr(ExprPtr& expr);
    
    // Create constant expression from global info
    ExprPtr createConstantExpr(const GlobalVarInfo& info, SourceLocation loc);
};

} // namespace tyl

#endif // TYL_GLOBAL_OPT_H
