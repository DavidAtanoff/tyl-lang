// Tyl Compiler - Register Allocator
// Linear Scan Register Allocation for local variables
#ifndef TYL_REGISTER_ALLOCATOR_H
#define TYL_REGISTER_ALLOCATOR_H

#include "frontend/ast/ast.h"
#include <map>
#include <set>
#include <vector>
#include <string>

namespace tyl {

// Available callee-saved registers for variable allocation
// Windows x64: RBX, R12, R13, R14, R15 are callee-saved
enum class VarRegister {
    NONE = 0,   // Variable is on stack
    RBX,        // First callee-saved
    R12,
    R13,
    R14,
    R15
};

// Live range for a variable
struct LiveRange {
    std::string name;
    int startPos;       // First use/def position
    int endPos;         // Last use position
    VarRegister reg;    // Assigned register (NONE = spilled to stack)
    int32_t stackOffset; // Stack offset if spilled
    bool isParam;       // Is this a function parameter?
    int paramIndex;     // Parameter index (0-3 for register params)
};

class RegisterAllocator {
public:
    RegisterAllocator();
    
    // Analyze a function and compute register assignments
    void analyze(FnDecl& fn);
    
    // Get the register assigned to a variable (NONE if on stack)
    VarRegister getRegister(const std::string& name) const;
    
    // Check if a variable is in a register
    bool isInRegister(const std::string& name) const;
    
    // Get all variables that need to be saved/restored
    std::vector<VarRegister> getUsedRegisters() const;
    
    // Get live ranges for debugging
    const std::vector<LiveRange>& getLiveRanges() const { return liveRanges_; }
    
    // Reset for a new function
    void reset();
    
    // Set function names to skip during register allocation
    // These are function labels that should not be treated as variables
    void setFunctionNames(const std::set<std::string>* fnNames) { functionNames_ = fnNames; }
    
private:
    std::vector<LiveRange> liveRanges_;
    std::map<std::string, VarRegister> assignments_;
    std::set<VarRegister> usedRegisters_;
    int currentPos_;
    const std::set<std::string>* functionNames_ = nullptr;  // Function names to skip
    
    // Compute live ranges by scanning the AST
    void computeLiveRanges(Statement* body, const std::vector<std::pair<std::string, std::string>>& params);
    void scanStatement(Statement* stmt);
    void scanExpression(Expression* expr);
    
    // Record a use of a variable
    void recordUse(const std::string& name);
    void recordDef(const std::string& name);
    
    // Linear scan allocation
    void allocateRegisters();
};

} // namespace tyl

#endif // TYL_REGISTER_ALLOCATOR_H
