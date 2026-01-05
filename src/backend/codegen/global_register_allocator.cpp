// Tyl Compiler - Global Register Allocator Implementation
// Graph coloring register allocation for top-level variables

#include "global_register_allocator.h"
#include <algorithm>
#include <queue>

namespace tyl {

GlobalRegisterAllocator::GlobalRegisterAllocator() : nextStackOffset_(-8) {}

void GlobalRegisterAllocator::reset() {
    globalVars_.clear();
    interferenceGraph_.clear();
    usedRegisters_.clear();
    nextStackOffset_ = -8;
}

void GlobalRegisterAllocator::analyze(Program& program) {
    reset();
    
    // Phase 1: Collect all global variables
    collectGlobalVariables(program);
    
    // Phase 2: Analyze usage patterns
    analyzeVariableUsage(program);
    
    // Phase 3: Build interference graph
    buildInterferenceGraph(program);
    
    // Phase 4: Color the graph (assign registers)
    colorGraph();
    
    // Phase 5: Assign stack slots for spilled variables
    assignStackSlots();
}

VarRegister GlobalRegisterAllocator::getGlobalRegister(const std::string& name) const {
    auto it = globalVars_.find(name);
    if (it != globalVars_.end()) {
        return it->second.assignedReg;
    }
    return VarRegister::NONE;
}

bool GlobalRegisterAllocator::isGlobalInRegister(const std::string& name) const {
    return getGlobalRegister(name) != VarRegister::NONE;
}

bool GlobalRegisterAllocator::getConstantValue(const std::string& name, int64_t& outValue) const {
    auto it = globalVars_.find(name);
    if (it != globalVars_.end() && it->second.isConstant) {
        outValue = it->second.constValue;
        return true;
    }
    return false;
}

std::vector<VarRegister> GlobalRegisterAllocator::getUsedGlobalRegisters() const {
    std::vector<VarRegister> result;
    for (auto reg : usedRegisters_) {
        result.push_back(reg);
    }
    return result;
}

int32_t GlobalRegisterAllocator::getGlobalStackOffset(const std::string& name) const {
    auto it = globalVars_.find(name);
    if (it != globalVars_.end()) {
        return it->second.stackOffset;
    }
    return 0;
}

void GlobalRegisterAllocator::collectGlobalVariables(Program& program) {
    for (auto& stmt : program.statements) {
        // Skip function declarations
        if (dynamic_cast<FnDecl*>(stmt.get())) continue;
        
        if (auto* varDecl = dynamic_cast<VarDecl*>(stmt.get())) {
            GlobalVarInfo info;
            info.name = varDecl->name;
            info.defCount = 1;
            
            // Check if it's a constant
            if (varDecl->isConst && varDecl->initializer) {
                int64_t value;
                if (tryEvalConstant(varDecl->initializer.get(), value)) {
                    info.isConstant = true;
                    info.constValue = value;
                }
            }
            
            globalVars_[varDecl->name] = info;
        }
    }
}

void GlobalRegisterAllocator::analyzeVariableUsage(Program& program) {
    // Scan all statements for variable uses
    std::function<void(Statement*, bool)> scanStmt = [&](Statement* stmt, bool inFunction) {
        if (!stmt) return;
        
        if (auto* block = dynamic_cast<Block*>(stmt)) {
            for (auto& s : block->statements) {
                scanStmt(s.get(), inFunction);
            }
        }
        else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
            if (varDecl->initializer) {
                std::set<std::string> uses;
                scanExpressionForUses(varDecl->initializer.get(), uses);
                for (const auto& name : uses) {
                    auto it = globalVars_.find(name);
                    if (it != globalVars_.end()) {
                        it->second.useCount++;
                        if (inFunction) it->second.isUsedInFunctions = true;
                    }
                }
            }
        }
        else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
            if (auto* id = dynamic_cast<Identifier*>(assignStmt->target.get())) {
                auto it = globalVars_.find(id->name);
                if (it != globalVars_.end()) {
                    it->second.defCount++;
                    if (inFunction) it->second.isUsedInFunctions = true;
                }
            }
            std::set<std::string> uses;
            scanExpressionForUses(assignStmt->value.get(), uses);
            for (const auto& name : uses) {
                auto it = globalVars_.find(name);
                if (it != globalVars_.end()) {
                    it->second.useCount++;
                    if (inFunction) it->second.isUsedInFunctions = true;
                }
            }
        }
        else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
            std::set<std::string> uses;
            scanExpressionForUses(exprStmt->expr.get(), uses);
            for (const auto& name : uses) {
                auto it = globalVars_.find(name);
                if (it != globalVars_.end()) {
                    it->second.useCount++;
                    if (inFunction) it->second.isUsedInFunctions = true;
                }
            }
        }
        else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
            std::set<std::string> uses;
            scanExpressionForUses(ifStmt->condition.get(), uses);
            for (const auto& name : uses) {
                auto it = globalVars_.find(name);
                if (it != globalVars_.end()) {
                    it->second.useCount++;
                    if (inFunction) it->second.isUsedInFunctions = true;
                }
            }
            scanStmt(ifStmt->thenBranch.get(), inFunction);
            for (auto& elif : ifStmt->elifBranches) {
                scanExpressionForUses(elif.first.get(), uses);
                scanStmt(elif.second.get(), inFunction);
            }
            scanStmt(ifStmt->elseBranch.get(), inFunction);
        }
        else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
            std::set<std::string> uses;
            scanExpressionForUses(whileStmt->condition.get(), uses);
            for (const auto& name : uses) {
                auto it = globalVars_.find(name);
                if (it != globalVars_.end()) {
                    it->second.useCount++;
                    if (inFunction) it->second.isUsedInFunctions = true;
                }
            }
            scanStmt(whileStmt->body.get(), inFunction);
        }
        else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
            std::set<std::string> uses;
            scanExpressionForUses(forStmt->iterable.get(), uses);
            for (const auto& name : uses) {
                auto it = globalVars_.find(name);
                if (it != globalVars_.end()) {
                    it->second.useCount++;
                    if (inFunction) it->second.isUsedInFunctions = true;
                }
            }
            scanStmt(forStmt->body.get(), inFunction);
        }
        else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
            if (returnStmt->value) {
                std::set<std::string> uses;
                scanExpressionForUses(returnStmt->value.get(), uses);
                for (const auto& name : uses) {
                    auto it = globalVars_.find(name);
                    if (it != globalVars_.end()) {
                        it->second.useCount++;
                        if (inFunction) it->second.isUsedInFunctions = true;
                    }
                }
            }
        }
        else if (auto* fnDecl = dynamic_cast<FnDecl*>(stmt)) {
            scanStmt(fnDecl->body.get(), true);
        }
    };
    
    for (auto& stmt : program.statements) {
        bool inFunction = dynamic_cast<FnDecl*>(stmt.get()) != nullptr;
        scanStmt(stmt.get(), inFunction);
    }
}

