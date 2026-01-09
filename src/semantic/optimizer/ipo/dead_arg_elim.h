// Tyl Compiler - Dead Argument Elimination Pass
// Removes unused function arguments
#ifndef TYL_DEAD_ARG_ELIM_H
#define TYL_DEAD_ARG_ELIM_H

#include "../optimizer.h"
#include "frontend/ast/ast.h"
#include <map>
#include <set>
#include <vector>

namespace tyl {

// Information about argument usage
struct ArgumentUsage {
    std::string funcName;
    std::string argName;
    int argIndex;
    bool isUsed = false;           // Is the argument used in the function body?
    bool isPassedThrough = false;  // Is it only passed to another function?
    bool canEliminate = false;     // Can this argument be eliminated?
};

// Function signature info for dead argument elimination
struct FunctionSignature {
    FnDecl* decl = nullptr;
    std::vector<ArgumentUsage> arguments;
    std::vector<int> deadArgIndices;      // Indices of arguments that can be removed
    bool hasVarArgs = false;               // Has variadic arguments
    bool isExtern = false;                 // External function (can't modify)
    bool isCallback = false;               // Used as callback (can't modify signature)
    bool isRecursive = false;              // Recursive function
    std::set<std::string> callers;         // Functions that call this function
};

// Statistics for Dead Argument Elimination
struct DeadArgElimStats {
    int argumentsRemoved = 0;
    int functionsModified = 0;
    int callSitesUpdated = 0;
};

// Dead Argument Elimination Pass
// Removes function arguments that are never used:
// 1. Analyze all functions to find unused arguments
// 2. Check that all call sites can be updated
// 3. Remove the argument from function signature
// 4. Update all call sites to not pass the argument
class DeadArgElimPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "DeadArgElim"; }
    
    // Get detailed statistics
    const DeadArgElimStats& stats() const { return stats_; }
    
private:
    DeadArgElimStats stats_;
    
    // Function signatures
    std::map<std::string, FunctionSignature> signatures_;
    
    // Call sites: funcName -> list of CallExpr*
    std::map<std::string, std::vector<CallExpr*>> callSites_;
    
    // Functions that are used as callbacks (address taken)
    std::set<std::string> callbackFunctions_;
    
    // === Phase 1: Collection ===
    
    // Collect all functions and their signatures
    void collectFunctions(Program& ast);
    
    // Collect all call sites
    void collectCallSites(Program& ast);
    void collectCallSitesInStmt(Statement* stmt);
    void collectCallSitesInExpr(Expression* expr);
    
    // Collect functions used as callbacks
    void collectCallbacks(Program& ast);
    void collectCallbacksInStmt(Statement* stmt);
    void collectCallbacksInExpr(Expression* expr);
    
    // === Phase 2: Analysis ===
    
    // Analyze argument usage for all functions
    void analyzeArgumentUsage();
    
    // Analyze a single function's argument usage
    void analyzeFunctionArgs(FunctionSignature& sig);
    
    // Check if an argument is used in a statement
    bool isArgUsedInStmt(Statement* stmt, const std::string& argName);
    
    // Check if an argument is used in an expression
    bool isArgUsedInExpr(Expression* expr, const std::string& argName);
    
    // Determine which arguments can be eliminated
    void determineEliminablArgs();
    
    // Check if we can safely eliminate an argument
    bool canEliminateArg(const FunctionSignature& sig, int argIndex);
    
    // === Phase 3: Transformation ===
    
    // Apply the transformations
    void applyTransformations(Program& ast);
    
    // Remove dead arguments from a function
    void removeDeadArgs(FnDecl* fn, const std::vector<int>& deadIndices);
    
    // Update all call sites for a function
    void updateCallSites(const std::string& funcName, const std::vector<int>& deadIndices);
    
    // Update a single call expression
    void updateCallExpr(CallExpr* call, const std::vector<int>& deadIndices);
    
    // === Helper Functions ===
    
    // Check if function is external (FFI)
    bool isExternalFunction(FnDecl* fn);
    
    // Check if function is recursive
    bool isRecursiveFunction(FnDecl* fn);
    bool callsFunction(Statement* stmt, const std::string& funcName);
    bool callsFunctionInExpr(Expression* expr, const std::string& funcName);
    
    // Get function name from a call expression
    std::string getCalledFunctionName(CallExpr* call);
};

} // namespace tyl

#endif // TYL_DEAD_ARG_ELIM_H
