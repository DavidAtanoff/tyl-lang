// Tyl Compiler - Instruction Scheduler Implementation
// Reorders instructions to hide latencies and improve pipeline utilization
#include "instruction_scheduler.h"
#include <algorithm>
#include <queue>

namespace tyl {

// ============================================
// Instruction Scheduler Pass
// ============================================

void InstructionSchedulerPass::run(Program& ast) {
    transformations_ = 0;
    processStatements(ast.statements);
}

void InstructionSchedulerPass::processStatements(std::vector<StmtPtr>& stmts) {
    // Build dependency graph
    std::vector<ScheduleNode> nodes;
    buildDependencyGraph(stmts, nodes);
    
    // Schedule if we have enough statements to benefit
    if (nodes.size() >= 3) {
        scheduleStatements(stmts, nodes);
    }
    
    // Process nested blocks
    for (auto& stmt : stmts) {
        if (auto* block = dynamic_cast<Block*>(stmt.get())) {
            processStatements(block->statements);
        }
        else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
            if (auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
                processStatements(thenBlock->statements);
            }
            for (auto& elif : ifStmt->elifBranches) {
                if (auto* elifBlock = dynamic_cast<Block*>(elif.second.get())) {
                    processStatements(elifBlock->statements);
                }
            }
            if (auto* elseBlock = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
                processStatements(elseBlock->statements);
            }
        }
        else if (auto* forLoop = dynamic_cast<ForStmt*>(stmt.get())) {
            if (auto* body = dynamic_cast<Block*>(forLoop->body.get())) {
                processStatements(body->statements);
            }
        }
        else if (auto* whileLoop = dynamic_cast<WhileStmt*>(stmt.get())) {
            if (auto* body = dynamic_cast<Block*>(whileLoop->body.get())) {
                processStatements(body->statements);
            }
        }
        else if (auto* fnDecl = dynamic_cast<FnDecl*>(stmt.get())) {
            if (auto* body = dynamic_cast<Block*>(fnDecl->body.get())) {
                processStatements(body->statements);
            }
        }
    }
}

void InstructionSchedulerPass::buildDependencyGraph(std::vector<StmtPtr>& stmts,
                                                     std::vector<ScheduleNode>& nodes) {
    nodes.clear();
    nodes.reserve(stmts.size());
    
    // Create nodes for each statement
    for (size_t i = 0; i < stmts.size(); ++i) {
        ScheduleNode node;
        node.stmt = stmts[i].get();
        node.originalIndex = static_cast<int>(i);
        node.hasSideEffects = false;
        node.latency = getStatementLatency(stmts[i].get());
        node.earliestStart = 0;
        
        analyzeStatement(stmts[i].get(), node);
        nodes.push_back(node);
    }
    
    // Build dependency edges
    for (size_t i = 0; i < nodes.size(); ++i) {
        for (size_t j = i + 1; j < nodes.size(); ++j) {
            DependencyType dep = checkDependency(nodes[i], nodes[j]);
            if (dep != DependencyType::NONE) {
                nodes[i].successors.push_back(static_cast<int>(j));
                nodes[j].predecessors.push_back(static_cast<int>(i));
            }
        }
    }
    
    // Calculate priorities (critical path length)
    for (auto& node : nodes) {
        node.priority = calculatePriority(node, nodes);
    }
}

void InstructionSchedulerPass::analyzeStatement(Statement* stmt, ScheduleNode& node) {
    if (!stmt) return;
    
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        node.writes.insert(varDecl->name);
        if (varDecl->initializer) {
            analyzeExpression(varDecl->initializer.get(), node.reads);
        }
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        if (auto* ident = dynamic_cast<Identifier*>(assignStmt->target.get())) {
            node.writes.insert(ident->name);
            // For compound assignment, also read the variable
            if (assignStmt->op != TokenType::ASSIGN) {
                node.reads.insert(ident->name);
            }
        }
        analyzeExpression(assignStmt->value.get(), node.reads);
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        analyzeExpression(exprStmt->expr.get(), node.reads);
        
        // Check for AssignExpr (compound assignments like sum += i)
        if (auto* assignExpr = dynamic_cast<AssignExpr*>(exprStmt->expr.get())) {
            if (auto* ident = dynamic_cast<Identifier*>(assignExpr->target.get())) {
                node.writes.insert(ident->name);
                // For compound assignment, also read the variable
                if (assignExpr->op != TokenType::ASSIGN) {
                    node.reads.insert(ident->name);
                }
            }
            analyzeExpression(assignExpr->value.get(), node.reads);
        }
        
        // Check for side effects (function calls)
        if (dynamic_cast<CallExpr*>(exprStmt->expr.get())) {
            node.hasSideEffects = true;
        }
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        if (returnStmt->value) {
            analyzeExpression(returnStmt->value.get(), node.reads);
        }
        node.hasSideEffects = true;  // Return is a control flow change
    }
    else if (dynamic_cast<IfStmt*>(stmt) || 
             dynamic_cast<ForStmt*>(stmt) || 
             dynamic_cast<WhileStmt*>(stmt)) {
        // Control flow statements have side effects and can't be reordered
        node.hasSideEffects = true;
    }
}

