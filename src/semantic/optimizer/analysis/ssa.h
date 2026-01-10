// Tyl Compiler - SSA (Static Single Assignment) Form
// Converts AST to SSA form for advanced optimizations
#ifndef TYL_SSA_H
#define TYL_SSA_H

#include "frontend/ast/ast.h"
#include <map>
#include <set>
#include <vector>
#include <string>
#include <memory>
#include <optional>

namespace tyl {

// Forward declarations
struct SSAValue;
struct SSAInstruction;
struct SSABasicBlock;
struct SSAFunction;
struct SSAModule;

using SSAValuePtr = std::shared_ptr<SSAValue>;
using SSAInstrPtr = std::unique_ptr<SSAInstruction>;
using SSABlockPtr = std::unique_ptr<SSABasicBlock>;

// SSA Value types
enum class SSAType {
    VOID,
    INT,
    FLOAT,
    BOOL,
    STRING,
    POINTER
};

// SSA Value - represents a single-assignment value
struct SSAValue {
    int id;                     // Unique value ID (v0, v1, v2, ...)
    SSAType type;
    std::string name;           // Original variable name (for debugging)
    int version;                // SSA version number
    SSAInstruction* defInstr;   // Instruction that defines this value
    
    SSAValue(int id, SSAType type, const std::string& name = "", int version = 0)
        : id(id), type(type), name(name), version(version), defInstr(nullptr) {}
    
    std::string toString() const;
};

// SSA Instruction opcodes
enum class SSAOpcode {
    // Constants
    CONST_INT,
    CONST_FLOAT,
    CONST_BOOL,
    CONST_STRING,
    
    // Arithmetic
    ADD,
    SUB,
    MUL,
    DIV,
    MOD,
    NEG,
    
    // Comparison
    EQ,
    NE,
    LT,
    GT,
    LE,
    GE,
    
    // Logical
    AND,
    OR,
    NOT,
    
    // Control flow
    PHI,            // Phi function for merging values
    BRANCH,         // Conditional branch
    JUMP,           // Unconditional jump
    RETURN,
    
    // Memory
    LOAD,           // Load from memory/variable
    STORE,          // Store to memory/variable
    ALLOCA,         // Stack allocation
    
    // Function
    CALL,
    PARAM,          // Function parameter
    
    // Type conversion
    INT_TO_FLOAT,
    FLOAT_TO_INT,
    
    // Special
    COPY,           // Simple copy (for coalescing)
    NOP
};

// SSA Instruction
struct SSAInstruction {
    SSAOpcode opcode;
    SSAValuePtr result;                     // Result value (null for void ops)
    std::vector<SSAValuePtr> operands;      // Input operands
    SSABasicBlock* parent;                  // Parent basic block
    
    // For constants
    int64_t intValue;
    double floatValue;
    bool boolValue;
    std::string stringValue;
    
    // For branches
    SSABasicBlock* trueTarget;
    SSABasicBlock* falseTarget;
    
    // For calls
    std::string funcName;
    
    // For phi nodes
    std::vector<std::pair<SSABasicBlock*, SSAValuePtr>> phiOperands;
    
    SSAInstruction(SSAOpcode op) : opcode(op), parent(nullptr), 
        intValue(0), floatValue(0.0), boolValue(false),
        trueTarget(nullptr), falseTarget(nullptr) {}
    
    std::string toString() const;
    bool isPhi() const { return opcode == SSAOpcode::PHI; }
    bool isTerminator() const;
    bool hasSideEffects() const;
};

// SSA Basic Block
struct SSABasicBlock {
    int id;
    std::string label;
    std::vector<SSAInstrPtr> instructions;
    std::vector<SSABasicBlock*> predecessors;
    std::vector<SSABasicBlock*> successors;
    SSAFunction* parent;
    
    // Dominance info (computed later)
    SSABasicBlock* immediateDominator;
    std::set<SSABasicBlock*> dominanceFrontier;
    
    SSABasicBlock(int id, const std::string& label = "")
        : id(id), label(label), parent(nullptr), immediateDominator(nullptr) {}
    
    void addInstruction(SSAInstrPtr instr);
    SSAInstruction* getTerminator();
    bool isEmpty() const { return instructions.empty(); }
    std::string toString() const;
};

// SSA Function
struct SSAFunction {
    std::string name;
    std::vector<SSAValuePtr> params;
    SSAType returnType;
    std::vector<SSABlockPtr> blocks;
    SSABasicBlock* entryBlock;
    SSAModule* parent;
    
