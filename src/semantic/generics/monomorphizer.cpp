// Tyl Compiler - Monomorphization Implementation
#include "monomorphizer.h"
#include "ast_cloner.h"
#include <sstream>
#include <algorithm>

namespace tyl {

bool GenericInstantiation::operator==(const GenericInstantiation& other) const {
    if (baseName != other.baseName) return false;
    if (typeArgs.size() != other.typeArgs.size()) return false;
    for (size_t i = 0; i < typeArgs.size(); i++) {
        if (!typeArgs[i]->equals(other.typeArgs[i].get())) return false;
    }
    return true;
}

size_t GenericInstantiationHash::operator()(const GenericInstantiation& inst) const {
    size_t hash = std::hash<std::string>{}(inst.baseName);
    for (const auto& arg : inst.typeArgs) {
        hash ^= std::hash<std::string>{}(arg->toString()) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    }
    return hash;
}

std::string Monomorphizer::mangleTypeArgs(const std::vector<TypePtr>& typeArgs) const {
    std::stringstream ss;
    for (size_t i = 0; i < typeArgs.size(); i++) {
        if (i > 0) ss << "_";
        std::string typeStr = typeArgs[i]->toString();
        // Replace special characters for valid identifier
        std::replace(typeStr.begin(), typeStr.end(), '[', '_');
        std::replace(typeStr.begin(), typeStr.end(), ']', '_');
        std::replace(typeStr.begin(), typeStr.end(), '<', '_');
        std::replace(typeStr.begin(), typeStr.end(), '>', '_');
        std::replace(typeStr.begin(), typeStr.end(), ',', '_');
        std::replace(typeStr.begin(), typeStr.end(), ' ', '_');
        std::replace(typeStr.begin(), typeStr.end(), ':', '_');
        ss << typeStr;
    }
    return ss.str();
}

std::string Monomorphizer::getMangledName(const std::string& baseName,
                                          const std::vector<TypePtr>& typeArgs) const {
    if (typeArgs.empty()) return baseName;
    return baseName + "$" + mangleTypeArgs(typeArgs);
}

bool Monomorphizer::hasInstantiation(const std::string& baseName,
                                     const std::vector<TypePtr>& typeArgs) const {
    std::string mangled = getMangledName(baseName, typeArgs);
    return instantiatedNames_.find(mangled) != instantiatedNames_.end();
}

void Monomorphizer::recordFunctionInstantiation(const std::string& fnName,
                                                 const std::vector<TypePtr>& typeArgs,
                                                 FnDecl* originalDecl) {
    if (typeArgs.empty() || !originalDecl) return;
    
    std::string mangled = getMangledName(fnName, typeArgs);
    if (instantiatedNames_.find(mangled) != instantiatedNames_.end()) {
        return;  // Already recorded
    }
    
    GenericInstantiation inst;
    inst.baseName = fnName;
    inst.typeArgs = typeArgs;
    inst.mangledName = mangled;
    
    // Compute the concrete return type by substituting type parameters
    inst.returnType = substituteTypeString(originalDecl->returnType, 
                                           originalDecl->typeParams, 
                                           typeArgs);
    
    functionInstantiations_.push_back({inst, originalDecl});
    instantiatedNames_.insert(mangled);
}

void Monomorphizer::recordRecordInstantiation(const std::string& recordName,
                                               const std::vector<TypePtr>& typeArgs,
                                               RecordDecl* originalDecl) {
    if (typeArgs.empty() || !originalDecl) return;
    
    std::string mangled = getMangledName(recordName, typeArgs);
    if (instantiatedNames_.find(mangled) != instantiatedNames_.end()) {
        return;  // Already recorded
    }
    
    GenericInstantiation inst;
    inst.baseName = recordName;
    inst.typeArgs = typeArgs;
    inst.mangledName = mangled;
    
    recordInstantiations_.push_back({inst, originalDecl});
    instantiatedNames_.insert(mangled);
}

std::string Monomorphizer::substituteTypeString(const std::string& typeStr,
                                                 const std::vector<std::string>& typeParams,
                                                 const std::vector<TypePtr>& typeArgs) const {
    std::string result = typeStr;
    for (size_t i = 0; i < typeParams.size() && i < typeArgs.size(); i++) {
        // Simple string replacement - works for basic cases
        size_t pos = 0;
        while ((pos = result.find(typeParams[i], pos)) != std::string::npos) {
            // Check if it's a whole word match
            bool isWordStart = (pos == 0 || !std::isalnum(result[pos - 1]));
            bool isWordEnd = (pos + typeParams[i].length() >= result.length() || 
                             !std::isalnum(result[pos + typeParams[i].length()]));
            if (isWordStart && isWordEnd) {
                result.replace(pos, typeParams[i].length(), typeArgs[i]->toString());
                pos += typeArgs[i]->toString().length();
            } else {
                pos++;
            }
        }
    }
    return result;
}

std::unique_ptr<FnDecl> Monomorphizer::specializeFunction(FnDecl* original,
                                                           const std::vector<TypePtr>& typeArgs) {
    if (!original || typeArgs.size() != original->typeParams.size()) {
        return nullptr;
    }
    
    std::string mangledName = getMangledName(original->name, typeArgs);
    auto specialized = std::make_unique<FnDecl>(mangledName, original->location);
    
    // Copy and substitute parameters
    for (const auto& param : original->params) {
        std::string newType = substituteTypeString(param.second, original->typeParams, typeArgs);
        specialized->params.push_back({param.first, newType});
    }
    
    // Substitute return type
    specialized->returnType = substituteTypeString(original->returnType, original->typeParams, typeArgs);
    
    // Copy other properties
    specialized->isPublic = original->isPublic;
    specialized->isExtern = original->isExtern;
    specialized->isAsync = original->isAsync;
    specialized->isHot = original->isHot;
    specialized->isCold = original->isCold;
    
    // Note: typeParams is intentionally left empty - this is a concrete instantiation
    
    // Deep clone the body with type substitution
    if (original->body) {
        ASTCloner cloner(original->typeParams, typeArgs);
        specialized->body = cloner.cloneFunctionBody(original->body.get());
    }
    
    return specialized;
}

std::unique_ptr<RecordDecl> Monomorphizer::specializeRecord(RecordDecl* original,
                                                             const std::vector<TypePtr>& typeArgs) {
    if (!original || typeArgs.size() != original->typeParams.size()) {
        return nullptr;
    }
    
    std::string mangledName = getMangledName(original->name, typeArgs);
    auto specialized = std::make_unique<RecordDecl>(mangledName, original->location);
    
    // Copy and substitute fields
    for (const auto& field : original->fields) {
        std::string newType = substituteTypeString(field.second, original->typeParams, typeArgs);
        specialized->fields.push_back({field.first, newType});
    }
    
    specialized->isPublic = original->isPublic;
    // typeParams is intentionally left empty
    
    return specialized;
}

void Monomorphizer::clear() {
    functionInstantiations_.clear();
    recordInstantiations_.clear();
    instantiatedNames_.clear();
}

bool Monomorphizer::functionReturnsFloat(const std::string& mangledName) const {
    for (const auto& [inst, _] : functionInstantiations_) {
        if (inst.mangledName == mangledName) {
            return inst.returnsFloat();
        }
    }
    return false;
}

bool Monomorphizer::functionReturnsString(const std::string& mangledName) const {
    for (const auto& [inst, _] : functionInstantiations_) {
        if (inst.mangledName == mangledName) {
            return inst.returnsString();
        }
    }
    return false;
}

std::string Monomorphizer::getFunctionReturnType(const std::string& mangledName) const {
    for (const auto& [inst, _] : functionInstantiations_) {
        if (inst.mangledName == mangledName) {
            return inst.returnType;
        }
    }
    return "";
}

// GenericCollector implementation

GenericCollector::GenericCollector(Monomorphizer& mono,
                                   std::unordered_map<std::string, FnDecl*>& genericFunctions,
                                   std::unordered_map<std::string, RecordDecl*>& genericRecords)
    : mono_(mono), genericFunctions_(genericFunctions), genericRecords_(genericRecords) {}

void GenericCollector::collect(Program& program) {
    // First pass: collect all generic declarations
    for (auto& stmt : program.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            if (!fn->typeParams.empty()) {
                genericFunctions_[fn->name] = fn;
            }
        } else if (auto* rec = dynamic_cast<RecordDecl*>(stmt.get())) {
            if (!rec->typeParams.empty()) {
                genericRecords_[rec->name] = rec;
            }
        }
    }
    
