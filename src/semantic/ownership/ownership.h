// Tyl Compiler - Ownership System
// Tracks ownership, moves, and borrows for memory safety
#ifndef TYL_OWNERSHIP_H
#define TYL_OWNERSHIP_H

#include "common/common.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <functional>

namespace tyl {

// Ownership state of a variable
enum class OwnershipState {
    OWNED,              // Variable owns its value
    MOVED,              // Value has been moved out
    BORROWED_SHARED,    // Immutably borrowed (&T)
    BORROWED_MUT,       // Mutably borrowed (&mut T)
    PARTIALLY_MOVED,    // Some fields moved (for records)
    UNINITIALIZED       // Declared but not initialized
};

// Parameter passing mode
enum class ParamMode {
    OWNED,      // Takes ownership (default for non-Copy types)
    BORROW,     // Immutable borrow (&T)
    BORROW_MUT, // Mutable borrow (&mut T)
    COPY        // Copy (for Copy types)
};

// Lifetime identifier
struct Lifetime {
    std::string name;           // e.g., "'a", "'static"
    int scopeDepth = 0;         // Scope where lifetime is valid
    bool isStatic = false;      // 'static lifetime
    
    bool operator==(const Lifetime& other) const {
        return name == other.name;
    }
    
    bool outlives(const Lifetime& other) const {
        if (isStatic) return true;
        if (other.isStatic) return false;
        return scopeDepth <= other.scopeDepth;
    }
};

// Drop trait information
struct DropInfo {
    std::string typeName;
    bool hasCustomDrop = false;
    std::string dropFunctionName;  // Name of the drop function to call
};

// Information about an active borrow
struct BorrowInfo {
    std::string borrower;       // Name of borrowing variable/expression
    SourceLocation location;    // Where the borrow occurred
    bool isMutable;             // Is this a mutable borrow?
    int scopeDepth;             // Scope depth where borrow is valid
    Lifetime lifetime;          // Lifetime of the borrow
};

// Function parameter ownership info
struct ParamOwnershipInfo {
    std::string name;
    ParamMode mode;
    std::string typeName;
    Lifetime lifetime;          // For borrowed parameters
    bool consumed = false;      // Was the parameter consumed (moved out)?
};

// Ownership information for a variable
struct OwnershipInfo {
    OwnershipState state = OwnershipState::UNINITIALIZED;
    SourceLocation lastMoveLocation;        // Where it was moved (if moved)
    std::vector<BorrowInfo> activeBorrows;  // Current active borrows
    std::unordered_set<std::string> movedFields;  // For partial moves
    bool needsDrop = false;                 // Does this need cleanup on scope exit?
    bool isCopyType = false;                // Is this a Copy type (primitives)?
    std::string typeName;                   // Type name for drop lookup
    Lifetime lifetime;                      // Lifetime of the value
    ParamMode paramMode = ParamMode::OWNED; // How this was passed (if parameter)
    
    bool isUsable() const {
        return state == OwnershipState::OWNED || 
               state == OwnershipState::BORROWED_SHARED ||
               state == OwnershipState::BORROWED_MUT;
    }
    
    bool canMove() const {
        return state == OwnershipState::OWNED && activeBorrows.empty();
    }
    
    bool canBorrowShared() const {
        // Can borrow shared if owned or already borrowed shared (not mut borrowed)
        if (state == OwnershipState::MOVED || state == OwnershipState::UNINITIALIZED) {
            return false;
        }
        // Check no mutable borrows exist
        for (const auto& b : activeBorrows) {
            if (b.isMutable) return false;
        }
        return true;
    }
    
    bool canBorrowMut() const {
        // Can only borrow mut if owned and no other borrows exist
        return state == OwnershipState::OWNED && activeBorrows.empty();
    }
};

// Tracks ownership state for all variables in current scope
class OwnershipTracker {
public:
    OwnershipTracker() = default;
    
    // Initialize ownership for a new variable
    void initVar(const std::string& name, bool isCopyType, bool needsDrop);
    void initVar(const std::string& name, bool isCopyType, bool needsDrop, 
                 const std::string& typeName, ParamMode mode = ParamMode::OWNED);
    
    // Mark variable as initialized (after assignment)
    void markInitialized(const std::string& name);
    
    // Record a move - returns error message if invalid
    std::optional<std::string> recordMove(const std::string& name, const SourceLocation& loc);
    
    // Record a borrow - returns error message if invalid
    std::optional<std::string> recordBorrow(const std::string& name, const std::string& borrower,
                                            bool isMutable, const SourceLocation& loc, int scopeDepth);
    
    // Record a borrow with lifetime
    std::optional<std::string> recordBorrow(const std::string& name, const std::string& borrower,
                                            bool isMutable, const SourceLocation& loc, 
                                            int scopeDepth, const Lifetime& lifetime);
    
    // End borrows at a given scope depth
    void endBorrowsAtScope(int scopeDepth);
    
    // Check if variable is usable - returns error message if not
    std::optional<std::string> checkUsable(const std::string& name, const SourceLocation& loc);
    
    // Check if variable can be borrowed - returns error message if not
    std::optional<std::string> checkCanBorrow(const std::string& name, bool isMutable, 
                                               const SourceLocation& loc);
    
    // Get variables that need drop at scope exit
    std::vector<std::string> getDropsForScope() const;
    
    // Get ownership info for a variable
    const OwnershipInfo* getInfo(const std::string& name) const;
    OwnershipInfo* getInfoMut(const std::string& name);
    
    // Push/pop scope for nested tracking
    void pushScope();
    void popScope();
    
    // Clone for a new scope (inherits parent state)
    OwnershipTracker clone() const;
    
    // Function parameter tracking
    void enterFunction(const std::vector<ParamOwnershipInfo>& params);
    void exitFunction();
    std::optional<std::string> checkParamUsage(const std::string& name, bool isMove, 
                                                const SourceLocation& loc);
    
    // Restore ownership after reassignment
    void restoreOwnership(const std::string& name);
    
    // Drop trait registration
    static void registerDropType(const std::string& typeName, const std::string& dropFn);
    static const DropInfo* getDropInfo(const std::string& typeName);
    static bool hasCustomDrop(const std::string& typeName);
    
    // Lifetime tracking
    Lifetime createLifetime(const std::string& name);
    void setLifetime(const std::string& varName, const Lifetime& lifetime);
    std::optional<std::string> checkLifetimeValid(const Lifetime& borrow, 
                                                   const Lifetime& borrowed,
                                                   const SourceLocation& loc);
    
private:
    std::unordered_map<std::string, OwnershipInfo> vars_;
    std::vector<std::string> scopeVars_;  // Variables declared in current scope
    int currentScopeDepth_ = 0;
    
    // Function parameter tracking
    std::vector<ParamOwnershipInfo> currentParams_;
    bool inFunction_ = false;
    
    // Lifetime counter for generating unique lifetimes
    int lifetimeCounter_ = 0;
    
    // Global drop registry
    static std::unordered_map<std::string, DropInfo> dropRegistry_;
};

// Determine if a type is Copy (can be implicitly copied)
bool isCopyType(const std::string& typeName);

// Determine if a type needs Drop (cleanup on scope exit)
bool needsDropType(const std::string& typeName);

} // namespace tyl

#endif // TYL_OWNERSHIP_H