void GlobalRegisterAllocator::scanExpressionForUses(Expression* expr, std::set<std::string>& uses) {
    if (!expr) return;
    
    if (auto* id = dynamic_cast<Identifier*>(expr)) {
        uses.insert(id->name);
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        scanExpressionForUses(binary->left.get(), uses);
        scanExpressionForUses(binary->right.get(), uses);
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        scanExpressionForUses(unary->operand.get(), uses);
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        // Don't scan the callee - it's a function name, not a variable
        // Only scan the arguments
        for (auto& arg : call->args) {
            scanExpressionForUses(arg.get(), uses);
        }
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        scanExpressionForUses(ternary->condition.get(), uses);
        scanExpressionForUses(ternary->thenExpr.get(), uses);
        scanExpressionForUses(ternary->elseExpr.get(), uses);
    }
    else if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        scanExpressionForUses(index->object.get(), uses);
        scanExpressionForUses(index->index.get(), uses);
    }
    else if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        scanExpressionForUses(member->object.get(), uses);
    }
}

void GlobalRegisterAllocator::buildInterferenceGraph(Program& program) {
    // Initialize interference graph nodes
    for (auto& [name, info] : globalVars_) {
        if (info.isConstant) continue;  // Constants don't need registers
        
        InterferenceNode node;
        node.varName = name;
        node.spillCost = info.useCount + info.defCount * 2;  // Defs are more expensive
        interferenceGraph_[name] = node;
    }
    
    // Build interference by scanning live ranges
    // Two variables interfere if they are both live at the same point
    std::set<std::string> currentlyLive;
    
    std::function<void(Statement*)> scanStmt = [&](Statement* stmt) {
        if (!stmt) return;
        
        if (auto* block = dynamic_cast<Block*>(stmt)) {
            for (auto& s : block->statements) {
                scanStmt(s.get());
            }
        }
        else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
            // Variable becomes live after definition
            if (globalVars_.count(varDecl->name) && !globalVars_[varDecl->name].isConstant) {
                // Add interference with all currently live variables
                for (const auto& live : currentlyLive) {
                    addInterference(varDecl->name, live);
                }
                currentlyLive.insert(varDecl->name);
            }
            
            // Process initializer
            if (varDecl->initializer) {
                std::set<std::string> uses;
                scanExpressionForUses(varDecl->initializer.get(), uses);
                for (const auto& use : uses) {
                    if (globalVars_.count(use) && !globalVars_[use].isConstant) {
                        currentlyLive.insert(use);
                    }
                }
            }
        }
        else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
            // Process value first (uses)
            std::set<std::string> uses;
            scanExpressionForUses(assignStmt->value.get(), uses);
            for (const auto& use : uses) {
                if (globalVars_.count(use) && !globalVars_[use].isConstant) {
                    currentlyLive.insert(use);
                }
            }
            
            // Then process target (def)
            if (auto* id = dynamic_cast<Identifier*>(assignStmt->target.get())) {
                if (globalVars_.count(id->name) && !globalVars_[id->name].isConstant) {
                    for (const auto& live : currentlyLive) {
                        if (live != id->name) {
                            addInterference(id->name, live);
                        }
                    }
                }
            }
        }
        else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
            std::set<std::string> uses;
            scanExpressionForUses(exprStmt->expr.get(), uses);
            for (const auto& use : uses) {
                if (globalVars_.count(use) && !globalVars_[use].isConstant) {
                    currentlyLive.insert(use);
                }
            }
        }
        else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
            std::set<std::string> uses;
            scanExpressionForUses(ifStmt->condition.get(), uses);
            for (const auto& use : uses) {
                if (globalVars_.count(use) && !globalVars_[use].isConstant) {
                    currentlyLive.insert(use);
                }
            }
            
            auto savedLive = currentlyLive;
            scanStmt(ifStmt->thenBranch.get());
            auto thenLive = currentlyLive;
            
            currentlyLive = savedLive;
            for (auto& elif : ifStmt->elifBranches) {
                scanExpressionForUses(elif.first.get(), uses);
                scanStmt(elif.second.get());
            }
            
            if (ifStmt->elseBranch) {
                currentlyLive = savedLive;
                scanStmt(ifStmt->elseBranch.get());
            }
            
            // Merge live sets
            currentlyLive.insert(thenLive.begin(), thenLive.end());
        }
        else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
            std::set<std::string> uses;
            scanExpressionForUses(whileStmt->condition.get(), uses);
            for (const auto& use : uses) {
                if (globalVars_.count(use) && !globalVars_[use].isConstant) {
                    currentlyLive.insert(use);
                }
            }
            scanStmt(whileStmt->body.get());
        }
        else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
            std::set<std::string> uses;
            scanExpressionForUses(forStmt->iterable.get(), uses);
            for (const auto& use : uses) {
                if (globalVars_.count(use) && !globalVars_[use].isConstant) {
                    currentlyLive.insert(use);
                }
            }
            scanStmt(forStmt->body.get());
        }
    };
    
    for (auto& stmt : program.statements) {
        if (!dynamic_cast<FnDecl*>(stmt.get())) {
            scanStmt(stmt.get());
        }
    }
    
    // Update degrees
    for (auto& [name, node] : interferenceGraph_) {
        node.degree = (int)node.neighbors.size();
    }
}