    // Second pass: collect instantiations
    program.accept(*this);
}

TypePtr GenericCollector::parseType(const std::string& typeStr) {
    auto& reg = TypeRegistry::instance();
    
    // Check current type bindings first
    auto it = currentTypeBindings_.find(typeStr);
    if (it != currentTypeBindings_.end()) {
        return it->second;
    }
    
    return reg.fromString(typeStr);
}

std::vector<TypePtr> GenericCollector::inferTypeArgs(FnDecl* fn, CallExpr& call) {
    std::vector<TypePtr> result;
    auto& reg = TypeRegistry::instance();
    
    if (!fn || fn->typeParams.empty()) return result;
    
    std::unordered_map<std::string, TypePtr> inferred;
    
    // Try to infer from argument types
    for (size_t i = 0; i < call.args.size() && i < fn->params.size(); i++) {
        const std::string& paramType = fn->params[i].second;
        
        // Check if param type is a type parameter
        for (const auto& tp : fn->typeParams) {
            if (paramType == tp) {
                // This parameter has the type parameter type
                // Infer from the argument
                TypePtr argType = reg.anyType();  // Default
                
                // Try to determine argument type
                if (auto* intLit = dynamic_cast<IntegerLiteral*>(call.args[i].get())) {
                    argType = reg.intType();
                } else if (auto* floatLit = dynamic_cast<FloatLiteral*>(call.args[i].get())) {
                    argType = reg.floatType();
                } else if (auto* strLit = dynamic_cast<StringLiteral*>(call.args[i].get())) {
                    argType = reg.stringType();
                } else if (auto* boolLit = dynamic_cast<BoolLiteral*>(call.args[i].get())) {
                    argType = reg.boolType();
                } else if (auto* id = dynamic_cast<Identifier*>(call.args[i].get())) {
                    // Look up variable type from bindings
                    auto bindIt = currentTypeBindings_.find(id->name);
                    if (bindIt != currentTypeBindings_.end()) {
                        argType = bindIt->second;
                    }
                }
                
                auto existingIt = inferred.find(tp);
                if (existingIt == inferred.end()) {
                    inferred[tp] = argType;
                }
                // If already inferred, we could unify here
                break;
            }
        }
    }
    
    // Build result in order of type parameters
    for (const auto& tp : fn->typeParams) {
        auto it = inferred.find(tp);
        if (it != inferred.end()) {
            result.push_back(it->second);
        } else {
            result.push_back(reg.anyType());  // Couldn't infer
        }
    }
    
    return result;
}

