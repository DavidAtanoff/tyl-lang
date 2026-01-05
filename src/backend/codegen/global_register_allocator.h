// Tyl Compiler - Global Register Allocator
// Top-level register allocation for program-wide optimization
#ifndef TYL_GLOBAL_REGISTER_ALLOCATOR_H
#define TYL_GLOBAL_REGISTER_ALLOCATOR_H

#include "frontend/ast/ast.h"
#include "register_allocator.h"
#include <map>
#include <set>
#include <vector>
#include <string>

namespace tyl {

// Global variable info
struct GlobalVarInfo {
    std::string name;
    int64_t constValue;         // Value if constant
    bool isConstant;            // Is this a compile-time constant?
    bool isUsedInFunctions;     // Is this used inside any function?
    int useCount;               // Number of uses
    int defCount;               // Number of definitions
    VarRegister assignedReg;    // Assigned register (NONE = memory)
    int32_t stackOffset;        // Stack offset if in memory
    
    GlobalVarInfo() : constValue(0), isConstant(false), isUsedInFunctions(false),
                      useCount(0), defCount(0), assignedReg(VarRegister::NONE), 
                      stackOffset(0) {}
};

// Interference graph node
struct InterferenceNode {
    std::string varName;
    std::set<std::string> neighbors;    // Variables that interfere
    int degree;                          // Number of neighbors
    VarRegister color;                   // Assigned register
    bool spilled;                        // Spilled to memory
    int spillCost;                       // Cost of spilling
    
    InterferenceNode() : degree(0), color(VarRegister::NONE), spilled(false), spillCost(0) {}
};

// Global Register Allocator using graph coloring
class GlobalRegisterAllocator {
public:
    GlobalRegisterAllocator();
    
    // Analyze entire program and compute global register assignments
    void analyze(Program& program);
    
    // Get register for a global variable
    VarRegister getGlobalRegister(const std::string& name) const;
    
    // Check if a global variable is in a register
    bool isGlobalInRegister(const std::string& name) const;
    
    // Get constant value if variable is constant
    bool getConstantValue(const std::string& name, int64_t& outValue) const;
    
    // Get all global variables info
    const std::map<std::string, GlobalVarInfo>& getGlobalVars() const { return globalVars_; }
    
    // Get registers used for globals (need to be saved at function boundaries)
    std::vector<VarRegister> getUsedGlobalRegisters() const;
    
    // Get stack offset for a global variable
    int32_t getGlobalStackOffset(const std::string& name) const;
    
    // Reset allocator
    void reset();
    
private:
    std::map<std::string, GlobalVarInfo> globalVars_;
    std::map<std::string, InterferenceNode> interferenceGraph_;
    std::set<VarRegister> usedRegisters_;
    int32_t nextStackOffset_;
    
    // Analysis phases
    void collectGlobalVariables(Program& program);
    void analyzeVariableUsage(Program& program);
    void buildInterferenceGraph(Program& program);
    void colorGraph();
    void assignStackSlots();
    
    // Helper functions
    void scanStatementForUses(Statement* stmt, std::set<std::string>& liveVars);
    void scanExpressionForUses(Expression* expr, std::set<std::string>& liveVars);
    void addInterference(const std::string& var1, const std::string& var2);
    bool tryEvalConstant(Expression* expr, int64_t& outValue);
    
    // Graph coloring helpers
    std::vector<std::string> simplifyGraph();
    void selectColors(const std::vector<std::string>& order);
    VarRegister findAvailableColor(const std::string& var);
};

// SSA-based Global Register Allocator
// Uses SSA form for more precise liveness analysis
class SSAGlobalRegisterAllocator {
public:
    SSAGlobalRegisterAllocator();
    
    // Analyze program in SSA form
    void analyze(Program& program);
    
    // Get register assignment for SSA value
    VarRegister getRegister(int ssaValueId) const;
    
    // Get all register assignments
    const std::map<int, VarRegister>& getAssignments() const { return assignments_; }
    
private:
    std::map<int, VarRegister> assignments_;
    std::map<int, std::set<int>> interferenceGraph_;
    
    void buildLiveRanges();
    void buildInterferenceGraph();
    void allocateRegisters();
};

} // namespace tyl

#endif // TYL_GLOBAL_REGISTER_ALLOCATOR_H
