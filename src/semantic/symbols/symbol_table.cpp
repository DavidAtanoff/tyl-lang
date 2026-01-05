// Tyl Compiler - Symbol Table Implementation
#include "semantic/symbols/symbol_table.h"

namespace tyl {

bool Scope::define(const Symbol& sym) {
    if (symbols_.count(sym.name)) return false;
    symbols_[sym.name] = sym;
    return true;
}

Symbol* Scope::lookup(const std::string& name) {
    auto it = symbols_.find(name);
    if (it != symbols_.end()) return &it->second;
    if (parent_) return parent_->lookup(name);
    return nullptr;
}

Symbol* Scope::lookupLocal(const std::string& name) {
    auto it = symbols_.find(name);
    return it != symbols_.end() ? &it->second : nullptr;
}

bool Scope::isUnsafe() const {
    if (kind_ == Kind::UNSAFE) return true;
    if (parent_) return parent_->isUnsafe();
    return false;
}

int32_t Scope::allocateLocal(size_t size) {
    size = (size + 7) & ~7;
    stackOffset_ -= (int32_t)size;
    return stackOffset_;
}

SymbolTable::SymbolTable() : global_(Scope::Kind::GLOBAL), current_(&global_) {
    auto& reg = TypeRegistry::instance();
    
    auto printType = std::make_shared<FunctionType>();
    printType->params.push_back({"value", reg.anyType()});
    printType->isVariadic = true;
    printType->returnType = reg.voidType();
    Symbol printSym("print", SymbolKind::FUNCTION, printType);
    printSym.isExported = true;
    global_.define(printSym);
    
    auto lenType = std::make_shared<FunctionType>();
    lenType->params.push_back({"value", reg.anyType()});
    lenType->returnType = reg.intType();
    global_.define(Symbol("len", SymbolKind::FUNCTION, lenType));
    
    auto strType = std::make_shared<FunctionType>();
    strType->params.push_back({"value", reg.anyType()});
    strType->returnType = reg.stringType();
    global_.define(Symbol("str", SymbolKind::FUNCTION, strType));
    
    auto intType = std::make_shared<FunctionType>();
    intType->params.push_back({"value", reg.anyType()});
    intType->returnType = reg.intType();
    global_.define(Symbol("int", SymbolKind::FUNCTION, intType));
    
    auto floatType = std::make_shared<FunctionType>();
    floatType->params.push_back({"value", reg.anyType()});
    floatType->returnType = reg.floatType();
    global_.define(Symbol("float", SymbolKind::FUNCTION, floatType));
    
    auto inputType = std::make_shared<FunctionType>();
    inputType->params.push_back({"prompt", reg.stringType()});
    inputType->returnType = reg.stringType();
    global_.define(Symbol("input", SymbolKind::FUNCTION, inputType));
    
    auto typeType = std::make_shared<FunctionType>();
    typeType->params.push_back({"value", reg.anyType()});
    typeType->returnType = reg.stringType();
    global_.define(Symbol("type", SymbolKind::FUNCTION, typeType));
}

void SymbolTable::pushScope(Scope::Kind kind) {
    auto scope = std::make_unique<Scope>(kind, current_);
    current_ = scope.get();
    scopes_.push_back(std::move(scope));
    scopeDepth_++;
}

void SymbolTable::popScope() { 
    if (current_ != &global_) {
        current_ = current_->parent();
        scopeDepth_--;
    }
}
bool SymbolTable::define(const Symbol& sym) { return current_->define(sym); }
Symbol* SymbolTable::lookup(const std::string& name) { return current_->lookup(name); }
Symbol* SymbolTable::lookupLocal(const std::string& name) { return current_->lookupLocal(name); }
void SymbolTable::registerType(const std::string& name, TypePtr type) { TypeRegistry::instance().registerType(name, std::move(type)); }
TypePtr SymbolTable::lookupType(const std::string& name) { return TypeRegistry::instance().lookupType(name); }

bool SymbolTable::inFunction() const {
    Scope* s = current_;
    while (s) { if (s->kind() == Scope::Kind::FUNCTION) return true; s = s->parent(); }
    return false;
}

bool SymbolTable::inLoop() const {
    Scope* s = current_;
    while (s) {
        if (s->kind() == Scope::Kind::LOOP) return true;
        if (s->kind() == Scope::Kind::FUNCTION) return false;
        s = s->parent();
    }
    return false;
}

bool SymbolTable::inUnsafe() const { return current_->isUnsafe(); }

Scope* SymbolTable::enclosingFunction() {
    Scope* s = current_;
    while (s) { if (s->kind() == Scope::Kind::FUNCTION) return s; s = s->parent(); }
    return nullptr;
}

} // namespace tyl
