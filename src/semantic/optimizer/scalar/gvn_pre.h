// Tyl Compiler - Enhanced GVN with Partial Redundancy Elimination (PRE)
// Combines GVN with load/store optimization and PRE
#ifndef TYL_GVN_PRE_H
#define TYL_GVN_PRE_H

#include "optimizer.h"
#include "gvn.h"
#include "frontend/ast/ast.h"
#include <map>
#include <set>
#include <vector>
#include <string>

namespace tyl {

// Expression availability information
struct AvailabilityInfo {
    std::set<VNKey> available;      // Expressions available at this point
    std::set<VNKey> anticipated;    // Expressions anticipated (will be used)
    std::set<VNKey> earliest;       // Earliest placement points
    std::set<VNKey> latest;         // Latest placement points
};

// Enhanced GVN Pass with PRE
class GVNPREPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "GVN-PRE"; }
    
private:
    // Value numbering state
    ValueNumber nextVN_ = 1;
    std::map<VNKey, ValueNumber> exprToVN_;
    std::map<std::string, ValueNumber> varToVN_;
    std::map<ValueNumber, int64_t> vnToConst_;
    std::map<ValueNumber, double> vnToFloatConst_;
    std::map<ValueNumber, std::string> vnToStringConst_;
    
    // PRE state
    std::map<Statement*, AvailabilityInfo> availability_;
    std::map<VNKey, std::string> exprToTemp_;  // Cached expressions
    int tempCounter_ = 0;
    
    // Load/store optimization state
    std::map<std::string, ValueNumber> memoryState_;  // var -> last stored VN
    std::map<std::pair<std::string, ValueNumber>, ValueNumber> loadCache_;  // (array, index) -> VN
    
    // Value numbering
    ValueNumber getValueNumber(Expression* expr);
    VNKey makeKey(Expression* expr);
    void resetState();
    
    // PRE analysis
    void computeAvailability(std::vector<StmtPtr>& stmts);
    void computeAnticipated(std::vector<StmtPtr>& stmts);
    void insertPRE(std::vector<StmtPtr>& stmts);
    
    // Load/store optimization
    void optimizeLoads(std::vector<StmtPtr>& stmts);
    void optimizeStores(std::vector<StmtPtr>& stmts);
    void invalidateMemory(const std::string& var);
    void invalidateAllMemory();
    
    // Main processing
    void processFunction(FnDecl* fn);
    void processBlock(std::vector<StmtPtr>& stmts);
    void processStatement(StmtPtr& stmt);
    ExprPtr processExpression(ExprPtr& expr);
    
    // Helper to generate temp variable name
    std::string generateTempName();
    
    // Check if expression is worth hoisting
    bool isWorthHoisting(Expression* expr);
    
    // Clone expression
    ExprPtr cloneExpr(Expression* expr);
};

// Load Elimination Pass
// Eliminates redundant loads from memory
class LoadEliminationPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "LoadElimination"; }
    
private:
    // Map (base, index) -> last loaded value
    std::map<std::pair<std::string, std::string>, std::string> loadedValues_;
    
    // Map variable -> known value (for simple loads)
    std::map<std::string, ExprPtr> knownValues_;
    
    void processBlock(std::vector<StmtPtr>& stmts);
    void processStatement(StmtPtr& stmt);
    ExprPtr processExpression(ExprPtr& expr);
    
    void invalidate(const std::string& var);
    void invalidateAll();
};

// Store Sinking Pass
// Moves stores as late as possible
class StoreSinkingPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "StoreSinking"; }
    
private:
    void processFunction(FnDecl* fn);
    void processBlock(std::vector<StmtPtr>& stmts);
    
    // Check if a store can be sunk past a statement
    bool canSinkPast(Statement* store, Statement* stmt);
    
    // Get variables read by a statement
    std::set<std::string> getReads(Statement* stmt);
    std::set<std::string> getReadsExpr(Expression* expr);
};

} // namespace tyl

#endif // TYL_GVN_PRE_H