    // Value numbering
    int nextValueId;
    int nextBlockId;
    
    SSAFunction(const std::string& name) 
        : name(name), returnType(SSAType::VOID), entryBlock(nullptr), 
          parent(nullptr), nextValueId(0), nextBlockId(0) {}
    
    SSAValuePtr createValue(SSAType type, const std::string& name = "");
    SSABasicBlock* createBlock(const std::string& label = "");
    std::string toString() const;
};

// SSA Module (entire program)
struct SSAModule {
    std::vector<std::unique_ptr<SSAFunction>> functions;
    std::map<std::string, SSAValuePtr> globals;
    
    // String constants pool
    std::map<std::string, int> stringPool;
    int nextStringId;
    
    SSAModule() : nextStringId(0) {}
    
    SSAFunction* createFunction(const std::string& name);
    SSAFunction* getFunction(const std::string& name);
    int addString(const std::string& str);
    std::string toString() const;
};

// SSA Builder - converts AST to SSA form
class SSABuilder {
public:
    SSABuilder();
    
    // Convert entire program to SSA
    std::unique_ptr<SSAModule> build(Program& ast);
    
private:
    SSAModule* module_;
    SSAFunction* currentFunc_;
    SSABasicBlock* currentBlock_;
    
    // Variable versioning for SSA construction
    std::map<std::string, std::vector<SSAValuePtr>> varVersions_;
    std::map<std::string, int> varCounter_;
    
    // Incomplete phi nodes (for later sealing)
    std::map<SSABasicBlock*, std::map<std::string, SSAInstruction*>> incompletePhis_;
    std::set<SSABasicBlock*> sealedBlocks_;
    
    // Build helpers
    void buildFunction(FnDecl& fn);
    void buildStatement(Statement* stmt);
    SSAValuePtr buildExpression(Expression* expr);
    
    // SSA construction (Braun et al. algorithm)
    void writeVariable(const std::string& name, SSABasicBlock* block, SSAValuePtr value);
    SSAValuePtr readVariable(const std::string& name, SSABasicBlock* block);
    SSAValuePtr readVariableRecursive(const std::string& name, SSABasicBlock* block);
    SSAValuePtr addPhiOperands(const std::string& name, SSAInstruction* phi);
    SSAValuePtr tryRemoveTrivialPhi(SSAInstruction* phi);
    void sealBlock(SSABasicBlock* block);
    
    // Helpers
    SSAValuePtr emitBinary(SSAOpcode op, SSAValuePtr left, SSAValuePtr right);
    SSAValuePtr emitUnary(SSAOpcode op, SSAValuePtr operand);
    SSAValuePtr emitCall(const std::string& name, const std::vector<SSAValuePtr>& args);
    void emitBranch(SSAValuePtr cond, SSABasicBlock* trueBlock, SSABasicBlock* falseBlock);
    void emitJump(SSABasicBlock* target);
    void emitReturn(SSAValuePtr value);
    
    SSAType getExprType(Expression* expr);
};

// SSA Optimizer - performs optimizations on SSA form
class SSAOptimizer {
public:
    void optimize(SSAModule& module);
    
    // Individual optimization passes
    void deadCodeElimination(SSAFunction& func);
    void constantPropagation(SSAFunction& func);
    void copyPropagation(SSAFunction& func);
    void commonSubexpressionElimination(SSAFunction& func);
    
private:
    bool isInstructionDead(SSAInstruction* instr);
    std::optional<int64_t> tryEvalConstant(SSAInstruction* instr);
};

// SSA to AST converter (for backends that don't use SSA directly)
class SSAToAST {
public:
    std::unique_ptr<Program> convert(SSAModule& module);
    
private:
    std::map<int, std::string> valueNames_;
    int tempCounter_;
    
    StmtPtr convertFunction(SSAFunction& func);
    StmtPtr convertBlock(SSABasicBlock& block);
    StmtPtr convertInstruction(SSAInstruction& instr);
    ExprPtr convertValue(SSAValuePtr value);
    
    // Get or create a name for an SSA value
    std::string getValueName(SSAValuePtr value);
    
    // Convert SSA type to string
    std::string ssaTypeToString(SSAType type);
    
    // Convert SSA opcode to token type
    TokenType ssaOpcodeToToken(SSAOpcode op);
};

} // namespace tyl

#endif // TYL_SSA_H
