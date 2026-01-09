// Tyl Compiler - SROA (Scalar Replacement of Aggregates) Pass
// Breaks up aggregates (records/structs) into individual scalar variables
// This enables better register allocation and further optimizations
#ifndef TYL_SROA_H
#define TYL_SROA_H

#include "optimizer.h"
#include "frontend/ast/ast.h"
#include <map>
#include <set>
#include <vector>
#include <string>

namespace tyl {

// Information about a record variable that can be split
struct SROACandidate {
    std::string varName;
    std::string typeName;
    std::vector<std::pair<std::string, std::string>> fields;  // field name -> type name
    bool canSplit = true;
    SourceLocation location;
};

// Mapping from original field access to scalar replacement
struct ScalarReplacement {
    std::string originalVar;
    std::string fieldName;
    std::string scalarName;
    std::string typeName;
};

class SROAPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "SROA"; }
    
private:
    // Record type definitions (name -> fields)
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> recordTypes_;
    
    // Current function's SROA candidates
    std::map<std::string, SROACandidate> candidates_;
    
    // Scalar replacements for current function
    std::map<std::string, std::map<std::string, std::string>> scalarReplacements_;
    // scalarReplacements_[varName][fieldName] = scalarVarName
    
    // Process the program
    void collectRecordTypes(Program& ast);
    void processStatements(std::vector<StmtPtr>& stmts);
    void processFunction(FnDecl* fn);
    
    // Analysis phase
    void findCandidates(std::vector<StmtPtr>& stmts);
    void checkCandidate(Statement* stmt, const std::string& varName);
    bool isAddressTaken(Expression* expr, const std::string& varName);
    bool isWholeRecordUse(Expression* expr, const std::string& varName);
    
    // Transformation phase
    void createScalarReplacements(std::vector<StmtPtr>& stmts);
    void rewriteAccesses(std::vector<StmtPtr>& stmts);
    void rewriteStatement(StmtPtr& stmt);
    ExprPtr rewriteExpression(ExprPtr& expr);
    
    // Check if a type is a record type we can split
    bool isRecordType(const std::string& typeName);
    
    // Get fields for a record type
    std::vector<std::pair<std::string, std::string>> getRecordFields(const std::string& typeName);
    
    // Generate scalar variable name
    std::string makeScalarName(const std::string& varName, const std::string& fieldName);
    
    // Clone an expression
    ExprPtr cloneExpr(Expression* expr);
};

std::unique_ptr<SROAPass> createSROAPass();

} // namespace tyl

#endif // TYL_SROA_H