void GlobalRegisterAllocator::addInterference(const std::string& var1, const std::string& var2) {
    if (var1 == var2) return;
    if (!interferenceGraph_.count(var1) || !interferenceGraph_.count(var2)) return;
    
    interferenceGraph_[var1].neighbors.insert(var2);
    interferenceGraph_[var2].neighbors.insert(var1);
}

void GlobalRegisterAllocator::colorGraph() {
    // Graph coloring using simplify-select algorithm
    std::vector<std::string> colorOrder = simplifyGraph();
    selectColors(colorOrder);
}

std::vector<std::string> GlobalRegisterAllocator::simplifyGraph() {
    // Available registers for globals (callee-saved)
    const int K = 5;  // RBX, R12, R13, R14, R15
    
    std::vector<std::string> order;
    std::set<std::string> removed;
    std::map<std::string, InterferenceNode> workGraph = interferenceGraph_;
    
    while (removed.size() < interferenceGraph_.size()) {
        // Find a node with degree < K
        std::string toRemove;
        int minSpillCost = INT_MAX;
        
        for (auto& [name, node] : workGraph) {
            if (removed.count(name)) continue;
            
            // Count actual degree (excluding removed nodes)
            int actualDegree = 0;
            for (const auto& neighbor : node.neighbors) {
                if (!removed.count(neighbor)) actualDegree++;
            }
            
            if (actualDegree < K) {
                toRemove = name;
                break;
            }
            
            // Track potential spill candidate
            if (node.spillCost < minSpillCost) {
                minSpillCost = node.spillCost;
                toRemove = name;
            }
        }
        
        if (toRemove.empty()) break;
        
        order.push_back(toRemove);
        removed.insert(toRemove);
    }
    
    // Reverse order for coloring
    std::reverse(order.begin(), order.end());
    return order;
}

