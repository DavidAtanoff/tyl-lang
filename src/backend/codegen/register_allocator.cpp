// Tyl Compiler - Register Allocator Implementation
// Linear Scan Register Allocation

#include "register_allocator.h"
#include <algorithm>

namespace tyl {

RegisterAllocator::RegisterAllocator() : currentPos_(0) {}

void RegisterAllocator::reset() {
    liveRanges_.clear();
    assignments_.clear();
    usedRegisters_.clear();
    currentPos_ = 0;
}

void RegisterAllocator::analyze(FnDecl& fn) {
    reset();
    computeLiveRanges(fn.body.get(), fn.params);
    allocateRegisters();
}

VarRegister RegisterAllocator::getRegister(const std::string& name) const {
    auto it = assignments_.find(name);
    if (it != assignments_.end()) {
        return it->second;
    }
    return VarRegister::NONE;
}

bool RegisterAllocator::isInRegister(const std::string& name) const {
    return getRegister(name) != VarRegister::NONE;
}

std::vector<VarRegister> RegisterAllocator::getUsedRegisters() const {
    std::vector<VarRegister> result;
    for (auto reg : usedRegisters_) {
        result.push_back(reg);
    }
    return result;
}

void RegisterAllocator::recordDef(const std::string& name) {
    // Skip internal/temporary variables
    if (name.empty() || name[0] == '$') return;
    
    // Find existing range or create new one
    for (auto& range : liveRanges_) {
        if (range.name == name) {
            range.endPos = currentPos_;
            return;
        }
    }
    
    // New variable
    LiveRange range;
    range.name = name;
    range.startPos = currentPos_;
    range.endPos = currentPos_;
    range.reg = VarRegister::NONE;
    range.stackOffset = 0;
    range.isParam = false;
    range.paramIndex = -1;
    liveRanges_.push_back(range);
}

void RegisterAllocator::recordUse(const std::string& name) {
    // Skip internal/temporary variables
    if (name.empty() || name[0] == '$') return;
    
    for (auto& range : liveRanges_) {
        if (range.name == name) {
            range.endPos = currentPos_;
            return;
        }
    }
    
    // Variable used before definition (parameter or global)
    LiveRange range;
    range.name = name;
    range.startPos = 0;
    range.endPos = currentPos_;
    range.reg = VarRegister::NONE;
    range.stackOffset = 0;
    range.isParam = false;
    range.paramIndex = -1;
    liveRanges_.push_back(range);
}

void RegisterAllocator::computeLiveRanges(Statement* body, 
    const std::vector<std::pair<std::string, std::string>>& params) {
    
    // Add parameters as live from the start
    for (size_t i = 0; i < params.size(); i++) {
        LiveRange range;
        range.name = params[i].first;
        range.startPos = 0;
        range.endPos = 0;
        range.reg = VarRegister::NONE;
        range.stackOffset = 0;
        range.isParam = true;
        range.paramIndex = (int)i;
        liveRanges_.push_back(range);
    }
    
    currentPos_ = 1;
    scanStatement(body);
}

void RegisterAllocator::scanStatement(Statement* stmt) {
    if (!stmt) return;
    
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        for (auto& s : block->statements) {
            scanStatement(s.get());
            currentPos_++;
        }
    }
    else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        if (varDecl->initializer) {
            scanExpression(varDecl->initializer.get());
        }
        recordDef(varDecl->name);
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        scanExpression(assignStmt->value.get());
        if (auto* id = dynamic_cast<Identifier*>(assignStmt->target.get())) {
            recordDef(id->name);
        }
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        scanExpression(exprStmt->expr.get());
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        scanExpression(ifStmt->condition.get());
        currentPos_++;
        scanStatement(ifStmt->thenBranch.get());
        for (auto& elif : ifStmt->elifBranches) {
            scanExpression(elif.first.get());
            currentPos_++;
            scanStatement(elif.second.get());
        }
        if (ifStmt->elseBranch) {
            scanStatement(ifStmt->elseBranch.get());
        }
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        scanExpression(whileStmt->condition.get());
        currentPos_++;
        scanStatement(whileStmt->body.get());
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
        scanExpression(forStmt->iterable.get());
        recordDef(forStmt->var);
        currentPos_++;
        scanStatement(forStmt->body.get());
        // Loop variable is used throughout the loop
        recordUse(forStmt->var);
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        if (returnStmt->value) {
            scanExpression(returnStmt->value.get());
        }
    }
}

