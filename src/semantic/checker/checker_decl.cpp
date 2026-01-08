// Tyl Compiler - Type Checker Declaration Visitors
// Declaration type checking

#include "checker_base.h"
#include <algorithm>

namespace tyl {

void TypeChecker::visit(FnDecl& node) {
    auto& reg = TypeRegistry::instance();
    auto fnType = std::make_shared<FunctionType>();
    
    // Handle generic type parameters
    fnType->typeParams = node.typeParams;
    
    // Push type parameters into scope, parsing constraints if present
    std::vector<std::string> savedTypeParamNames = currentTypeParamNames_;
    auto savedTypeParamConstraints = typeParamConstraints_;  // Save current constraints
    
    for (const auto& tp : node.typeParams) {
        std::string paramName = tp;
        std::vector<std::string> constraints;
        
        // Check for constraint syntax: "T: Numeric" or "T: Numeric + Orderable"
        size_t colonPos = tp.find(':');
        if (colonPos != std::string::npos && tp.find('[') == std::string::npos) {
            // Has constraint (but not HKT syntax like F[_])
            paramName = tp.substr(0, colonPos);
            // Trim whitespace
            while (!paramName.empty() && paramName.back() == ' ') paramName.pop_back();
            
            std::string constraintStr = tp.substr(colonPos + 1);
            // Parse constraints separated by +
            size_t pos = 0;
            while (pos < constraintStr.length()) {
                // Skip whitespace
                while (pos < constraintStr.length() && constraintStr[pos] == ' ') pos++;
                if (pos >= constraintStr.length()) break;
                
                // Find end of concept name
                size_t end = pos;
                while (end < constraintStr.length() && constraintStr[end] != ' ' && constraintStr[end] != '+') end++;
                
                if (end > pos) {
                    std::string conceptName = constraintStr.substr(pos, end - pos);
                    constraints.push_back(conceptName);
                    
                    // Validate concept exists
                    ConceptPtr concept = reg.lookupConcept(conceptName);
                    if (!concept) {
                        warning("Unknown concept '" + conceptName + "' in constraint for type parameter '" + paramName + "'", node.location);
                    }
                }
                
                pos = end;
                // Skip + and whitespace
                while (pos < constraintStr.length() && (constraintStr[pos] == ' ' || constraintStr[pos] == '+')) pos++;
            }
            
            if (!constraints.empty()) {
                typeParamConstraints_[paramName] = constraints;
            }
        }
        
        currentTypeParamNames_.push_back(paramName);
        // Create type parameter type
        auto tpType = std::make_shared<TypeParamType>(paramName);
        currentTypeParams_[paramName] = tpType;
    }
    
    // =========================================================================
    // Lifetime Elision Rules
    // =========================================================================
    // Apply lifetime elision rules for common patterns:
    // 1. Single input lifetime → output gets same lifetime
    // 2. &self method → output gets self's lifetime  
    // 3. Multiple inputs without &self → require explicit annotation (error)
    //
    // These rules allow omitting lifetime annotations in common cases.
    // =========================================================================
    
    // Count reference parameters and track their lifetimes
    std::vector<std::string> inputLifetimes;
    std::string selfLifetime;
    bool hasSelfParam = false;
    
    for (auto& p : node.params) {
        // Check if this is a reference parameter
        if (p.second.length() > 0 && p.second[0] == '&') {
            // Extract lifetime if present (e.g., "&'a T" -> "'a")
            std::string lifetime;
            size_t pos = 1;
            if (pos < p.second.length() && p.second[pos] == '\'') {
                // Has explicit lifetime
                size_t end = p.second.find(' ', pos);
                if (end == std::string::npos) end = p.second.length();
                lifetime = p.second.substr(pos, end - pos);
            } else {
                // No explicit lifetime - generate one for elision
                lifetime = "'_param" + std::to_string(inputLifetimes.size());
            }
            inputLifetimes.push_back(lifetime);
            
            // Check for self parameter
            if (p.first == "self") {
                hasSelfParam = true;
                selfLifetime = lifetime;
            }
        }
    }
    
    // Check if return type is a reference that needs lifetime elision
    bool returnIsRef = node.returnType.length() > 0 && node.returnType[0] == '&';
    bool returnHasExplicitLifetime = returnIsRef && node.returnType.length() > 1 && node.returnType[1] == '\'';
    
    // Apply elision rules if return type is a reference without explicit lifetime
    if (returnIsRef && !returnHasExplicitLifetime && !node.lifetimeParams.empty() == false) {
        if (hasSelfParam) {
            // Rule 2: &self method → output gets self's lifetime
            // This is the most common case for methods
            // The return type implicitly has self's lifetime
        } else if (inputLifetimes.size() == 1) {
            // Rule 1: Single input lifetime → output gets same lifetime
            // The return type implicitly has the same lifetime as the single input
        } else if (inputLifetimes.size() > 1) {
            // Rule 3: Multiple inputs without &self → require explicit annotation
            // Only warn if there are explicit lifetime params expected
            if (node.lifetimeParams.empty()) {
                warning("function returns a reference but has multiple input lifetimes; "
                        "consider adding explicit lifetime annotations", node.location);
            }
        }
    }
    
    // Parse parameters and track ownership modes
    std::vector<ParamOwnershipInfo> paramOwnership;
    for (auto& p : node.params) {
        TypePtr paramType = parseTypeAnnotation(p.second);
        if (paramType->kind == TypeKind::UNKNOWN) paramType = reg.anyType();
        fnType->params.push_back(std::make_pair(p.first, paramType));
        
        // Determine parameter passing mode from type annotation
        ParamOwnershipInfo poi;
        poi.name = p.first;
        poi.typeName = p.second;
        poi.mode = parseParamMode(p.second);
        
        // Set lifetime for borrowed parameters
        if (poi.mode == ParamMode::BORROW || poi.mode == ParamMode::BORROW_MUT) {
            // Find the corresponding input lifetime
            size_t refIndex = 0;
            for (size_t i = 0; i < node.params.size(); i++) {
                if (node.params[i].second.length() > 0 && node.params[i].second[0] == '&') {
                    if (node.params[i].first == p.first) {
                        if (refIndex < inputLifetimes.size()) {
                            poi.lifetime.name = inputLifetimes[refIndex];
                        }
                        break;
                    }
                    refIndex++;
                }
            }
        }
        
        paramOwnership.push_back(poi);
    }
    fnType->returnType = parseTypeAnnotation(node.returnType);
    if (fnType->returnType->kind == TypeKind::UNKNOWN) fnType->returnType = reg.anyType();
    
    Symbol fnSym(node.name, SymbolKind::FUNCTION, fnType);
    symbols_.define(fnSym);
    
    symbols_.pushScope(Scope::Kind::FUNCTION);
    
    // Enter function for ownership tracking
    if (borrowCheckEnabled_) {
        ownership_.pushScope();
        ownership_.enterFunction(paramOwnership);
    }
    
    for (size_t i = 0; i < node.params.size(); i++) {
        Symbol paramSym(node.params[i].first, SymbolKind::PARAMETER, fnType->params[i].second);
        paramSym.location = node.location;  // Use function location for params
        paramSym.isParameter = true;
        symbols_.define(paramSym);
    }
    expectedReturn_ = fnType->returnType;
    if (node.body) {
        node.body->accept(*this);
    }
    checkUnusedVariables(symbols_.currentScope());  // Check for unused variables/params
    
    // Exit function for ownership tracking
    if (borrowCheckEnabled_) {
        ownership_.exitFunction();
        ownership_.popScope();
    }
    
    symbols_.popScope();
    
    // Restore type parameter scope
    for (const auto& tp : node.typeParams) {
        // Extract parameter name (handle "T: Concept" syntax)
        std::string paramName = tp;
        size_t colonPos = tp.find(':');
        if (colonPos != std::string::npos && tp.find('[') == std::string::npos) {
            paramName = tp.substr(0, colonPos);
            while (!paramName.empty() && paramName.back() == ' ') paramName.pop_back();
        }
        currentTypeParams_.erase(paramName);
        typeParamConstraints_.erase(paramName);
    }
    currentTypeParamNames_ = savedTypeParamNames;
    typeParamConstraints_ = savedTypeParamConstraints;
}

void TypeChecker::visit(RecordDecl& node) {
    auto& reg = TypeRegistry::instance();
    auto recType = std::make_shared<RecordType>(node.name);
    
    // Handle generic type parameters
    std::vector<std::string> savedTypeParamNames = currentTypeParamNames_;
    for (const auto& tp : node.typeParams) {
        currentTypeParamNames_.push_back(tp);
        auto tpType = std::make_shared<TypeParamType>(tp);
        currentTypeParams_[tp] = tpType;
    }
    
    for (auto& f : node.fields) {
        TypePtr fieldType = parseTypeAnnotation(f.second);
        recType->fields.push_back({f.first, fieldType, false});
    }
    
    symbols_.registerType(node.name, recType);
    Symbol typeSym(node.name, SymbolKind::TYPE, recType);
    symbols_.define(typeSym);
    
    // Restore type parameter scope
    for (const auto& tp : node.typeParams) {
        currentTypeParams_.erase(tp);
    }
    currentTypeParamNames_ = savedTypeParamNames;
}

void TypeChecker::visit(UnionDecl& node) {
    auto& reg = TypeRegistry::instance();
    // Unions use RecordType internally but with different memory layout
    auto unionType = std::make_shared<RecordType>(node.name);
    
    // Handle generic type parameters
    std::vector<std::string> savedTypeParamNames = currentTypeParamNames_;
    for (const auto& tp : node.typeParams) {
        currentTypeParamNames_.push_back(tp);
        auto tpType = std::make_shared<TypeParamType>(tp);
        currentTypeParams_[tp] = tpType;
    }
    
    for (auto& f : node.fields) {
        TypePtr fieldType = parseTypeAnnotation(f.second);
        unionType->fields.push_back({f.first, fieldType, false});
    }
    
    symbols_.registerType(node.name, unionType);
    Symbol typeSym(node.name, SymbolKind::TYPE, unionType);
    symbols_.define(typeSym);
    
    // Restore type parameter scope
    for (const auto& tp : node.typeParams) {
        currentTypeParams_.erase(tp);
    }
    currentTypeParamNames_ = savedTypeParamNames;
}

void TypeChecker::visit(EnumDecl& node) {
    auto& reg = TypeRegistry::instance();
    auto enumType = std::make_shared<Type>(TypeKind::INT);
    symbols_.registerType(node.name, enumType);
    int64_t nextValue = 0;
    for (auto& v : node.variants) {
        int64_t actualValue = v.second.value_or(nextValue);
        Symbol variantSym(node.name + "." + v.first, SymbolKind::VARIABLE, reg.intType());
        variantSym.isMutable = false;
        symbols_.define(variantSym);
        nextValue = actualValue + 1;
    }
}

void TypeChecker::visit(TypeAlias& node) {
    auto& reg = TypeRegistry::instance();
    
    // Handle type parameters (including value parameters for dependent types)
    std::vector<std::string> savedTypeParamNames = currentTypeParamNames_;
    std::vector<std::pair<std::string, TypePtr>> depParams;
    
    for (const auto& tp : node.typeParams) {
        currentTypeParamNames_.push_back(tp.name);
        if (tp.isValue) {
            // Value parameter (e.g., N: int)
            TypePtr valueType = parseTypeAnnotation(tp.kind);
            auto vpType = reg.valueParamType(tp.name, valueType);
            currentTypeParams_[tp.name] = vpType;
            depParams.push_back({tp.name, valueType});
        } else {
            // Regular type parameter
            auto tpType = std::make_shared<TypeParamType>(tp.name);
            currentTypeParams_[tp.name] = tpType;
            depParams.push_back({tp.name, nullptr});  // nullptr indicates type param
        }
    }
    
    TypePtr targetType;
    if (node.targetType == "opaque") {
        // Opaque types are treated as void* for FFI purposes
        targetType = reg.ptrType(reg.voidType(), true);
    } else {
        targetType = parseTypeAnnotation(node.targetType);
    }
    
    // Check if this is a dependent type (has value parameters)
    bool hasDependentParams = false;
    for (const auto& tp : node.typeParams) {
        if (tp.isValue) {
            hasDependentParams = true;
            break;
        }
    }
    
    // Check if this is a refined type (has constraint)
    if (node.constraint) {
        // Create a refined type
        // For now, we store the constraint as a string representation
        std::string constraintStr;
        // Simple constraint stringification - in a full implementation,
        // we'd have a proper AST-to-string converter
        if (auto* binExpr = dynamic_cast<BinaryExpr*>(node.constraint.get())) {
            if (auto* call = dynamic_cast<CallExpr*>(binExpr->left.get())) {
                if (auto* id = dynamic_cast<Identifier*>(call->callee.get())) {
                    constraintStr = id->name + "(_)";
                }
            }
            constraintStr += " > ";
            if (auto* intLit = dynamic_cast<IntegerLiteral*>(binExpr->right.get())) {
                constraintStr += std::to_string(intLit->value);
            }
        }
        
        auto refinedType = reg.refinedType(node.name, targetType, constraintStr);
        symbols_.registerType(node.name, refinedType);
        reg.registerDependentType(node.name, refinedType);
    } else if (hasDependentParams) {
        // Create a dependent type
        auto depType = std::dynamic_pointer_cast<DependentType>(reg.dependentType(node.name));
        if (depType) {
            depType->params = depParams;
            depType->baseType = targetType;
            symbols_.registerType(node.name, depType);
            reg.registerDependentType(node.name, depType);
        }
    } else if (!node.typeParams.empty()) {
        // Generic type alias (no value params)
        auto genType = std::make_shared<GenericType>(node.name);
        for (const auto& tp : node.typeParams) {
            genType->typeArgs.push_back(reg.typeParamType(tp.name));
        }
        // Store the resolved type for later instantiation
        symbols_.registerType(node.name, targetType);
    } else {
        // Simple type alias
        symbols_.registerType(node.name, targetType);
    }
    
    // Restore type parameter scope
    for (const auto& tp : node.typeParams) {
        currentTypeParams_.erase(tp.name);
    }
    currentTypeParamNames_ = savedTypeParamNames;
}

void TypeChecker::visit(TraitDecl& node) {
    auto& reg = TypeRegistry::instance();
    auto trait = std::make_shared<TraitType>(node.name);
    trait->typeParams = node.typeParams;
    trait->superTraits = node.superTraits;
    
    // Push type parameters into scope for method parsing
    std::vector<std::string> savedTypeParamNames = currentTypeParamNames_;
    for (const auto& tp : node.typeParams) {
        currentTypeParamNames_.push_back(tp);
        auto tpType = std::make_shared<TypeParamType>(tp);
        currentTypeParams_[tp] = tpType;
    }
    
    // Handle Higher-Kinded Type parameters (F[_], M[_, _], etc.)
    for (const auto& hktParam : node.hktTypeParams) {
        currentTypeParamNames_.push_back(hktParam.name);
        auto tcType = std::make_shared<TypeConstructorType>(hktParam.name, hktParam.arity);
        tcType->bounds = hktParam.bounds;
        currentTypeParams_[hktParam.name] = tcType;
        
        // Register the type constructor
        reg.registerTypeConstructor(hktParam.name, tcType);
        
        // Also add to trait's type params for serialization
        std::string hktStr = hktParam.name + "[";
        for (size_t i = 0; i < hktParam.arity; i++) {
            if (i > 0) hktStr += ", ";
            hktStr += "_";
        }
        hktStr += "]";
        trait->typeParams.push_back(hktStr);
    }
    
    // Add implicit Self type parameter
    currentTypeParamNames_.push_back("Self");
    currentTypeParams_["Self"] = reg.typeParamType("Self");
    
    // Validate super traits exist
    for (const auto& superTrait : node.superTraits) {
        TraitPtr superTraitType = reg.lookupTrait(superTrait);
        if (!superTraitType) {
            error("Unknown super trait '" + superTrait + "'", node.location);
        }
    }
    
    // Process trait methods
    for (auto& method : node.methods) {
        TraitMethod tm;
        tm.name = method->name;
        tm.signature = std::make_shared<FunctionType>();
        tm.hasDefaultImpl = (method->body != nullptr);
        
        for (auto& p : method->params) {
            TypePtr paramType = parseTypeAnnotation(p.second);
            if (paramType->kind == TypeKind::UNKNOWN) paramType = reg.anyType();
            tm.signature->params.push_back({p.first, paramType});
        }
        tm.signature->returnType = parseTypeAnnotation(method->returnType);
        if (tm.signature->returnType->kind == TypeKind::UNKNOWN) {
            tm.signature->returnType = reg.voidType();
        }
        
        trait->methods.push_back(tm);
    }
    
    // Register the trait
    reg.registerTrait(node.name, trait);
    
    // Also register as a type for trait object usage
    Symbol traitSym(node.name, SymbolKind::TYPE, trait);
    symbols_.define(traitSym);
    
    // Restore type parameter scope
    currentTypeParams_.erase("Self");
    for (const auto& tp : node.typeParams) {
        currentTypeParams_.erase(tp);
    }
    for (const auto& hktParam : node.hktTypeParams) {
        currentTypeParams_.erase(hktParam.name);
    }
    currentTypeParamNames_ = savedTypeParamNames;
}

void TypeChecker::visit(ImplBlock& node) {
    auto& reg = TypeRegistry::instance();
    
    // Push type parameters into scope
    std::vector<std::string> savedTypeParamNames = currentTypeParamNames_;
    for (const auto& tp : node.typeParams) {
        currentTypeParamNames_.push_back(tp);
        auto tpType = std::make_shared<TypeParamType>(tp);
        currentTypeParams_[tp] = tpType;
    }
    
    // Resolve the implementing type
    TypePtr implType = parseTypeAnnotation(node.typeName);
    
    // Add Self as alias for the implementing type
    currentTypeParams_["Self"] = implType;
    
    // For HKT trait implementations, we need to handle the type constructor
    // e.g., "impl Functor for Container" where Functor[F[_]] and Container is F
    bool isHKTImpl = false;
    TraitPtr trait = nullptr;
    if (!node.traitName.empty()) {
        trait = reg.lookupTrait(node.traitName);
        if (trait) {
            // Check if this trait has HKT type parameters
            for (const auto& tp : trait->typeParams) {
                if (tp.find("[_]") != std::string::npos || tp.find("[_, _]") != std::string::npos) {
                    isHKTImpl = true;
                    break;
                }
            }
        }
        
        // This is a trait implementation
        checkTraitImpl(node.traitName, node.typeName, node.methods, node.location);
        
        // Register Drop trait implementation in ownership system
        if (node.traitName == "Drop") {
            // Find the drop method
            for (auto& method : node.methods) {
                if (method->name == "drop") {
                    // Register this type as having a custom drop
                    std::string dropFnName = node.typeName + "_Drop_drop";
                    OwnershipTracker::registerDropType(node.typeName, dropFnName);
                    break;
                }
            }
        }
    } else {
        // Inherent impl (no trait) - register methods for lookup
        TraitImpl impl;
        impl.traitName = "";  // Empty means inherent impl
        impl.typeName = node.typeName;
        
        for (auto& method : node.methods) {
            // Add method type parameters to scope before parsing types
            std::vector<std::string> savedMethodTypeParams = currentTypeParamNames_;
            auto savedMethodParams = currentTypeParams_;
            
            for (const auto& tp : method->typeParams) {
                currentTypeParamNames_.push_back(tp);
                auto tpType = std::make_shared<TypeParamType>(tp);
                currentTypeParams_[tp] = tpType;
            }
            
            auto fnType = std::make_shared<FunctionType>();
            fnType->typeParams = method->typeParams;
            for (const auto& p : method->params) {
                fnType->params.push_back({p.first, parseTypeAnnotation(p.second)});
            }
            fnType->returnType = parseTypeAnnotation(method->returnType);
            impl.methods[method->name] = fnType;
            
            // Restore type parameter scope
            currentTypeParamNames_ = savedMethodTypeParams;
            currentTypeParams_ = savedMethodParams;
        }
        reg.registerTraitImpl(impl);
    }
    
    // Type check all methods
    for (auto& method : node.methods) {
        // Create qualified method name for the type
        std::string qualifiedName = node.typeName + "." + method->name;
        
        // For HKT impls, method type parameters need to be in scope
        // e.g., fn map[A, B] self: Container[A], f: fn(A) -> B -> Container[B]
        std::vector<std::string> methodTypeParams;
        for (const auto& tp : method->typeParams) {
            methodTypeParams.push_back(tp);
            currentTypeParamNames_.push_back(tp);
            auto tpType = std::make_shared<TypeParamType>(tp);
            currentTypeParams_[tp] = tpType;
        }
        
        auto fnType = std::make_shared<FunctionType>();
        fnType->typeParams = method->typeParams;
        
        for (auto& p : method->params) {
            TypePtr paramType = parseTypeAnnotation(p.second);
            if (paramType->kind == TypeKind::UNKNOWN) paramType = reg.anyType();
            fnType->params.push_back({p.first, paramType});
        }
        fnType->returnType = parseTypeAnnotation(method->returnType);
        if (fnType->returnType->kind == TypeKind::UNKNOWN) {
            fnType->returnType = reg.anyType();
        }
        
        Symbol methodSym(qualifiedName, SymbolKind::FUNCTION, fnType);
        symbols_.define(methodSym);
        
        // Type check method body
        symbols_.pushScope(Scope::Kind::FUNCTION);
        
        // Add 'self' parameter if present
        for (size_t i = 0; i < method->params.size(); i++) {
            TypePtr paramType = fnType->params[i].second;
            if (method->params[i].first == "self") {
                paramType = implType;  // self has the implementing type
            }
            Symbol paramSym(method->params[i].first, SymbolKind::PARAMETER, paramType);
            symbols_.define(paramSym);
        }
        
        expectedReturn_ = fnType->returnType;
        if (method->body) {
            method->body->accept(*this);
        }
        symbols_.popScope();
        
        // Remove method type parameters from scope
        for (const auto& tp : methodTypeParams) {
            currentTypeParams_.erase(tp);
            currentTypeParamNames_.erase(
                std::remove(currentTypeParamNames_.begin(), currentTypeParamNames_.end(), tp),
                currentTypeParamNames_.end()
            );
        }
    }
    
    // Restore type parameter scope
    currentTypeParams_.erase("Self");
    for (const auto& tp : node.typeParams) {
        currentTypeParams_.erase(tp);
    }
    currentTypeParamNames_ = savedTypeParamNames;
}

void TypeChecker::visit(ImportStmt&) {}

void TypeChecker::visit(ExternDecl& node) {
    auto& reg = TypeRegistry::instance();
    for (auto& fn : node.functions) {
        auto fnType = std::make_shared<FunctionType>();
        for (auto& p : fn->params) {
            TypePtr paramType = parseTypeAnnotation(p.second);
            if (paramType->kind == TypeKind::UNKNOWN) paramType = reg.anyType();
            fnType->params.push_back(std::make_pair(p.first, paramType));
        }
        fnType->returnType = parseTypeAnnotation(fn->returnType);
        if (fnType->returnType->kind == TypeKind::UNKNOWN) fnType->returnType = reg.voidType();
        Symbol fnSym(fn->name, SymbolKind::FUNCTION, fnType);
        symbols_.define(fnSym);
    }
}

void TypeChecker::visit(MacroDecl& node) {
    Symbol macroSym(node.name, SymbolKind::MACRO, TypeRegistry::instance().anyType());
    symbols_.define(macroSym);
}

void TypeChecker::visit(LayerDecl& node) {
    Symbol layerSym(node.name, SymbolKind::LAYER, TypeRegistry::instance().anyType());
    symbols_.define(layerSym);
}

void TypeChecker::visit(UseStmt&) {}
void TypeChecker::visit(ModuleDecl& node) {
    auto& reg = TypeRegistry::instance();
    
    // Create a module type to represent the module
    auto moduleType = std::make_shared<Type>(TypeKind::ANY);  // Use ANY for now
    
    // Register the module name as a symbol so it can be referenced
    Symbol moduleSym(node.name, SymbolKind::MODULE, moduleType);
    symbols_.define(moduleSym);
    
    // Type check all declarations in the module and register qualified names
    for (auto& stmt : node.body) {
        // For functions, also register with qualified name (module.function)
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            // First visit the function to register it
            stmt->accept(*this);
            
            // Also register with qualified name for module.function() calls
            Symbol* fnSym = symbols_.lookup(fn->name);
            if (fnSym) {
                std::string qualifiedName = node.name + "." + fn->name;
                Symbol qualifiedSym(qualifiedName, SymbolKind::FUNCTION, fnSym->type);
                symbols_.define(qualifiedSym);
            }
        } else {
            stmt->accept(*this);
        }
    }
}