void GenericCollector::visit(Program& node) {
    for (auto& stmt : node.statements) {
        stmt->accept(*this);
    }
}

void GenericCollector::visit(CallExpr& node) {
    // Visit arguments first
    for (auto& arg : node.args) {
        arg->accept(*this);
    }
    
    // Check if callee is a generic function
    if (auto* id = dynamic_cast<Identifier*>(node.callee.get())) {
        auto it = genericFunctions_.find(id->name);
        if (it != genericFunctions_.end()) {
            // Infer type arguments
            std::vector<TypePtr> typeArgs = inferTypeArgs(it->second, node);
            if (!typeArgs.empty()) {
                mono_.recordFunctionInstantiation(id->name, typeArgs, it->second);
            }
        }
    }
    
    node.callee->accept(*this);
}

void GenericCollector::visit(Identifier& node) {
    // Check if this is a generic record instantiation
    auto it = genericRecords_.find(node.name);
    if (it != genericRecords_.end()) {
        // For now, we can't infer record type args from just an identifier
        // This would need explicit type annotation or context
    }
}

void GenericCollector::visit(InterpolatedString& node) {
    for (auto& part : node.parts) {
        if (auto* exprPtr = std::get_if<ExprPtr>(&part)) {
            (*exprPtr)->accept(*this);
        }
    }
}

void GenericCollector::visit(BinaryExpr& node) {
    node.left->accept(*this);
    node.right->accept(*this);
}

void GenericCollector::visit(UnaryExpr& node) {
    node.operand->accept(*this);
}

void GenericCollector::visit(MemberExpr& node) {
    node.object->accept(*this);
}

void GenericCollector::visit(IndexExpr& node) {
    node.object->accept(*this);
    node.index->accept(*this);
}

void GenericCollector::visit(ListExpr& node) {
    for (auto& elem : node.elements) {
        elem->accept(*this);
    }
}

void GenericCollector::visit(RecordExpr& node) {
    for (auto& field : node.fields) {
        field.second->accept(*this);
    }
}

void GenericCollector::visit(MapExpr& node) {
    for (auto& entry : node.entries) {
        entry.first->accept(*this);
        entry.second->accept(*this);
    }
}

void GenericCollector::visit(RangeExpr& node) {
    node.start->accept(*this);
    node.end->accept(*this);
    if (node.step) node.step->accept(*this);
}

void GenericCollector::visit(LambdaExpr& node) {
    node.body->accept(*this);
}

void GenericCollector::visit(TernaryExpr& node) {
    node.condition->accept(*this);
    node.thenExpr->accept(*this);
    node.elseExpr->accept(*this);
}

