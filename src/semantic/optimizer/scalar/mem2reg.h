// Tyl Compiler - mem2reg (Memory to Register Promotion) Pass
// Promotes stack allocations to SSA registers when possible
// This is fundamental for good code generation and reduces memory traffic
#ifndef TYL_MEM2REG_H
#define TYL_MEM2REG_H

#include "optimizer.h"
#include "frontend/ast/ast.h"
#include <map>
#include <set>
#include <vector>
#include <string>
#include <stack>

namespace tyl {

// Information about a variable for promotion analysis
struct PromotableVar {
    std::string name;
    std::string typeName;
    bool isPromotable = true;
    bool hasAddressTaken = false;
    bool hasComplexUse = false;  // Used in ways that prevent promotion
    int defCount = 0;            // Number of definitions
    int useCount = 0;            // Number of uses
    SourceLocation location;
};

// SSA version of a variable
struct SSAVersion {
    std::string originalName;
    int version;
    ExprPtr value;  // The value at this version (for propagation)
    
    std::string versionedName() const {
        return originalName + "_v" + std::to_string(version);
    }
};

// Basic block representation for dominance analysis
struct BasicBlock {
    int id;
    std::vector<Statement*> statements;
    std::vector<BasicBlock*> predecessors;
    std::vector<BasicBlock*> successors;
    BasicBlock* immediateDominator = nullptr;
    std::set<BasicBlock*> dominanceFrontier;
    
    // Variables defined and used in this block
    std::set<std::string> defs;
    std::set<std::string> uses;
    
    // Phi nodes needed at this block
    std::map<std::string, std::vector<std::pair<BasicBlock*, int>>> phiNodes;
    // phiNodes[varName] = [(predBlock, version), ...]
};

class Mem2RegPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "Mem2Reg"; }
    
private:
    // Promotable variables in current function
    std::map<std::string, PromotableVar> promotableVars_;
    
    // Current SSA version for each variable
    std::map<std::string, int> currentVersion_;
    
    // Stack of versions for each variable (for renaming)
    std::map<std::string, std::stack<int>> versionStack_;
    
    // Value at each version (for constant propagation during promotion)
    std::map<std::string, std::map<int, ExprPtr>> versionValues_;
    
    // Basic blocks for current function
    std::vector<std::unique_ptr<BasicBlock>> blocks_;
    
    // Process the program
    void processStatements(std::vector<StmtPtr>& stmts);
    void processFunction(FnDecl* fn);
    
    // Phase 1: Identify promotable variables
    void findPromotableVars(std::vector<StmtPtr>& stmts);
    void analyzeVarUse(Statement* stmt);
    void checkAddressTaken(Expression* expr);
    bool isSimpleType(const std::string& typeName);
    
    // Phase 2: Build CFG and compute dominance
    void buildCFG(std::vector<StmtPtr>& stmts);
    void computeDominators();
    void computeDominanceFrontier();
    
    // Phase 3: Insert phi nodes (simplified - we use value propagation instead)
    void insertPhiNodes();
    
    // Phase 4: Rename variables to SSA form
    void renameVariables(std::vector<StmtPtr>& stmts);
    void renameInBlock(std::vector<StmtPtr>& stmts);
    ExprPtr renameExpression(ExprPtr& expr);
    
    // Phase 5: Propagate values and eliminate loads
    void propagateValues(std::vector<StmtPtr>& stmts);
    void propagateInStatement(StmtPtr& stmt);
    ExprPtr propagateInExpression(ExprPtr& expr);
    
    // Phase 6: Remove promoted allocations
    void removePromotedAllocations(std::vector<StmtPtr>& stmts);
    
    // Helper functions
    int getNextVersion(const std::string& varName);
    int getCurrentVersion(const std::string& varName);
    void pushVersion(const std::string& varName, int version);
    void popVersion(const std::string& varName);
    void setVersionValue(const std::string& varName, int version, ExprPtr value);
    ExprPtr getVersionValue(const std::string& varName, int version);
    
    // Clone an expression
    ExprPtr cloneExpr(Expression* expr);
};

std::unique_ptr<Mem2RegPass> createMem2RegPass();

} // namespace tyl

#endif // TYL_MEM2REG_H
