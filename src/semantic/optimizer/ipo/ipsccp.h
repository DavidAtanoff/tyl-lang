// Tyl Compiler - Inter-Procedural Sparse Conditional Constant Propagation
// Propagates constants across function boundaries
#ifndef TYL_IPSCCP_H
#define TYL_IPSCCP_H

#include "../optimizer.h"
#include "frontend/ast/ast.h"
#include <map>
#include <set>
#include <vector>
#include <optional>

namespace tyl {

// Lattice value for SCCP
// Bottom (undefined) -> Constant -> Top (overdefined/unknown)
struct LatticeValue {
    enum class State {
        Bottom,     // Not yet computed (undefined)
        Constant,   // Known constant value
        Top         // Overdefined (multiple values possible)
    };
    
    State state = State::Bottom;
    int64_t intValue = 0;
    double floatValue = 0.0;
    bool boolValue = false;
    std::string stringValue;
    
    enum class Type { Unknown, Int, Float, Bool, String } type = Type::Unknown;
    
    bool isBottom() const { return state == State::Bottom; }
    bool isConstant() const { return state == State::Constant; }
    bool isTop() const { return state == State::Top; }
    
    static LatticeValue bottom() { return LatticeValue{State::Bottom}; }
    static LatticeValue top() { return LatticeValue{State::Top}; }
    static LatticeValue constant(int64_t v) { 
        LatticeValue lv{State::Constant}; 
        lv.intValue = v; 
        lv.type = Type::Int;
        return lv; 
    }
    static LatticeValue constant(double v) { 
        LatticeValue lv{State::Constant}; 
        lv.floatValue = v; 
        lv.type = Type::Float;
        return lv; 
    }
    static LatticeValue constant(bool v) { 
        LatticeValue lv{State::Constant}; 
        lv.boolValue = v; 
        lv.type = Type::Bool;
        return lv; 
    }
    
    // Meet operation (lattice join)
    LatticeValue meet(const LatticeValue& other) const;
    
    bool operator==(const LatticeValue& other) const;
    bool operator!=(const LatticeValue& other) const { return !(*this == other); }
};

// Function summary for inter-procedural analysis
struct FunctionSummary {
    FnDecl* decl = nullptr;
    std::vector<LatticeValue> argValues;  // Lattice values for each argument
    LatticeValue returnValue;              // Lattice value for return
    bool isConstantReturn = false;         // Returns a constant
    bool hasBeenAnalyzed = false;
    bool hasSideEffects = false;
    std::set<std::string> calledFunctions; // Functions this function calls
};

// Statistics for IPSCCP
struct IPSCCPStats {
    int constantsFound = 0;
    int argumentsConstified = 0;
    int returnsConstified = 0;
    int callsSimplified = 0;
    int branchesSimplified = 0;
    int deadCodeRemoved = 0;
};

// Inter-Procedural Sparse Conditional Constant Propagation Pass
// Propagates constants across function boundaries:
// 1. If all call sites pass the same constant for an argument, treat it as constant
// 2. If a function always returns the same constant, replace calls with that constant
// 3. Use conditional constant propagation within functions
class IPSCCPPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "IPSCCP"; }
    
    // Get detailed statistics
    const IPSCCPStats& stats() const { return stats_; }
    
private:
    IPSCCPStats stats_;
    
    // Function summaries
    std::map<std::string, FunctionSummary> functionSummaries_;
    
    // Variable lattice values (per function)
    std::map<std::string, std::map<std::string, LatticeValue>> variableValues_;
    
    // Worklist for iterative analysis
    std::set<std::string> worklist_;
    
    // === Phase 1: Collection ===
    
    // Collect all functions and build call graph
    void collectFunctions(Program& ast);
    
    // Collect call sites for each function
    void collectCallSites(Program& ast);
    
    // === Phase 2: Analysis ===
    
    // Run the SCCP algorithm
    void runSCCP();
    
    // Analyze a single function
    void analyzeFunction(const std::string& funcName);
    
    // Analyze a statement
    void analyzeStatement(Statement* stmt, const std::string& funcName);
    
    // Evaluate an expression to a lattice value
    LatticeValue evaluateExpression(Expression* expr, const std::string& funcName);
    
    // Evaluate a binary operation
    LatticeValue evaluateBinaryOp(TokenType op, const LatticeValue& lhs, const LatticeValue& rhs);
    
    // Evaluate a unary operation
    LatticeValue evaluateUnaryOp(TokenType op, const LatticeValue& operand);
    
    // Update variable value and add to worklist if changed
    bool updateValue(const std::string& funcName, const std::string& varName, const LatticeValue& value);
    
    // Mark all variables modified in a loop body as top (non-constant)
    void markLoopModifiedVariablesAsTop(Block* body, const std::string& funcName);
    
    // === Phase 3: Transformation ===
    
    // Apply transformations based on analysis
    void applyTransformations(Program& ast);
    
    // Transform a function based on constant arguments
    void transformFunction(FnDecl* fn);
    
    // Transform statements
    void transformStatements(std::vector<StmtPtr>& stmts, const std::string& funcName);
    
    // Transform a single statement
    void transformStatement(StmtPtr& stmt, const std::string& funcName);
    
    // Transform an expression (replace with constant if known)
    ExprPtr transformExpression(Expression* expr, const std::string& funcName);
    
    // Replace call with constant if function always returns constant
    ExprPtr tryReplaceCallWithConstant(CallExpr* call);
    
    // === Helper Functions ===
    
    // Check if a function has side effects
    bool hasSideEffects(FnDecl* fn);
    bool hasSideEffectsInStmt(Statement* stmt);
    bool hasSideEffectsInExpr(Expression* expr);
    
    // Get the lattice value for a variable
    LatticeValue getVariableValue(const std::string& funcName, const std::string& varName);
    
    // Create a constant expression from a lattice value
    ExprPtr createConstantExpr(const LatticeValue& value, const SourceLocation& loc);
};

} // namespace tyl

#endif // TYL_IPSCCP_H