void InstructionSchedulerPass::analyzeExpression(Expression* expr, std::set<std::string>& reads) {
    if (!expr) return;
    
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        reads.insert(ident->name);
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        analyzeExpression(binary->left.get(), reads);
        analyzeExpression(binary->right.get(), reads);
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        analyzeExpression(unary->operand.get(), reads);
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        analyzeExpression(call->callee.get(), reads);
        for (auto& arg : call->args) {
            analyzeExpression(arg.get(), reads);
        }
    }
    else if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        analyzeExpression(index->object.get(), reads);
        analyzeExpression(index->index.get(), reads);
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        analyzeExpression(ternary->condition.get(), reads);
        analyzeExpression(ternary->thenExpr.get(), reads);
        analyzeExpression(ternary->elseExpr.get(), reads);
    }
}

DependencyType InstructionSchedulerPass::checkDependency(const ScheduleNode& from, 
                                                          const ScheduleNode& to) {
    // Side effects create dependencies
    if (from.hasSideEffects || to.hasSideEffects) {
        return DependencyType::RAW;  // Treat as true dependency
    }
    
    // RAW: to reads something from writes
    for (const auto& w : from.writes) {
        if (to.reads.count(w)) {
            return DependencyType::RAW;
        }
    }
    
    // WAW: both write to same variable
    for (const auto& w : from.writes) {
        if (to.writes.count(w)) {
            return DependencyType::WAW;
        }
    }
    
    // WAR: to writes something from reads
    for (const auto& r : from.reads) {
        if (to.writes.count(r)) {
            return DependencyType::WAR;
        }
    }
    
    return DependencyType::NONE;
}

int InstructionSchedulerPass::calculatePriority(const ScheduleNode& node,
                                                 const std::vector<ScheduleNode>& nodes) {
    // Priority = longest path to any exit (critical path)
    int maxPath = node.latency;
    
    for (int succ : node.successors) {
        int succPath = calculatePriority(nodes[succ], nodes);
        maxPath = std::max(maxPath, node.latency + succPath);
    }
    
    return maxPath;
}

int InstructionSchedulerPass::getStatementLatency(Statement* stmt) {
    if (!stmt) return 1;
    
    // Estimate latency based on statement type
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        if (varDecl->initializer) {
            // Complex expressions take longer
            if (dynamic_cast<BinaryExpr*>(varDecl->initializer.get())) {
                return 2;
            }
            if (dynamic_cast<CallExpr*>(varDecl->initializer.get())) {
                return 5;  // Function calls are expensive
            }
        }
        return 1;
    }
    
    if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        if (dynamic_cast<BinaryExpr*>(assignStmt->value.get())) {
            return 2;
        }
        if (dynamic_cast<CallExpr*>(assignStmt->value.get())) {
            return 5;
        }
        return 1;
    }
    
    if (dynamic_cast<ExprStmt*>(stmt)) {
        return 2;  // Expression statements often involve calls
    }
    
    return 1;
}