void GenericCollector::visit(ListCompExpr& node) {
    node.iterable->accept(*this);
    node.expr->accept(*this);
    if (node.condition) node.condition->accept(*this);
}

void GenericCollector::visit(AddressOfExpr& node) {
    node.operand->accept(*this);
}

void GenericCollector::visit(BorrowExpr& node) {
    node.operand->accept(*this);
}

void GenericCollector::visit(DerefExpr& node) {
    node.operand->accept(*this);
}

void GenericCollector::visit(NewExpr& node) {
    // Check if this is a generic type instantiation
    // Parse type name for generic syntax: Name[T, U]
    size_t bracketPos = node.typeName.find('[');
    if (bracketPos != std::string::npos) {
        std::string baseName = node.typeName.substr(0, bracketPos);
        auto it = genericRecords_.find(baseName);
        if (it != genericRecords_.end()) {
            // Parse type arguments from the type name
            // This is simplified - a full implementation would properly parse
            // For now, we'll handle this in the type checker
        }
    }
    
    for (auto& arg : node.args) {
        arg->accept(*this);
    }
}

void GenericCollector::visit(CastExpr& node) {
    node.expr->accept(*this);
}

void GenericCollector::visit(AwaitExpr& node) {
    node.operand->accept(*this);
}

void GenericCollector::visit(SpawnExpr& node) {
    node.operand->accept(*this);
}

void GenericCollector::visit(AssignExpr& node) {
    node.target->accept(*this);
    node.value->accept(*this);
    
    // Track variable type for inference (handles "pi = 3.14" style assignments)
    if (auto* id = dynamic_cast<Identifier*>(node.target.get())) {
        auto& reg = TypeRegistry::instance();
        TypePtr varType = reg.anyType();
        
        if (dynamic_cast<IntegerLiteral*>(node.value.get())) {
            varType = reg.intType();
        } else if (dynamic_cast<FloatLiteral*>(node.value.get())) {
            varType = reg.floatType();
        } else if (dynamic_cast<StringLiteral*>(node.value.get())) {
            varType = reg.stringType();
        } else if (dynamic_cast<BoolLiteral*>(node.value.get())) {
            varType = reg.boolType();
        }
        
        currentTypeBindings_[id->name] = varType;
    }
}

void GenericCollector::visit(PropagateExpr& node) {
    node.operand->accept(*this);
}

void GenericCollector::visit(ExprStmt& node) {
    node.expr->accept(*this);
}

void GenericCollector::visit(VarDecl& node) {
    if (node.initializer) {
        node.initializer->accept(*this);
        
        // Track variable type for inference
        auto& reg = TypeRegistry::instance();
        TypePtr varType = reg.anyType();
        
        if (auto* intLit = dynamic_cast<IntegerLiteral*>(node.initializer.get())) {
            varType = reg.intType();
        } else if (auto* floatLit = dynamic_cast<FloatLiteral*>(node.initializer.get())) {
            varType = reg.floatType();
        } else if (auto* strLit = dynamic_cast<StringLiteral*>(node.initializer.get())) {
            varType = reg.stringType();
        } else if (auto* boolLit = dynamic_cast<BoolLiteral*>(node.initializer.get())) {
            varType = reg.boolType();
        }
        
        currentTypeBindings_[node.name] = varType;
    }
}

void GenericCollector::visit(DestructuringDecl& node) {
    if (node.initializer) {
        node.initializer->accept(*this);
    }
}

void GenericCollector::visit(AssignStmt& node) {
    node.target->accept(*this);
    node.value->accept(*this);
}

void GenericCollector::visit(Block& node) {
    for (auto& stmt : node.statements) {
        stmt->accept(*this);
    }
}

void GenericCollector::visit(IfStmt& node) {
    node.condition->accept(*this);
    node.thenBranch->accept(*this);
    for (auto& elif : node.elifBranches) {
        elif.first->accept(*this);
        elif.second->accept(*this);
    }
    if (node.elseBranch) node.elseBranch->accept(*this);
}

void GenericCollector::visit(WhileStmt& node) {
    node.condition->accept(*this);
    node.body->accept(*this);
}

void GenericCollector::visit(ForStmt& node) {
    node.iterable->accept(*this);
    node.body->accept(*this);
}

void GenericCollector::visit(MatchStmt& node) {
    node.value->accept(*this);
    for (auto& c : node.cases) {
        if (c.pattern) c.pattern->accept(*this);
        if (c.guard) c.guard->accept(*this);
        if (c.body) c.body->accept(*this);
    }
    if (node.defaultCase) node.defaultCase->accept(*this);
}

