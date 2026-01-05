// Tyl Compiler - Symbol Table
#ifndef TYL_SYMBOL_TABLE_H
#define TYL_SYMBOL_TABLE_H

#include "semantic/types/types.h"
#include "semantic/ownership/ownership.h"
#include "common/common.h"  // For SourceLocation
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>

namespace tyl {

enum class SymbolKind { VARIABLE, FUNCTION, PARAMETER, TYPE, RECORD_FIELD, MODULE, MACRO, LAYER };
enum class StorageClass { LOCAL, GLOBAL, HEAP, REGISTER };

// OwnershipState is now defined in ownership.h

struct Symbol {
    std::string name;
    SymbolKind kind;
    TypePtr type;
    StorageClass storage = StorageClass::LOCAL;
    bool isMutable = true;
    bool isExported = false;
    bool isInitialized = false;
    bool isUsed = false;           // Track if variable is used
    bool isParameter = false;      // Track if this is a function parameter
    int32_t offset = 0;
    int paramCount = 0;
    bool isVariadic = false;
    std::string file;
    int line = 0;
    int column = 0;
    SourceLocation location;       // Full source location for warnings
    
    // Ownership tracking
    OwnershipState ownershipState = OwnershipState::UNINITIALIZED;
    SourceLocation moveLocation;   // Where the value was moved (if moved)
    bool isCopyType = false;       // Is this a Copy type (primitives)?
    bool needsDrop = false;        // Does this need cleanup on scope exit?
    int borrowCount = 0;           // Number of active borrows
    bool hasMutableBorrow = false; // Is there an active mutable borrow?
    
    Symbol() = default;
    Symbol(std::string n, SymbolKind k, TypePtr t) : name(std::move(n)), kind(k), type(std::move(t)) {}
    
    // Ownership helpers
    bool isOwned() const { return ownershipState == OwnershipState::OWNED; }
    bool isMoved() const { return ownershipState == OwnershipState::MOVED; }
    bool canMove() const { return isOwned() && borrowCount == 0; }
    bool canBorrowShared() const { return !isMoved() && ownershipState != OwnershipState::UNINITIALIZED && !hasMutableBorrow; }
    bool canBorrowMut() const { return isOwned() && borrowCount == 0; }
};

class Scope {
public:
    enum class Kind { GLOBAL, MODULE, FUNCTION, BLOCK, LOOP, UNSAFE };
    Scope(Kind k, Scope* parent = nullptr) : kind_(k), parent_(parent) {}
    bool define(const Symbol& sym);
    Symbol* lookup(const std::string& name);
    Symbol* lookupLocal(const std::string& name);
    Kind kind() const { return kind_; }
    Scope* parent() const { return parent_; }
    bool isGlobal() const { return kind_ == Kind::GLOBAL; }
    bool isFunction() const { return kind_ == Kind::FUNCTION; }
    bool isUnsafe() const;
    const std::unordered_map<std::string, Symbol>& symbols() const { return symbols_; }
    std::unordered_map<std::string, Symbol>& symbolsMut() { return symbols_; }  // Mutable access for marking usage
    int32_t allocateLocal(size_t size);
    int32_t currentStackOffset() const { return stackOffset_; }
private:
    Kind kind_;
    Scope* parent_;
    std::unordered_map<std::string, Symbol> symbols_;
    int32_t stackOffset_ = 0;
};

class SymbolTable {
public:
    SymbolTable();
    void pushScope(Scope::Kind kind);
    void popScope();
    Scope* currentScope() { return current_; }
    Scope* globalScope() { return &global_; }
    bool define(const Symbol& sym);
    Symbol* lookup(const std::string& name);
    Symbol* lookupLocal(const std::string& name);
    void registerType(const std::string& name, TypePtr type);
    TypePtr lookupType(const std::string& name);
    bool inFunction() const;
    bool inLoop() const;
    bool inUnsafe() const;
    Scope* enclosingFunction();
    int scopeDepth() const { return scopeDepth_; }  // Get current scope depth for lifetime tracking
private:
    Scope global_;
    Scope* current_;
    std::vector<std::unique_ptr<Scope>> scopes_;
    int scopeDepth_ = 0;  // Track scope depth for lifetime analysis
};

} // namespace tyl

#endif // TYL_SYMBOL_TABLE_H
