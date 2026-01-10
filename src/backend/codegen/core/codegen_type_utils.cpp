// Tyl Compiler - Native Code Generator Type Utilities
// Handles: isFloatExpression, isStringReturningExpr, type size/alignment

#include "backend/codegen/codegen_base.h"
#include "semantic/types/types.h"

namespace tyl {

// Static helper to check if a type string represents a float type
bool NativeCodeGen::isFloatTypeName(const std::string& typeName) {
    return typeName == "float" || typeName == "f16" || typeName == "f32" || 
           typeName == "f64" || typeName == "f128";
}

// Static helper to check if a type string represents a complex type
bool NativeCodeGen::isComplexTypeName(const std::string& typeName) {
    return typeName == "c64" || typeName == "c128";
}

bool NativeCodeGen::isFloatExpression(Expression* expr) {
    if (dynamic_cast<FloatLiteral*>(expr)) return true;
    
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        if (floatVars.count(ident->name)) return true;
        if (constFloatVars.count(ident->name)) return true;
        
        // Check varTypes_ for explicit float type annotations
        auto typeIt = varTypes_.find(ident->name);
        if (typeIt != varTypes_.end() && isFloatTypeName(typeIt->second)) {
            return true;
        }
    }
    
    // Check for record field access - check if the field type is float
    if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        if (auto* objId = dynamic_cast<Identifier*>(member->object.get())) {
            auto varTypeIt = varRecordTypes_.find(objId->name);
            if (varTypeIt != varRecordTypes_.end()) {
                auto typeIt = recordTypes_.find(varTypeIt->second);
                if (typeIt != recordTypes_.end()) {
                    for (size_t i = 0; i < typeIt->second.fieldNames.size(); i++) {
                        if (typeIt->second.fieldNames[i] == member->member) {
                            const std::string& fieldType = typeIt->second.fieldTypes[i];
                            if (isFloatTypeName(fieldType)) {
                                return true;
                            }
                            break;
                        }
                    }
                }
            }
        }
    }
    
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        return isFloatExpression(binary->left.get()) || isFloatExpression(binary->right.get());
    }
    
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        return isFloatExpression(unary->operand.get());
    }
    
    if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        return isFloatExpression(ternary->thenExpr.get()) || isFloatExpression(ternary->elseExpr.get());
    }
    
    // Check for float() conversion function
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        if (auto* id = dynamic_cast<Identifier*>(call->callee.get())) {
            if (id->name == "float") return true;
            
            // Math functions that return float
            if (id->name == "sqrt" || id->name == "pow" ||
                id->name == "sin" || id->name == "cos" || id->name == "tan" ||
                id->name == "exp" || id->name == "log" || id->name == "lerp") {
                return true;
            }
            
            // Check if this is a comptime function - use its declared return type
            if (comptimeFunctions_.count(id->name)) {
                FnDecl* fn = ctfe_.getComptimeFunction(id->name);
                if (fn) {
                    // Check the declared return type
                    return isFloatTypeName(fn->returnType);
                }
            }
            
            // Check if this is a generic function call that returns float
            auto it = genericFunctions_.find(id->name);
            if (it != genericFunctions_.end() && !call->args.empty()) {
                FnDecl* genericFn = it->second;
                auto& reg = TypeRegistry::instance();
                
                std::unordered_map<std::string, TypePtr> inferred;
                for (size_t i = 0; i < call->args.size() && i < genericFn->params.size(); i++) {
                    const std::string& paramType = genericFn->params[i].second;
                    for (const auto& tp : genericFn->typeParams) {
                        if (paramType == tp) {
                            TypePtr argType = reg.anyType();
                            if (dynamic_cast<FloatLiteral*>(call->args[i].get())) {
                                argType = reg.floatType();
                            } else if (isFloatExpression(call->args[i].get())) {
                                argType = reg.floatType();
                            } else if (auto* argId = dynamic_cast<Identifier*>(call->args[i].get())) {
                                if (floatVars.count(argId->name) || constFloatVars.count(argId->name)) {
                                    argType = reg.floatType();
                                }
                            }
                            if (inferred.find(tp) == inferred.end()) {
                                inferred[tp] = argType;
                            }
                            break;
                        }
                    }
                }
                
                std::vector<TypePtr> typeArgs;
                for (const auto& tp : genericFn->typeParams) {
                    auto typeIt = inferred.find(tp);
                    if (typeIt != inferred.end()) {
                        typeArgs.push_back(typeIt->second);
                    } else {
                        typeArgs.push_back(reg.anyType());
                    }
                }
                
                std::string mangledName = monomorphizer_.getMangledName(id->name, typeArgs);
                if (monomorphizer_.functionReturnsFloat(mangledName)) {
                    return true;
                }
                
                std::string returnType = genericFn->returnType;
                for (size_t i = 0; i < genericFn->typeParams.size() && i < typeArgs.size(); i++) {
                    if (returnType == genericFn->typeParams[i]) {
                        if (typeArgs[i]->toString() == "float") {
                            return true;
                        }
                    }
                }
            }
        }
    }
    
    return false;
}