void InstructionSchedulerPass::scheduleStatements(std::vector<StmtPtr>& stmts,
                                                   std::vector<ScheduleNode>& nodes) {
    if (nodes.empty()) return;
    
    // List scheduling algorithm
    std::vector<int> scheduled;
    std::vector<bool> isScheduled(nodes.size(), false);
    std::vector<int> readyTime(nodes.size(), 0);
    
    int currentCycle = 0;
    
    while (scheduled.size() < nodes.size()) {
        // Find ready nodes (all predecessors scheduled)
        std::vector<int> ready;
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (isScheduled[i]) continue;
            
            bool allPredsScheduled = true;
            int maxPredFinish = 0;
            for (int pred : nodes[i].predecessors) {
                if (!isScheduled[pred]) {
                    allPredsScheduled = false;
                    break;
                }
                maxPredFinish = std::max(maxPredFinish, 
                    readyTime[pred] + nodes[pred].latency);
            }
            
            if (allPredsScheduled && maxPredFinish <= currentCycle) {
                ready.push_back(static_cast<int>(i));
            }
        }
        
        if (ready.empty()) {
            // No ready nodes, advance time
            currentCycle++;
            continue;
        }
        
        // Sort ready nodes by priority (highest first)
        std::sort(ready.begin(), ready.end(), [&](int a, int b) {
            return nodes[a].priority > nodes[b].priority;
        });
        
        // Schedule highest priority ready node
        int toSchedule = ready[0];
        scheduled.push_back(toSchedule);
        isScheduled[toSchedule] = true;
        readyTime[toSchedule] = currentCycle;
    }
    
    // Check if scheduling changed the order
    bool changed = false;
    for (size_t i = 0; i < scheduled.size(); ++i) {
        if (scheduled[i] != static_cast<int>(i)) {
            changed = true;
            break;
        }
    }
    
    if (changed) {
        // Reorder statements according to schedule
        std::vector<StmtPtr> newStmts;
        newStmts.reserve(stmts.size());
        for (int idx : scheduled) {
            newStmts.push_back(std::move(stmts[idx]));
        }
        stmts = std::move(newStmts);
        transformations_++;
    }
}

// ============================================
// Machine Code Scheduler
// ============================================

void MachineCodeScheduler::scheduleBlock(std::vector<uint8_t>& code, size_t start, size_t end) {
    // Machine code scheduler for x64 instructions
    // Reorders independent instructions to hide latencies and improve ILP
    
    if (end <= start || end - start < 6) return;  // Too small to schedule
    
    // Decode all instructions in the block
    std::vector<DecodedInstruction> instructions;
    size_t pos = start;
    
    while (pos < end) {
        DecodedInstruction instr;
        instr.offset = pos;
        instr.length = decodeInstructionLength(code, pos);
        if (instr.length <= 0) break;
        
        // Decode register usage
        decodeRegisterUsage(code, pos, instr);
        
        // Get latency info
        uint8_t opcode = code[pos];
        if (opcode >= 0x40 && opcode <= 0x4F && pos + 1 < code.size()) {
            opcode = code[pos + 1];  // Skip REX prefix
        }
        auto latency = getInstructionLatency(opcode);
        instr.latency = latency.latency;
        instr.throughput = latency.throughput;
        
        instructions.push_back(instr);
        pos += instr.length;
    }
    
    if (instructions.size() < 3) return;  // Not enough to schedule
    
    // Build dependency graph
    std::vector<std::vector<int>> deps(instructions.size());
    for (size_t i = 0; i < instructions.size(); ++i) {
        for (size_t j = i + 1; j < instructions.size(); ++j) {
            if (hasDataDependency(instructions[i], instructions[j])) {
                deps[j].push_back(static_cast<int>(i));
            }
        }
    }
    
    // List scheduling algorithm
    std::vector<int> schedule;
    std::vector<bool> scheduled(instructions.size(), false);
    std::vector<int> readyTime(instructions.size(), 0);
    
    int currentCycle = 0;
    while (schedule.size() < instructions.size()) {
        // Find ready instructions
        std::vector<int> ready;
        for (size_t i = 0; i < instructions.size(); ++i) {
            if (scheduled[i]) continue;
            
            bool allDepsScheduled = true;
            int maxDepFinish = 0;
            for (int dep : deps[i]) {
                if (!scheduled[dep]) {
                    allDepsScheduled = false;
                    break;
                }
                maxDepFinish = std::max(maxDepFinish, 
                    readyTime[dep] + instructions[dep].latency);
            }
            
            if (allDepsScheduled && maxDepFinish <= currentCycle) {
                ready.push_back(static_cast<int>(i));
            }
        }
        
        if (ready.empty()) {
            currentCycle++;
            continue;
        }
        
        // Sort by priority (higher latency first to expose more parallelism)
        std::sort(ready.begin(), ready.end(), [&](int a, int b) {
            return instructions[a].latency > instructions[b].latency;
        });
        
        // Schedule highest priority
        int toSchedule = ready[0];
        schedule.push_back(toSchedule);
        scheduled[toSchedule] = true;
        readyTime[toSchedule] = currentCycle;
    }
    
    // Check if schedule differs from original order
    bool changed = false;
    for (size_t i = 0; i < schedule.size(); ++i) {
        if (schedule[i] != static_cast<int>(i)) {
            changed = true;
            break;
        }
    }
    
    if (changed) {
        // Reorder the actual bytes
        std::vector<uint8_t> newCode;
        newCode.reserve(end - start);
        
        for (int idx : schedule) {
            const auto& instr = instructions[idx];
            for (size_t i = 0; i < static_cast<size_t>(instr.length); ++i) {
                newCode.push_back(code[instr.offset + i]);
            }
        }
        
        // Copy back
        for (size_t i = 0; i < newCode.size(); ++i) {
            code[start + i] = newCode[i];
        }
    }
}