void TypeChecker::visit(ConceptDecl& node) {
    auto& reg = TypeRegistry::instance();
    
    // Create the concept type
    auto concept = reg.conceptType(node.name);
    concept->typeParams = node.typeParams;
    
    // Push type parameters into scope for requirement parsing
    std::vector<std::string> savedTypeParamNames = currentTypeParamNames_;
    for (const auto& tp : node.typeParams) {
        currentTypeParamNames_.push_back(tp);
        auto tpType = std::make_shared<TypeParamType>(tp);
        currentTypeParams_[tp] = tpType;
    }
    
    // Validate super concepts exist
    for (const auto& superConcept : node.superConcepts) {
        ConceptPtr superConceptType = reg.lookupConcept(superConcept);
        if (!superConceptType) {
            error("Unknown super concept '" + superConcept + "'", node.location);
        }
    }
    
    // Process concept requirements (function signatures)
    for (const auto& req : node.requirements) {
        ConceptRequirementType reqType;
        reqType.name = req.name;
        reqType.isStatic = req.isStatic;
        
        // Create function signature for the requirement
        reqType.signature = std::make_shared<FunctionType>();
        
        // Parse parameter types
        for (const auto& param : req.params) {
            TypePtr paramType = parseTypeAnnotation(param.second);
            if (paramType->kind == TypeKind::UNKNOWN) paramType = reg.anyType();
            reqType.signature->params.push_back({param.first, paramType});
        }
        
        // Parse return type
        reqType.signature->returnType = parseTypeAnnotation(req.returnType);
        if (reqType.signature->returnType->kind == TypeKind::UNKNOWN) {
            reqType.signature->returnType = reg.voidType();
        }
        
        concept->requirements.push_back(reqType);
    }
    
    // Register the concept
    reg.registerConcept(node.name, concept);
    
    // Also register as a type symbol for constraint usage
    Symbol conceptSym(node.name, SymbolKind::TYPE, concept);
    symbols_.define(conceptSym);
    
    // Restore type parameter scope
    for (const auto& tp : node.typeParams) {
        currentTypeParams_.erase(tp);
    }
    currentTypeParamNames_ = savedTypeParamNames;
}

} // namespace tyl