bool NativeCodeGen::isStringReturningExpr(Expression* expr) {
    if (!expr) return false;
    
    if (dynamic_cast<StringLiteral*>(expr)) return true;
    if (dynamic_cast<InterpolatedString*>(expr)) return true;
    
    // Compile-time reflection expressions that return strings
    if (dynamic_cast<TypeMetadataExpr*>(expr)) {
        auto* meta = dynamic_cast<TypeMetadataExpr*>(expr);
        return meta->metadataKind == "name";  // type_name returns string
    }
    if (dynamic_cast<FieldTypeExpr*>(expr)) return true;  // field_type returns string
    
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        // Check for method calls (obj.method())
        if (auto* member = dynamic_cast<MemberExpr*>(call->callee.get())) {
            // Get the object's type
            std::string objTypeName;
            if (auto* objId = dynamic_cast<Identifier*>(member->object.get())) {
                auto varTypeIt = varRecordTypes_.find(objId->name);
                if (varTypeIt != varRecordTypes_.end()) {
                    objTypeName = varTypeIt->second;
                }
            }
            
            // Look for impl method matching the object's type
            for (const auto& [implKey, info] : impls_) {
                if (!objTypeName.empty() && info.typeName != objTypeName) {
                    continue;
                }
                
                auto returnTypeIt = info.methodReturnTypes.find(member->member);
                if (returnTypeIt != info.methodReturnTypes.end()) {
                    const std::string& retType = returnTypeIt->second;
                    if (retType == "str" || retType == "string" ||
                        retType == "*str" || retType == "*u8") {
                        return true;
                    }
                }
            }
        }
        
        if (auto* id = dynamic_cast<Identifier*>(call->callee.get())) {
            // Check built-in string-returning functions
            if (id->name == "platform" || id->name == "arch" ||
                id->name == "upper" || id->name == "lower" ||
                id->name == "trim" || id->name == "substring" ||
                id->name == "replace" || id->name == "split" ||
                id->name == "join" || id->name == "hostname" ||
                id->name == "username" || id->name == "str" ||
                id->name == "read" ||
                // Extended string builtins
                id->name == "ltrim" || id->name == "rtrim" ||
                id->name == "char_at" || id->name == "repeat" ||
                id->name == "reverse_str" || id->name == "chr" ||
                // Extended system builtins that return strings
                id->name == "env" || id->name == "home_dir" ||
                id->name == "temp_dir") {
                return true;
            }
            
            // Check user-defined string-returning functions
            if (stringReturningFunctions_.count(id->name)) {
                return true;
            }
            
            auto it = genericFunctions_.find(id->name);
            if (it != genericFunctions_.end() && !call->args.empty()) {
                if (isStringReturningExpr(call->args[0].get())) {
                    return true;
                }
                if (auto* argId = dynamic_cast<Identifier*>(call->args[0].get())) {
                    if (constStrVars.count(argId->name)) {
                        return true;
                    }
                }
            }
        }
    }
    
    if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        return isStringReturningExpr(ternary->thenExpr.get()) || 
               isStringReturningExpr(ternary->elseExpr.get());
    }
    
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        if (constStrVars.count(ident->name)) return true;
    }
    
    return false;
}