void MachineCodeScheduler::decodeRegisterUsage(const std::vector<uint8_t>& code, 
                                                size_t offset, DecodedInstruction& instr) {
    if (offset >= code.size()) return;
    
    uint8_t b = code[offset];
    size_t pos = offset;
    
    // Handle REX prefix
    uint8_t rex = 0;
    if (b >= 0x40 && b <= 0x4F) {
        rex = b;
        pos++;
        if (pos >= code.size()) return;
        b = code[pos];
    }
    
    // Decode based on opcode
    switch (b) {
        case 0x50: case 0x51: case 0x52: case 0x53:  // push r64
        case 0x54: case 0x55: case 0x56: case 0x57:
            instr.regsRead.insert(b - 0x50 + ((rex & 0x01) ? 8 : 0));
            instr.regsRead.insert(4);  // RSP
            instr.regsWritten.insert(4);  // RSP
            break;
            
        case 0x58: case 0x59: case 0x5A: case 0x5B:  // pop r64
        case 0x5C: case 0x5D: case 0x5E: case 0x5F:
            instr.regsWritten.insert(b - 0x58 + ((rex & 0x01) ? 8 : 0));
            instr.regsRead.insert(4);  // RSP
            instr.regsWritten.insert(4);  // RSP
            break;
            
        case 0x89: case 0x8B:  // mov r/m, r or mov r, r/m
            if (pos + 1 < code.size()) {
                uint8_t modrm = code[pos + 1];
                int reg = (modrm >> 3) & 7;
                int rm = modrm & 7;
                if (rex & 0x04) reg += 8;  // REX.R
                if (rex & 0x01) rm += 8;   // REX.B
                
                if (b == 0x89) {  // mov r/m, r
                    instr.regsRead.insert(reg);
                    if ((modrm >> 6) == 3) {  // Register mode
                        instr.regsWritten.insert(rm);
                    } else {
                        instr.writesMemory = true;
                    }
                } else {  // mov r, r/m
                    instr.regsWritten.insert(reg);
                    if ((modrm >> 6) == 3) {
                        instr.regsRead.insert(rm);
                    } else {
                        instr.readsMemory = true;
                    }
                }
            }
            break;
            
        case 0x01: case 0x03:  // add
        case 0x29: case 0x2B:  // sub
        case 0x21: case 0x23:  // and
        case 0x09: case 0x0B:  // or
        case 0x31: case 0x33:  // xor
            if (pos + 1 < code.size()) {
                uint8_t modrm = code[pos + 1];
                int reg = (modrm >> 3) & 7;
                int rm = modrm & 7;
                if (rex & 0x04) reg += 8;
                if (rex & 0x01) rm += 8;
                
                instr.regsRead.insert(reg);
                if ((modrm >> 6) == 3) {
                    instr.regsRead.insert(rm);
                    instr.regsWritten.insert(rm);
                } else {
                    instr.readsMemory = true;
                    instr.writesMemory = true;
                }
            }
            break;
            
        default:
            // For unknown instructions, be conservative
            instr.readsMemory = true;
            instr.writesMemory = true;
            break;
    }
}

bool MachineCodeScheduler::hasDataDependency(const DecodedInstruction& a, 
                                              const DecodedInstruction& b) {
    // RAW: b reads something a writes
    for (int reg : a.regsWritten) {
        if (b.regsRead.count(reg)) return true;
    }
    
    // WAW: both write to same register
    for (int reg : a.regsWritten) {
        if (b.regsWritten.count(reg)) return true;
    }
    
    // WAR: b writes something a reads
    for (int reg : a.regsRead) {
        if (b.regsWritten.count(reg)) return true;
    }
    
    // Memory dependencies (conservative)
    if ((a.writesMemory && b.readsMemory) ||
        (a.writesMemory && b.writesMemory) ||
        (a.readsMemory && b.writesMemory)) {
        return true;
    }
    
    return false;
}