void GenericCollector::visit(ReturnStmt& node) {
    if (node.value) node.value->accept(*this);
}

void GenericCollector::visit(TryStmt& node) {
    node.tryExpr->accept(*this);
    if (node.elseExpr) node.elseExpr->accept(*this);
}

void GenericCollector::visit(FnDecl& node) {
    // Don't process generic function bodies during collection
    // They will be processed when instantiated
    if (node.typeParams.empty() && node.body) {
        node.body->accept(*this);
    }
}

void GenericCollector::visit(RecordDecl& node) {
    // Generic records are handled during instantiation
}

void GenericCollector::visit(ImplBlock& node) {
    for (auto& method : node.methods) {
        if (method->body) {
            method->body->accept(*this);
        }
    }
}

void GenericCollector::visit(UnsafeBlock& node) {
    if (node.body) node.body->accept(*this);
}

void GenericCollector::visit(ModuleDecl& node) {
    for (auto& stmt : node.body) {
        stmt->accept(*this);
    }
}

void GenericCollector::visit(DeleteStmt& node) {
    node.expr->accept(*this);
}

// Syntax Redesign - New Expression Visitors
void GenericCollector::visit(InclusiveRangeExpr& node) {
    node.start->accept(*this);
    node.end->accept(*this);
    if (node.step) node.step->accept(*this);
}

void GenericCollector::visit(SafeNavExpr& node) {
    node.object->accept(*this);
}

void GenericCollector::visit(TypeCheckExpr& node) {
    node.value->accept(*this);
}

// Syntax Redesign - New Statement Visitors
void GenericCollector::visit(LoopStmt& node) {
    if (node.body) node.body->accept(*this);
}

void GenericCollector::visit(WithStmt& node) {
    node.resource->accept(*this);
    if (node.body) node.body->accept(*this);
}

void GenericCollector::visit(ScopeStmt& node) {
    if (node.timeout) node.timeout->accept(*this);
    if (node.body) node.body->accept(*this);
}

void GenericCollector::visit(RequireStmt& node) {
    node.condition->accept(*this);
}

void GenericCollector::visit(EnsureStmt& node) {
    node.condition->accept(*this);
}

void GenericCollector::visit(InvariantStmt& node) {
    node.condition->accept(*this);
}

void GenericCollector::visit(ComptimeBlock& node) {
    if (node.body) node.body->accept(*this);
}

void GenericCollector::visit(ComptimeAssertStmt& node) {
    // Visit the condition expression to collect any generic usages
    if (node.condition) node.condition->accept(*this);
}

void GenericCollector::visit(EffectDecl& node) {
    // Effect declarations don't contain generic expressions to collect
    (void)node;
}

void GenericCollector::visit(PerformEffectExpr& node) {
    for (auto& arg : node.args) {
        arg->accept(*this);
    }
}

void GenericCollector::visit(HandleExpr& node) {
    node.expr->accept(*this);
    for (auto& handler : node.handlers) {
        if (handler.body) {
            handler.body->accept(*this);
        }
    }
}

void GenericCollector::visit(ResumeExpr& node) {
    if (node.value) {
        node.value->accept(*this);
    }
}

// Compile-Time Reflection - these don't contain generic expressions to collect
void GenericCollector::visit(TypeMetadataExpr& node) {
    (void)node;
}

void GenericCollector::visit(FieldsOfExpr& node) {
    (void)node;
}

void GenericCollector::visit(MethodsOfExpr& node) {
    (void)node;
}

void GenericCollector::visit(HasFieldExpr& node) {
    if (node.fieldName) {
        node.fieldName->accept(*this);
    }
}

void GenericCollector::visit(HasMethodExpr& node) {
    if (node.methodName) {
        node.methodName->accept(*this);
    }
}

void GenericCollector::visit(FieldTypeExpr& node) {
    if (node.fieldName) {
        node.fieldName->accept(*this);
    }
}

// New Syntax Enhancements

void GenericCollector::visit(IfLetStmt& node) {
    if (node.value) {
        node.value->accept(*this);
    }
    if (node.guard) {
        node.guard->accept(*this);
    }
    if (node.thenBranch) {
        node.thenBranch->accept(*this);
    }
    if (node.elseBranch) {
        node.elseBranch->accept(*this);
    }
}

void GenericCollector::visit(MultiVarDecl& node) {
    if (node.initializer) {
        node.initializer->accept(*this);
    }
}

void GenericCollector::visit(WalrusExpr& node) {
    if (node.value) {
        node.value->accept(*this);
    }
}

} // namespace tyl