int32_t NativeCodeGen::getTypeSize(const std::string& typeName) {
    // Primitive types
    if (typeName == "int" || typeName == "i64" || typeName == "u64" || 
        typeName == "float" || typeName == "f64") {
        return 8;
    }
    if (typeName == "i32" || typeName == "u32" || typeName == "f32") {
        return 4;
    }
    if (typeName == "i16" || typeName == "u16" || typeName == "f16") {
        return 2;
    }
    if (typeName == "i8" || typeName == "u8" || typeName == "bool") {
        return 1;
    }
    if (typeName == "void") {
        return 0;
    }
    // Extended precision floats
    if (typeName == "f128") {
        return 16;
    }
    // Complex numbers
    if (typeName == "c64") {
        return 8;   // 2x f32
    }
    if (typeName == "c128") {
        return 16;  // 2x f64
    }
    // Pointers and strings are 8 bytes
    if (typeName == "str" || typeName == "string" || (!typeName.empty() && typeName.front() == '*')) {
        return 8;
    }
    
    // Check for fixed-size array: [T; N]
    if (typeName.size() > 2 && typeName.front() == '[' && typeName.back() == ']') {
        std::string inner = typeName.substr(1, typeName.size() - 2);
        int bracketDepth = 0;
        size_t semicolonPos = std::string::npos;
        for (size_t i = 0; i < inner.size(); i++) {
            if (inner[i] == '[') bracketDepth++;
            else if (inner[i] == ']') bracketDepth--;
            else if (inner[i] == ';' && bracketDepth == 0) {
                semicolonPos = i;
                break;
            }
        }
        
        if (semicolonPos != std::string::npos) {
            std::string elemType = inner.substr(0, semicolonPos);
            std::string sizeStr = inner.substr(semicolonPos + 1);
            while (!sizeStr.empty() && (sizeStr[0] == ' ' || sizeStr[0] == '\t')) sizeStr = sizeStr.substr(1);
            while (!sizeStr.empty() && (sizeStr.back() == ' ' || sizeStr.back() == '\t')) sizeStr.pop_back();
            
            int32_t elemSize = getTypeSize(elemType);
            int64_t arraySize = std::stoll(sizeStr);
            return elemSize * static_cast<int32_t>(arraySize);
        }
    }
    
    // Check if it's a record type
    auto it = recordTypes_.find(typeName);
    if (it != recordTypes_.end()) {
        if (!it->second.offsetsComputed) {
            computeRecordLayout(it->second);
        }
        return it->second.totalSize;
    }
    // Default to pointer size for unknown types
    return 8;
}

int32_t NativeCodeGen::getTypeAlignment(const std::string& typeName) {
    // Primitive types - alignment equals size
    if (typeName == "int" || typeName == "i64" || typeName == "u64" || 
        typeName == "float" || typeName == "f64") {
        return 8;
    }
    if (typeName == "i32" || typeName == "u32" || typeName == "f32") {
        return 4;
    }
    if (typeName == "i16" || typeName == "u16") {
        return 2;
    }
    if (typeName == "i8" || typeName == "u8" || typeName == "bool") {
        return 1;
    }
    if (typeName == "void") {
        return 1;
    }
    // Pointers and strings have 8-byte alignment
    if (typeName == "str" || typeName == "string" || (!typeName.empty() && typeName.front() == '*')) {
        return 8;
    }
    
    // Check for fixed-size array: [T; N]
    if (typeName.size() > 2 && typeName.front() == '[' && typeName.back() == ']') {
        std::string inner = typeName.substr(1, typeName.size() - 2);
        int bracketDepth = 0;
        size_t semicolonPos = std::string::npos;
        for (size_t i = 0; i < inner.size(); i++) {
            if (inner[i] == '[') bracketDepth++;
            else if (inner[i] == ']') bracketDepth--;
            else if (inner[i] == ';' && bracketDepth == 0) {
                semicolonPos = i;
                break;
            }
        }
        
        if (semicolonPos != std::string::npos) {
            std::string elemType = inner.substr(0, semicolonPos);
            return getTypeAlignment(elemType);
        }
    }
    
    // Check if it's a record type
    auto it = recordTypes_.find(typeName);
    if (it != recordTypes_.end()) {
        if (!it->second.offsetsComputed) {
            computeRecordLayout(it->second);
        }
        // Record alignment is the max alignment of its fields
        int32_t maxAlign = 1;
        for (const auto& fieldType : it->second.fieldTypes) {
            maxAlign = std::max(maxAlign, getTypeAlignment(fieldType));
        }
        return maxAlign;
    }
    
    return 8;  // Default alignment
}

} // namespace tyl
