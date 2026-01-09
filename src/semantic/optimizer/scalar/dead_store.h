// Tyl Compiler - Dead Store Elimination Pass
// Removes stores to variables that are overwritten before being read
#ifndef TYL_DEAD_STORE_H
#define TYL_DEAD_STORE_H

#include "optimizer.h"
#include "frontend/ast/ast.h"
#include <unordered_set>
#include <unordered_map>
#include <set>
#include <functional>

namespace tyl {

// Information about a store (assignment or variable declaration with initializer)
struct StoreInfo {
    size_t index;           // Index in statement list
    std::string varName;    // Variable being stored to
    bool isDeclaration;     // True if VarDecl, false if AssignStmt
    bool isDead;            // Marked for removal
    std::set<std::string> readsInValue;  // Variables read in the RHS
};

class DeadStoreEliminationPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "DeadStoreElimination"; }
    
private:
    // Process a block of statements
    void processBlock(std::vector<StmtPtr>& statements);
    
    // Process nested structures (functions, if/while/for bodies)
    void processNestedStructures(std::vector<StmtPtr>& statements);
    
    // Analyze stores in a block
    void analyzeStores(std::vector<StmtPtr>& statements, 
                       std::vector<StoreInfo>& stores);
    
    // Collect variable reads from an expression
    void collectReads(Expression* expr, std::set<std::string>& reads);
    
    // Collect variable reads from a statement
    void collectReadsFromStmt(Statement* stmt, std::set<std::string>& reads);
    
    // Check if a variable is read between two indices
    bool isReadBetween(const std::vector<StmtPtr>& statements,
                       const std::string& varName,
                       size_t startIdx, size_t endIdx);
    
    // Check if a variable is read after an index (to end of block)
    bool isReadAfter(const std::vector<StmtPtr>& statements,
                     const std::string& varName,
                     size_t startIdx);
    
    // Check if a variable escapes (passed to function, returned, etc.)
    bool variableEscapes(const std::vector<StmtPtr>& statements,
                         const std::string& varName,
                         size_t startIdx);
    
    // Remove dead stores from statement list
    void removeDeadStores(std::vector<StmtPtr>& statements,
                          const std::vector<StoreInfo>& stores);
};

// Factory function
std::unique_ptr<DeadStoreEliminationPass> createDeadStoreEliminationPass();

} // namespace tyl

#endif // TYL_DEAD_STORE_H