void GlobalRegisterAllocator::selectColors(const std::vector<std::string>& order) {
    std::vector<VarRegister> availableRegs = {
        VarRegister::RBX,
        VarRegister::R12,
        VarRegister::R13,
        VarRegister::R14,
        VarRegister::R15
    };
    
    for (const auto& name : order) {
        auto& node = interferenceGraph_[name];
        
        // Find colors used by neighbors
        std::set<VarRegister> usedColors;
        for (const auto& neighbor : node.neighbors) {
            auto it = interferenceGraph_.find(neighbor);
            if (it != interferenceGraph_.end() && it->second.color != VarRegister::NONE) {
                usedColors.insert(it->second.color);
            }
        }
        
        // Find first available color
        VarRegister color = VarRegister::NONE;
        for (auto reg : availableRegs) {
            if (usedColors.find(reg) == usedColors.end()) {
                color = reg;
                break;
            }
        }
        
        if (color != VarRegister::NONE) {
            node.color = color;
            globalVars_[name].assignedReg = color;
            usedRegisters_.insert(color);
        } else {
            // Spill
            node.spilled = true;
            node.color = VarRegister::NONE;
            globalVars_[name].assignedReg = VarRegister::NONE;
        }
    }
}

void GlobalRegisterAllocator::assignStackSlots() {
    for (auto& [name, info] : globalVars_) {
        if (info.isConstant) continue;  // Constants don't need storage
        
        if (info.assignedReg == VarRegister::NONE) {
            info.stackOffset = nextStackOffset_;
            nextStackOffset_ -= 8;
        }
    }
}

bool GlobalRegisterAllocator::tryEvalConstant(Expression* expr, int64_t& outValue) {
    if (auto* intLit = dynamic_cast<IntegerLiteral*>(expr)) {
        outValue = intLit->value;
        return true;
    }
    if (auto* boolLit = dynamic_cast<BoolLiteral*>(expr)) {
        outValue = boolLit->value ? 1 : 0;
        return true;
    }
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        int64_t left, right;
        if (tryEvalConstant(binary->left.get(), left) && tryEvalConstant(binary->right.get(), right)) {
            switch (binary->op) {
                case TokenType::PLUS: outValue = left + right; return true;
                case TokenType::MINUS: outValue = left - right; return true;
                case TokenType::STAR: outValue = left * right; return true;
                case TokenType::SLASH: if (right != 0) { outValue = left / right; return true; } break;
                case TokenType::PERCENT: if (right != 0) { outValue = left % right; return true; } break;
                default: break;
            }
        }
    }
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        int64_t val;
        if (tryEvalConstant(unary->operand.get(), val)) {
            switch (unary->op) {
                case TokenType::MINUS: outValue = -val; return true;
                case TokenType::NOT: outValue = !val ? 1 : 0; return true;
                default: break;
            }
        }
    }
    return false;
}

// ============================================
// SSA-based Global Register Allocator
// ============================================

SSAGlobalRegisterAllocator::SSAGlobalRegisterAllocator() {}

void SSAGlobalRegisterAllocator::analyze(Program& program) {
    // This would use SSA form for more precise analysis
    // For now, delegate to the standard allocator
    buildLiveRanges();
    buildInterferenceGraph();
    allocateRegisters();
}

VarRegister SSAGlobalRegisterAllocator::getRegister(int ssaValueId) const {
    auto it = assignments_.find(ssaValueId);
    if (it != assignments_.end()) {
        return it->second;
    }
    return VarRegister::NONE;
}

void SSAGlobalRegisterAllocator::buildLiveRanges() {
    // Build live ranges from SSA form
    // Each SSA value has a single definition point
    // Live range extends from def to last use
}

void SSAGlobalRegisterAllocator::buildInterferenceGraph() {
    // Two SSA values interfere if their live ranges overlap
}

void SSAGlobalRegisterAllocator::allocateRegisters() {
    // Use linear scan or graph coloring on SSA values
}

} // namespace tyl
