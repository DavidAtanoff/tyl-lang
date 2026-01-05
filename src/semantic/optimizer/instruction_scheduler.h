// Tyl Compiler - Instruction Scheduler
// Reorders instructions to hide latencies and improve pipeline utilization
#ifndef TYL_INSTRUCTION_SCHEDULER_H
#define TYL_INSTRUCTION_SCHEDULER_H

#include "optimizer.h"
#include "frontend/ast/ast.h"
#include <vector>
#include <set>
#include <map>

namespace tyl {

// Instruction latency information for x64
struct InstructionLatency {
    int latency;        // Cycles to produce result
    int throughput;     // Cycles between issue of same instruction
};

// Dependency types between instructions
enum class DependencyType {
    RAW,    // Read After Write (true dependency)
    WAR,    // Write After Read (anti-dependency)
    WAW,    // Write After Write (output dependency)
    NONE
};

// Information about a schedulable unit (statement)
struct ScheduleNode {
    Statement* stmt;
    int originalIndex;
    int priority;           // Higher = schedule earlier
    int earliestStart;      // Earliest cycle this can start
    int latency;            // Cycles to complete
    
    std::set<std::string> reads;    // Variables read
    std::set<std::string> writes;   // Variables written
    bool hasSideEffects;            // Calls, I/O, etc.
    
    std::vector<int> predecessors;  // Nodes this depends on
    std::vector<int> successors;    // Nodes that depend on this
};

// Instruction Scheduling Pass
// Reorders independent instructions to improve ILP (Instruction Level Parallelism)
class InstructionSchedulerPass : public OptimizationPass {
public:
    void run(Program& ast) override;
    std::string name() const override { return "InstructionScheduler"; }
    
    // Configuration
    void setAggressiveScheduling(bool aggressive) { aggressive_ = aggressive; }
    
private:
    bool aggressive_ = false;
    
    // Build dependency graph for a block of statements
    void buildDependencyGraph(std::vector<StmtPtr>& stmts, 
                              std::vector<ScheduleNode>& nodes);
    
    // Schedule statements using list scheduling algorithm
    void scheduleStatements(std::vector<StmtPtr>& stmts,
                           std::vector<ScheduleNode>& nodes);
    
    // Analyze a statement for reads/writes
    void analyzeStatement(Statement* stmt, ScheduleNode& node);
    void analyzeExpression(Expression* expr, std::set<std::string>& reads);
    
    // Check dependency between two nodes
    DependencyType checkDependency(const ScheduleNode& from, const ScheduleNode& to);
    
    // Calculate priority for scheduling
    int calculatePriority(const ScheduleNode& node, 
                         const std::vector<ScheduleNode>& nodes);
    
    // Get latency for a statement
    int getStatementLatency(Statement* stmt);
    
    // Process statements recursively
    void processStatements(std::vector<StmtPtr>& stmts);
};

// Post-codegen instruction scheduler (operates on machine code)
// This is a more advanced scheduler that works on the generated x64 code
class MachineCodeScheduler {
public:
    // Schedule instructions in a basic block
    void scheduleBlock(std::vector<uint8_t>& code, size_t start, size_t end);
    
    // Get latency for an x64 instruction
    static InstructionLatency getInstructionLatency(uint8_t opcode);
    
private:
    // Decode instruction length
    int decodeInstructionLength(const std::vector<uint8_t>& code, size_t offset);
    
    // Check if instruction can be reordered
    bool canReorder(const std::vector<uint8_t>& code, size_t i1, size_t i2);
};

} // namespace tyl

#endif // TYL_INSTRUCTION_SCHEDULER_H