int MachineCodeScheduler::decodeInstructionLength(const std::vector<uint8_t>& code, size_t offset) {
    if (offset >= code.size()) return -1;
    
    uint8_t b = code[offset];
    
    // Simple length decoding for common x64 instructions
    // This is not complete - a full decoder would be much more complex
    
    // REX prefix
    bool hasRex = (b >= 0x40 && b <= 0x4F);
    if (hasRex) {
        if (offset + 1 >= code.size()) return -1;
        b = code[offset + 1];
    }
    
    int baseLen = hasRex ? 1 : 0;
    
    // Common instruction patterns
    switch (b) {
        case 0x50: case 0x51: case 0x52: case 0x53:  // push r64
        case 0x54: case 0x55: case 0x56: case 0x57:
        case 0x58: case 0x59: case 0x5A: case 0x5B:  // pop r64
        case 0x5C: case 0x5D: case 0x5E: case 0x5F:
            return baseLen + 1;
            
        case 0x90:  // nop
        case 0xC3:  // ret
        case 0xCC:  // int3
            return baseLen + 1;
            
        case 0xB8: case 0xB9: case 0xBA: case 0xBB:  // mov r32, imm32
        case 0xBC: case 0xBD: case 0xBE: case 0xBF:
            return baseLen + 5;
            
        case 0x89: case 0x8B:  // mov r/m, r or mov r, r/m
            // Need to decode ModR/M byte
            if (offset + baseLen + 1 >= code.size()) return -1;
            {
                uint8_t modrm = code[offset + baseLen + 1];
                int mod = (modrm >> 6) & 3;
                int rm = modrm & 7;
                int len = baseLen + 2;
                
                if (mod == 0 && rm == 5) len += 4;  // disp32
                else if (mod == 1) len += 1;        // disp8
                else if (mod == 2) len += 4;        // disp32
                
                if (rm == 4 && mod != 3) len += 1;  // SIB byte
                
                return len;
            }
            
        case 0x0F:  // Two-byte opcode
            if (offset + baseLen + 1 >= code.size()) return -1;
            {
                uint8_t b2 = code[offset + baseLen + 1];
                // Conditional jumps
                if (b2 >= 0x80 && b2 <= 0x8F) {
                    return baseLen + 6;  // jcc rel32
                }
                // setcc
                if (b2 >= 0x90 && b2 <= 0x9F) {
                    return baseLen + 3;  // setcc r/m8
                }
                return baseLen + 3;  // Default for 0F xx
            }
            
        case 0xE8:  // call rel32
        case 0xE9:  // jmp rel32
            return baseLen + 5;
            
        case 0xEB:  // jmp rel8
            return baseLen + 2;
            
        default:
            // Default: assume 3 bytes (common for many instructions)
            return baseLen + 3;
    }
}

bool MachineCodeScheduler::canReorder(const std::vector<uint8_t>& code, size_t i1, size_t i2) {
    // Very conservative check - only reorder if we're sure it's safe
    // This would need full register/memory dependency analysis
    
    if (i1 >= code.size() || i2 >= code.size()) return false;
    
    uint8_t b1 = code[i1];
    uint8_t b2 = code[i2];
    
    // Skip REX prefixes
    if (b1 >= 0x40 && b1 <= 0x4F && i1 + 1 < code.size()) b1 = code[i1 + 1];
    if (b2 >= 0x40 && b2 <= 0x4F && i2 + 1 < code.size()) b2 = code[i2 + 1];
    
    // Don't reorder control flow instructions
    if (b1 == 0xE8 || b1 == 0xE9 || b1 == 0xEB || b1 == 0xC3) return false;
    if (b2 == 0xE8 || b2 == 0xE9 || b2 == 0xEB || b2 == 0xC3) return false;
    
    // Don't reorder push/pop (stack operations)
    if ((b1 >= 0x50 && b1 <= 0x5F) || (b2 >= 0x50 && b2 <= 0x5F)) return false;
    
    // For now, be very conservative
    return false;
}

InstructionLatency MachineCodeScheduler::getInstructionLatency(uint8_t opcode) {
    // Approximate latencies for modern x64 CPUs (Intel Skylake-ish)
    switch (opcode) {
        // Simple ALU operations: 1 cycle latency, 0.25 throughput
        case 0x01: case 0x03:  // add
        case 0x29: case 0x2B:  // sub
        case 0x21: case 0x23:  // and
        case 0x09: case 0x0B:  // or
        case 0x31: case 0x33:  // xor
            return {1, 1};
            
        // Multiply: 3-4 cycle latency
        case 0x0F:  // imul (with 0FAF)
            return {3, 1};
            
        // Division: 20-80+ cycles
        case 0xF7:  // div/idiv
            return {30, 30};
            
        // Memory operations: 4-5 cycles for L1 hit
        case 0x89: case 0x8B:  // mov r/m
            return {4, 1};
            
        // Jumps: 1 cycle (predicted)
        case 0xE9: case 0xEB:  // jmp
        case 0xE8:             // call
            return {1, 1};
            
        default:
            return {1, 1};  // Default
    }
}

} // namespace tyl
