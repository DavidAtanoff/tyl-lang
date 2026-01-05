// Tyl Compiler - Ownership System Implementation
#include "ownership.h"

namespace tyl {

// Static drop registry
std::unordered_map<std::string, DropInfo> OwnershipTracker::dropRegistry_;

void OwnershipTracker::initVar(const std::string& name, bool isCopyType, bool needsDrop) {
    initVar(name, isCopyType, needsDrop, "", ParamMode::OWNED);
}

void OwnershipTracker::initVar(const std::string& name, bool isCopyType, bool needsDrop,
                                const std::string& typeName, ParamMode mode) {
    OwnershipInfo info;
    info.state = OwnershipState::UNINITIALIZED;
    info.isCopyType = isCopyType;
    info.needsDrop = needsDrop;
    info.typeName = typeName;
    info.paramMode = mode;
    info.lifetime = createLifetime("'" + name);
    vars_[name] = info;
    scopeVars_.push_back(name);
}

void OwnershipTracker::markInitialized(const std::string& name) {
    auto it = vars_.find(name);
    if (it != vars_.end()) {
        it->second.state = OwnershipState::OWNED;
    }
}

std::optional<std::string> OwnershipTracker::recordMove(const std::string& name, const SourceLocation& loc) {
    auto it = vars_.find(name);
    if (it == vars_.end()) {
        return std::nullopt;  // Unknown variable, let type checker handle it
    }
    
    auto& info = it->second;
    
    // Copy types don't move, they copy
    if (info.isCopyType) {
        return std::nullopt;  // OK - implicit copy
    }
    
    // Borrowed parameters cannot be moved
    if (info.paramMode == ParamMode::BORROW || info.paramMode == ParamMode::BORROW_MUT) {
        return "cannot move out of borrowed parameter '" + name + "'";
    }
    
    // Check current state
    switch (info.state) {
        case OwnershipState::UNINITIALIZED:
            return "use of uninitialized variable '" + name + "'";
            
        case OwnershipState::MOVED:
            return "use of moved value '" + name + "' (moved at " + 
                   info.lastMoveLocation.filename + ":" + 
                   std::to_string(info.lastMoveLocation.line) + ")";
            
        case OwnershipState::BORROWED_SHARED:
        case OwnershipState::BORROWED_MUT:
            return "cannot move '" + name + "' while borrowed";
            
        case OwnershipState::OWNED:
            // Check for active borrows
            if (!info.activeBorrows.empty()) {
                return "cannot move '" + name + "' while borrowed";
            }
            // OK to move
            info.state = OwnershipState::MOVED;
            info.lastMoveLocation = loc;
            return std::nullopt;
            
        case OwnershipState::PARTIALLY_MOVED:
            return "use of partially moved value '" + name + "'";
    }
    
    return std::nullopt;
}

std::optional<std::string> OwnershipTracker::recordBorrow(
    const std::string& name, const std::string& borrower,
    bool isMutable, const SourceLocation& loc, int scopeDepth) {
    Lifetime lt;
    lt.name = "'borrow_" + std::to_string(lifetimeCounter_++);
    lt.scopeDepth = scopeDepth;
    return recordBorrow(name, borrower, isMutable, loc, scopeDepth, lt);
}

std::optional<std::string> OwnershipTracker::recordBorrow(
    const std::string& name, const std::string& borrower,
    bool isMutable, const SourceLocation& loc, int scopeDepth, const Lifetime& lifetime) {
    
    auto it = vars_.find(name);
    if (it == vars_.end()) {
        return std::nullopt;  // Unknown variable
    }
    
    auto& info = it->second;
    
    // Check state
    if (info.state == OwnershipState::UNINITIALIZED) {
        return "cannot borrow uninitialized variable '" + name + "'";
    }
    if (info.state == OwnershipState::MOVED) {
        return "cannot borrow moved value '" + name + "'";
    }
    
    if (isMutable) {
        // Mutable borrow requires no other borrows
        if (!info.activeBorrows.empty()) {
            const auto& existing = info.activeBorrows[0];
            if (existing.isMutable) {
                return "cannot borrow '" + name + "' as mutable more than once";
            } else {
                return "cannot borrow '" + name + "' as mutable while borrowed as immutable";
            }
        }
        // Cannot mutably borrow an immutably borrowed parameter
        if (info.paramMode == ParamMode::BORROW) {
            return "cannot mutably borrow immutably borrowed parameter '" + name + "'";
        }
    } else {
        // Shared borrow - check no mutable borrows exist
        for (const auto& b : info.activeBorrows) {
            if (b.isMutable) {
                return "cannot borrow '" + name + "' as immutable while mutably borrowed";
            }
        }
    }
    
    // Record the borrow
    BorrowInfo borrow;
    borrow.borrower = borrower;
    borrow.location = loc;
    borrow.isMutable = isMutable;
    borrow.scopeDepth = scopeDepth;
    borrow.lifetime = lifetime;
    info.activeBorrows.push_back(borrow);
    
    return std::nullopt;
}

void OwnershipTracker::endBorrowsAtScope(int scopeDepth) {
    for (auto& [name, info] : vars_) {
        info.activeBorrows.erase(
            std::remove_if(info.activeBorrows.begin(), info.activeBorrows.end(),
                [scopeDepth](const BorrowInfo& b) { return b.scopeDepth >= scopeDepth; }),
            info.activeBorrows.end()
        );
    }
}

std::optional<std::string> OwnershipTracker::checkUsable(const std::string& name, const SourceLocation& loc) {
    (void)loc;  // May be used for better error messages
    
    auto it = vars_.find(name);
    if (it == vars_.end()) {
        return std::nullopt;  // Unknown variable
    }
    
    const auto& info = it->second;
    
    switch (info.state) {
        case OwnershipState::UNINITIALIZED:
            return "use of uninitialized variable '" + name + "'";
            
        case OwnershipState::MOVED:
            return "use of moved value '" + name + "' (moved at " + 
                   info.lastMoveLocation.filename + ":" + 
                   std::to_string(info.lastMoveLocation.line) + ")";
            
        case OwnershipState::PARTIALLY_MOVED:
            return "use of partially moved value '" + name + "'";
            
        default:
            return std::nullopt;  // OK
    }
}

std::optional<std::string> OwnershipTracker::checkCanBorrow(const std::string& name, bool isMutable,
                                                             const SourceLocation& loc) {
    auto it = vars_.find(name);
    if (it == vars_.end()) {
        return std::nullopt;
    }
    
    const auto& info = it->second;
    
    if (info.state == OwnershipState::UNINITIALIZED) {
        return "cannot borrow uninitialized variable '" + name + "'";
    }
    if (info.state == OwnershipState::MOVED) {
        return "cannot borrow moved value '" + name + "' (moved at " +
               info.lastMoveLocation.filename + ":" +
               std::to_string(info.lastMoveLocation.line) + ")";
    }
    
    if (isMutable) {
        if (!info.activeBorrows.empty()) {
            const auto& b = info.activeBorrows[0];
            return "cannot borrow '" + name + "' as mutable because it is already borrowed at " +
                   b.location.filename + ":" + std::to_string(b.location.line);
        }
    } else {
        for (const auto& b : info.activeBorrows) {
            if (b.isMutable) {
                return "cannot borrow '" + name + "' as immutable because it is mutably borrowed at " +
                       b.location.filename + ":" + std::to_string(b.location.line);
            }
        }
    }
    
    (void)loc;
    return std::nullopt;
}

std::vector<std::string> OwnershipTracker::getDropsForScope() const {
    std::vector<std::string> drops;
    for (const auto& name : scopeVars_) {
        auto it = vars_.find(name);
        if (it != vars_.end() && it->second.needsDrop && 
            it->second.state == OwnershipState::OWNED) {
            // Don't drop borrowed parameters - they don't own the value
            if (it->second.paramMode != ParamMode::BORROW && 
                it->second.paramMode != ParamMode::BORROW_MUT) {
                drops.push_back(name);
            }
        }
    }
    // Reverse order - drop in reverse declaration order
    std::reverse(drops.begin(), drops.end());
    return drops;
}

const OwnershipInfo* OwnershipTracker::getInfo(const std::string& name) const {
    auto it = vars_.find(name);
    return it != vars_.end() ? &it->second : nullptr;
}

OwnershipInfo* OwnershipTracker::getInfoMut(const std::string& name) {
    auto it = vars_.find(name);
    return it != vars_.end() ? &it->second : nullptr;
}

void OwnershipTracker::pushScope() {
    currentScopeDepth_++;
}

void OwnershipTracker::popScope() {
    // End borrows at this scope
    endBorrowsAtScope(currentScopeDepth_);
    
    // Remove variables declared in this scope
    for (const auto& name : scopeVars_) {
        vars_.erase(name);
    }
    scopeVars_.clear();
    
    currentScopeDepth_--;
}

OwnershipTracker OwnershipTracker::clone() const {
    return *this;
}

void OwnershipTracker::enterFunction(const std::vector<ParamOwnershipInfo>& params) {
    inFunction_ = true;
    currentParams_ = params;
    
    // Initialize ownership for each parameter
    for (const auto& param : params) {
        bool isCopy = (param.mode == ParamMode::COPY) || isCopyType(param.typeName);
        bool needsDrop = (param.mode == ParamMode::OWNED) && needsDropType(param.typeName);
        initVar(param.name, isCopy, needsDrop, param.typeName, param.mode);
        markInitialized(param.name);
    }
}

void OwnershipTracker::exitFunction() {
    inFunction_ = false;
    currentParams_.clear();
}

std::optional<std::string> OwnershipTracker::checkParamUsage(const std::string& name, bool isMove,
                                                              const SourceLocation& loc) {
    auto it = vars_.find(name);
    if (it == vars_.end()) {
        return std::nullopt;
    }
    
    const auto& info = it->second;
    
    // Check if this is a borrowed parameter being moved
    if (isMove && (info.paramMode == ParamMode::BORROW || info.paramMode == ParamMode::BORROW_MUT)) {
        return "cannot move out of borrowed parameter '" + name + "'";
    }
    
    // Check if borrowed parameter is being used after the borrow ended
    // (This would be caught by lifetime analysis)
    
    (void)loc;
    return std::nullopt;
}

void OwnershipTracker::restoreOwnership(const std::string& name) {
    auto it = vars_.find(name);
    if (it != vars_.end()) {
        it->second.state = OwnershipState::OWNED;
        it->second.activeBorrows.clear();
        it->second.movedFields.clear();
    }
}

void OwnershipTracker::registerDropType(const std::string& typeName, const std::string& dropFn) {
    DropInfo info;
    info.typeName = typeName;
    info.hasCustomDrop = true;
    info.dropFunctionName = dropFn;
    dropRegistry_[typeName] = info;
}

const DropInfo* OwnershipTracker::getDropInfo(const std::string& typeName) {
    auto it = dropRegistry_.find(typeName);
    return it != dropRegistry_.end() ? &it->second : nullptr;
}

bool OwnershipTracker::hasCustomDrop(const std::string& typeName) {
    auto it = dropRegistry_.find(typeName);
    return it != dropRegistry_.end() && it->second.hasCustomDrop;
}

Lifetime OwnershipTracker::createLifetime(const std::string& name) {
    Lifetime lt;
    lt.name = name.empty() ? ("'_" + std::to_string(lifetimeCounter_++)) : name;
    lt.scopeDepth = currentScopeDepth_;
    lt.isStatic = (name == "'static");
    return lt;
}

void OwnershipTracker::setLifetime(const std::string& varName, const Lifetime& lifetime) {
    auto it = vars_.find(varName);
    if (it != vars_.end()) {
        it->second.lifetime = lifetime;
    }
}

std::optional<std::string> OwnershipTracker::checkLifetimeValid(const Lifetime& borrow,
                                                                 const Lifetime& borrowed,
                                                                 const SourceLocation& loc) {
    // The borrowed value must outlive the borrow
    if (!borrowed.outlives(borrow)) {
        return "borrowed value does not live long enough (lifetime " + borrowed.name + 
               " does not outlive " + borrow.name + ")";
    }
    (void)loc;
    return std::nullopt;
}

// Helper functions to determine type properties
bool isCopyType(const std::string& typeName) {
    // Primitive types are Copy
    static const std::unordered_set<std::string> copyTypes = {
        "int", "i8", "i16", "i32", "i64", "i128",
        "uint", "u8", "u16", "u32", "u64", "u128",
        "float", "f16", "f32", "f64", "f128",
        "bool", "char", "byte",
        // Pointers are Copy (the pointer itself, not the data)
        "*", "&"
    };
    
    // Check if it's a primitive type
    if (copyTypes.count(typeName)) {
        return true;
    }
    
    // Check for pointer/reference types
    if (typeName.length() > 0 && (typeName[0] == '*' || typeName[0] == '&')) {
        return true;
    }
    
    return false;
}

bool needsDropType(const std::string& typeName) {
    // Types that need cleanup
    // - Lists, Maps, Records with heap data
    // - Strings (heap allocated)
    // - Any non-Copy type
    
    if (isCopyType(typeName)) {
        return false;
    }
    
    // Check for custom drop
    if (OwnershipTracker::hasCustomDrop(typeName)) {
        return true;
    }
    
    // These types need drop
    static const std::unordered_set<std::string> dropTypes = {
        "string", "str", "[", "List", "Map", "Box", "Rc", "Arc"
    };
    
    for (const auto& dt : dropTypes) {
        if (typeName.find(dt) != std::string::npos) {
            return true;
        }
    }
    
    // Records and custom types generally need drop
    // (unless marked as Copy)
    return true;
}

} // namespace tyl