void RegisterAllocator::scanExpression(Expression* expr) {
    if (!expr) return;
    
    if (auto* id = dynamic_cast<Identifier*>(expr)) {
        // Don't record function names as variable uses - they're handled separately
        // Only record actual variable uses
        recordUse(id->name);
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        scanExpression(binary->left.get());
        scanExpression(binary->right.get());
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        scanExpression(unary->operand.get());
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        // Don't scan the callee - it's a function name, not a variable
        // Only scan the arguments
        for (auto& arg : call->args) {
            scanExpression(arg.get());
        }
    }
    else if (auto* interp = dynamic_cast<InterpolatedString*>(expr)) {
        // Scan all parts of the interpolated string for variable uses
        for (auto& part : interp->parts) {
            if (std::holds_alternative<ExprPtr>(part)) {
                scanExpression(std::get<ExprPtr>(part).get());
            }
        }
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        scanExpression(ternary->condition.get());
        scanExpression(ternary->thenExpr.get());
        scanExpression(ternary->elseExpr.get());
    }
    else if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        scanExpression(index->object.get());
        scanExpression(index->index.get());
    }
    else if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        scanExpression(member->object.get());
    }
    else if (auto* range = dynamic_cast<RangeExpr*>(expr)) {
        scanExpression(range->start.get());
        scanExpression(range->end.get());
        if (range->step) {
            scanExpression(range->step.get());
        }
    }
    else if (auto* assign = dynamic_cast<AssignExpr*>(expr)) {
        scanExpression(assign->value.get());
        if (auto* targetId = dynamic_cast<Identifier*>(assign->target.get())) {
            recordDef(targetId->name);
        }
    }
    else if (auto* list = dynamic_cast<ListExpr*>(expr)) {
        for (auto& elem : list->elements) {
            scanExpression(elem.get());
        }
    }
    else if (auto* addrOf = dynamic_cast<AddressOfExpr*>(expr)) {
        scanExpression(addrOf->operand.get());
    }
    else if (auto* deref = dynamic_cast<DerefExpr*>(expr)) {
        scanExpression(deref->operand.get());
    }
}

void RegisterAllocator::allocateRegisters() {
    // Sort live ranges by start position
    std::sort(liveRanges_.begin(), liveRanges_.end(),
        [](const LiveRange& a, const LiveRange& b) {
            return a.startPos < b.startPos;
        });
    
    // Available registers (callee-saved)
    std::vector<VarRegister> availableRegs = {
        VarRegister::RBX,
        VarRegister::R12,
        VarRegister::R13,
        VarRegister::R14,
        VarRegister::R15
    };
    
    // Active intervals (currently allocated)
    std::vector<LiveRange*> active;
    
    for (auto& range : liveRanges_) {
        // Expire old intervals
        active.erase(
            std::remove_if(active.begin(), active.end(),
                [&range](LiveRange* r) {
                    return r->endPos < range.startPos;
                }),
            active.end());
        
        // Find a free register
        std::set<VarRegister> usedRegs;
        for (auto* r : active) {
            if (r->reg != VarRegister::NONE) {
                usedRegs.insert(r->reg);
            }
        }
        
        VarRegister freeReg = VarRegister::NONE;
        for (auto reg : availableRegs) {
            if (usedRegs.find(reg) == usedRegs.end()) {
                freeReg = reg;
                break;
            }
        }
        
        if (freeReg != VarRegister::NONE) {
            // Allocate register
            range.reg = freeReg;
            assignments_[range.name] = freeReg;
            usedRegisters_.insert(freeReg);
            active.push_back(&range);
            
            // Sort active by end position
            std::sort(active.begin(), active.end(),
                [](LiveRange* a, LiveRange* b) {
                    return a->endPos < b->endPos;
                });
        } else {
            // Spill: no register available, use stack
            range.reg = VarRegister::NONE;
            assignments_[range.name] = VarRegister::NONE;
        }
    }
}

} // namespace tyl
