// Tyl Compiler - Native Code Generator Type Declarations
// Handles: RecordDecl, EnumDecl, TraitDecl, ImplBlock, TypeAlias, ExternDecl

#include "backend/codegen/codegen_base.h"

namespace tyl {

void NativeCodeGen::visit(RecordDecl& node) {
    // Store record type information for field access
    RecordTypeInfo info;
    info.name = node.name;
    info.reprC = node.reprC;
    info.reprPacked = node.reprPacked;
    info.reprAlign = node.reprAlign;
    info.isUnion = false;
    info.hasBitfields = false;
    
    // Also register type metadata for compile-time reflection
    TypeMetadata metadata;
    metadata.name = node.name;
    
    for (size_t i = 0; i < node.fields.size(); i++) {
        const auto& [fieldName, fieldType] = node.fields[i];
        info.fieldNames.push_back(fieldName);
        info.fieldTypes.push_back(fieldType);
        
        // Add to reflection metadata
        TypeFieldInfo fieldInfo;
        fieldInfo.name = fieldName;
        fieldInfo.typeName = fieldType;
        metadata.fields.push_back(fieldInfo);
        
        // Handle bitfield specification
        int bitWidth = 0;
        if (i < node.bitfields.size() && node.bitfields[i].isBitfield()) {
            bitWidth = node.bitfields[i].bitWidth;
            info.hasBitfields = true;
        }
        info.fieldBitWidths.push_back(bitWidth);
        info.fieldBitOffsets.push_back(0);  // Will be computed in computeRecordLayout
    }
    
    recordTypes_[node.name] = info;
    
    // Calculate size and alignment for metadata
    // This is a simplified calculation - actual layout may differ
    size_t totalSize = 0;
    size_t maxAlign = 1;
    for (const auto& fieldType : info.fieldTypes) {
        size_t fieldSize = 8;  // Default to 8 bytes
        size_t fieldAlign = 8;
        if (fieldType == "int" || fieldType == "i64" || fieldType == "u64" || 
            fieldType == "float" || fieldType == "f64") {
            fieldSize = 8; fieldAlign = 8;
        } else if (fieldType == "i32" || fieldType == "u32" || fieldType == "f32") {
            fieldSize = 4; fieldAlign = 4;
        } else if (fieldType == "i16" || fieldType == "u16") {
            fieldSize = 2; fieldAlign = 2;
        } else if (fieldType == "i8" || fieldType == "u8" || fieldType == "bool") {
            fieldSize = 1; fieldAlign = 1;
        }
        
        // Align the current offset
        if (!info.reprPacked) {
            totalSize = (totalSize + fieldAlign - 1) & ~(fieldAlign - 1);
        }
        totalSize += fieldSize;
        if (fieldAlign > maxAlign) maxAlign = fieldAlign;
    }
    
    // Final alignment
    if (!info.reprPacked) {
        totalSize = (totalSize + maxAlign - 1) & ~(maxAlign - 1);
    }
    
    metadata.size = totalSize;
    metadata.alignment = info.reprAlign > 0 ? static_cast<size_t>(info.reprAlign) : maxAlign;
    
    // Register with CTFE interpreter for compile-time reflection
    ctfe_.registerTypeMetadata(node.name, metadata);
    
    // Generate derived trait implementations
    for (const auto& trait : node.deriveTraits) {
        if (trait == "Debug") {
            // Generate Debug trait implementation: fn debug(self: &T) -> str
            // For now, we'll generate a simple implementation that prints field names and values
            std::string debugLabel = node.name + "_Debug_debug";
            asm_.labels[debugLabel] = 0;  // Reserve label
            
            // Store impl info for trait dispatch
            std::string implKey = "Debug:" + node.name;
            ImplInfo implInfo;
            implInfo.traitName = "Debug";
            implInfo.typeName = node.name;
            implInfo.methodLabels["debug"] = debugLabel;
            impls_[implKey] = implInfo;
        }
        else if (trait == "Clone") {
            // Generate Clone trait implementation: fn clone(self: &T) -> T
            std::string cloneLabel = node.name + "_Clone_clone";
            asm_.labels[cloneLabel] = 0;  // Reserve label
            
            // Store impl info for trait dispatch
            std::string implKey = "Clone:" + node.name;
            ImplInfo implInfo;
            implInfo.traitName = "Clone";
            implInfo.typeName = node.name;
            implInfo.methodLabels["clone"] = cloneLabel;
            impls_[implKey] = implInfo;
        }
        else if (trait == "Eq") {
            // Generate Eq trait implementation: fn eq(self: &T, other: &T) -> bool
            std::string eqLabel = node.name + "_Eq_eq";
            asm_.labels[eqLabel] = 0;  // Reserve label
            
            // Store impl info for trait dispatch
            std::string implKey = "Eq:" + node.name;
            ImplInfo implInfo;
            implInfo.traitName = "Eq";
            implInfo.typeName = node.name;
            implInfo.methodLabels["eq"] = eqLabel;
            impls_[implKey] = implInfo;
        }
    }
}

void NativeCodeGen::visit(UnionDecl& node) {
    // Store union type information - unions have all fields at offset 0
    RecordTypeInfo info;
    info.name = node.name;
    info.reprC = node.reprC;
    info.reprPacked = false;  // Packed doesn't apply to unions
    info.reprAlign = node.reprAlign;
    info.isUnion = true;      // Mark as union for offset calculation
    
    for (auto& [fieldName, fieldType] : node.fields) {
        info.fieldNames.push_back(fieldName);
        info.fieldTypes.push_back(fieldType);
    }
    
    recordTypes_[node.name] = info;
}
void NativeCodeGen::visit(UseStmt& node) { (void)node; }
void NativeCodeGen::visit(EnumDecl& node) {
    // Store enum variant values as compile-time constants
    int64_t nextValue = 0;
    for (auto& [variantName, explicitValue] : node.variants) {
        int64_t actualValue = explicitValue.value_or(nextValue);
        std::string qualifiedName = node.name + "." + variantName;
        constVars[qualifiedName] = actualValue;
        nextValue = actualValue + 1;
    }
}
void NativeCodeGen::visit(TypeAlias& node) {
    // Check if this is a refinement type (has a constraint)
    if (node.constraint) {
        RefinementTypeInfo info;
        info.name = node.name;
        info.baseType = node.targetType;
        info.constraint = node.constraint.get();
        refinementTypes_[node.name] = info;
    }
    
    // Check if this is a dependent type (has value parameters)
    bool hasDependentParams = false;
    for (const auto& tp : node.typeParams) {
        if (tp.isValue) {
            hasDependentParams = true;
            break;
        }
    }
    
    if (hasDependentParams) {
        // Store dependent type information for later instantiation
        DependentTypeInfo info;
        info.name = node.name;
        info.baseType = node.targetType;
        for (const auto& tp : node.typeParams) {
            info.params.push_back({tp.name, tp.kind, tp.isValue});
        }
        dependentTypes_[node.name] = info;
    }
}

void NativeCodeGen::visit(TraitDecl& node) {
    TraitInfo info;
    info.name = node.name;
    for (auto& method : node.methods) {
        info.methodNames.push_back(method->name);
    }
    info.superTraits = node.superTraits;
    traits_[node.name] = info;
}

void NativeCodeGen::visit(ImplBlock& node) {
    std::string implKey = node.traitName + ":" + node.typeName;
    ImplInfo info;
    info.traitName = node.traitName;
    info.typeName = node.typeName;
    
    // Check if this is an HKT trait implementation
    bool isHKTImpl = false;
    if (!node.traitName.empty()) {
        auto traitIt = traits_.find(node.traitName);
        if (traitIt != traits_.end()) {
            // Check if any type param has HKT syntax
            for (const auto& tp : traitIt->second.methodNames) {
                // HKT traits have methods with type parameters
                isHKTImpl = true;  // Assume HKT for now if trait exists
                break;
            }
        }
    }
    
    for (auto& method : node.methods) {
        std::string mangledName;
        if (!node.traitName.empty()) {
            mangledName = node.typeName + "_" + node.traitName + "_" + method->name;
        } else {
            mangledName = node.typeName + "_" + method->name;
        }
        
        std::string originalName = method->name;
        method->name = mangledName;
        
        // Set current impl type so self parameter gets correct record type
        currentImplTypeName_ = node.typeName;
        method->accept(*this);
        currentImplTypeName_.clear();
        
        method->name = originalName;
        
        info.methodLabels[originalName] = mangledName;
        info.methodReturnTypes[originalName] = method->returnType;
        
        // Track string-returning methods
        if (method->returnType == "str" || method->returnType == "string" ||
            method->returnType == "*str" || method->returnType == "*u8") {
            stringReturningFunctions_.insert(mangledName);
        }
    }
    
    impls_[implKey] = info;
    
    // Also register with just the type name for method lookup
    // This allows c.map(...) to find Container_Functor_map
    if (!node.traitName.empty()) {
        std::string typeOnlyKey = ":" + node.typeName;
        auto& typeInfo = impls_[typeOnlyKey];
        typeInfo.typeName = node.typeName;
        for (const auto& [methodName, label] : info.methodLabels) {
            typeInfo.methodLabels[methodName] = label;
        }
    }
    
    // Vtable generation is deferred to finalizeVtables() after all code is emitted
    // This ensures function addresses are known
}

void NativeCodeGen::visit(UnsafeBlock& node) {
    node.body->accept(*this);
}

void NativeCodeGen::visit(ImportStmt& node) { (void)node; }

void NativeCodeGen::visit(ExternDecl& node) {
    for (auto& fn : node.functions) {
        // Only add import if library is specified
        if (!node.library.empty()) {
            pe_.addImport(node.library, fn->name);
        }
        externFunctions_[fn->name] = 0;
    }
}

void NativeCodeGen::visit(MacroDecl& node) { (void)node; }
void NativeCodeGen::visit(SyntaxMacroDecl& node) { (void)node; }
void NativeCodeGen::visit(LayerDecl& node) { (void)node; }
void NativeCodeGen::visit(ConceptDecl& node) { (void)node; }  // Concepts are compile-time only

void NativeCodeGen::visit(ModuleDecl& node) {
    std::string savedModule = currentModule_;
    currentModule_ = node.name;
    
    std::vector<FnDecl*> moduleFns;
    for (auto& stmt : node.body) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            std::string mangledName = node.name + "." + fn->name;
            moduleFunctions_[node.name].push_back(fn->name);
            
            fn->name = mangledName;
            
            asm_.labels[mangledName] = 0;
            
            moduleFns.push_back(fn);
        }
    }
    
    currentModule_ = savedModule;
}

// Collect captured variables from an expression
void NativeCodeGen::collectCapturedVariables(Expression* expr, const std::set<std::string>& params, std::set<std::string>& captured) {
    if (!expr) return;
    
    if (auto* id = dynamic_cast<Identifier*>(expr)) {
        if (params.find(id->name) == params.end() && asm_.labels.find(id->name) == asm_.labels.end()) {
            captured.insert(id->name);
        }
        return;
    }
    
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        collectCapturedVariables(binary->left.get(), params, captured);
        collectCapturedVariables(binary->right.get(), params, captured);
        return;
    }
    
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        collectCapturedVariables(unary->operand.get(), params, captured);
        return;
    }
    
    if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        collectCapturedVariables(ternary->condition.get(), params, captured);
        collectCapturedVariables(ternary->thenExpr.get(), params, captured);
        collectCapturedVariables(ternary->elseExpr.get(), params, captured);
        return;
    }
    
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        collectCapturedVariables(call->callee.get(), params, captured);
        for (auto& arg : call->args) {
            collectCapturedVariables(arg.get(), params, captured);
        }
        return;
    }
    
    if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        collectCapturedVariables(index->object.get(), params, captured);
        collectCapturedVariables(index->index.get(), params, captured);
        return;
    }
    
    if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        collectCapturedVariables(member->object.get(), params, captured);
        return;
    }
    
    if (auto* list = dynamic_cast<ListExpr*>(expr)) {
        for (auto& elem : list->elements) {
            collectCapturedVariables(elem.get(), params, captured);
        }
        return;
    }
    
    if (auto* record = dynamic_cast<RecordExpr*>(expr)) {
        for (auto& field : record->fields) {
            collectCapturedVariables(field.second.get(), params, captured);
        }
        return;
    }
    
    if (auto* map = dynamic_cast<MapExpr*>(expr)) {
        for (auto& entry : map->entries) {
            collectCapturedVariables(entry.first.get(), params, captured);
            collectCapturedVariables(entry.second.get(), params, captured);
        }
        return;
    }
    
    if (auto* assign = dynamic_cast<AssignExpr*>(expr)) {
        collectCapturedVariables(assign->target.get(), params, captured);
        collectCapturedVariables(assign->value.get(), params, captured);
        return;
    }
    
    if (auto* propagate = dynamic_cast<PropagateExpr*>(expr)) {
        collectCapturedVariables(propagate->operand.get(), params, captured);
        return;
    }
    
    if (auto* lambda = dynamic_cast<LambdaExpr*>(expr)) {
        std::set<std::string> nestedParams = params;
        for (const auto& param : lambda->params) {
            nestedParams.insert(param.first);
        }
        collectCapturedVariables(lambda->body.get(), nestedParams, captured);
        return;
    }
    
    if (auto* range = dynamic_cast<RangeExpr*>(expr)) {
        collectCapturedVariables(range->start.get(), params, captured);
        collectCapturedVariables(range->end.get(), params, captured);
        if (range->step) collectCapturedVariables(range->step.get(), params, captured);
        return;
    }
    
    if (auto* listComp = dynamic_cast<ListCompExpr*>(expr)) {
        std::set<std::string> compParams = params;
        compParams.insert(listComp->var);
        collectCapturedVariables(listComp->expr.get(), compParams, captured);
        collectCapturedVariables(listComp->iterable.get(), params, captured);
        if (listComp->condition) collectCapturedVariables(listComp->condition.get(), compParams, captured);
        return;
    }
}

void NativeCodeGen::collectCapturedVariablesStmt(Statement* stmt, const std::set<std::string>& params, std::set<std::string>& captured) {
    if (!stmt) return;
    
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        std::set<std::string> blockParams = params;
        for (auto& s : block->statements) {
            if (auto* varDecl = dynamic_cast<VarDecl*>(s.get())) {
                collectCapturedVariables(varDecl->initializer.get(), blockParams, captured);
                blockParams.insert(varDecl->name);
            } else {
                collectCapturedVariablesStmt(s.get(), blockParams, captured);
            }
        }
        return;
    }
    
    if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        collectCapturedVariables(exprStmt->expr.get(), params, captured);
        return;
    }
    
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        collectCapturedVariables(varDecl->initializer.get(), params, captured);
        return;
    }
    
    if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        collectCapturedVariables(assignStmt->target.get(), params, captured);
        collectCapturedVariables(assignStmt->value.get(), params, captured);
        return;
    }
    
    if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        collectCapturedVariables(ifStmt->condition.get(), params, captured);
        collectCapturedVariablesStmt(ifStmt->thenBranch.get(), params, captured);
        for (auto& elif : ifStmt->elifBranches) {
            collectCapturedVariables(elif.first.get(), params, captured);
            collectCapturedVariablesStmt(elif.second.get(), params, captured);
        }
        collectCapturedVariablesStmt(ifStmt->elseBranch.get(), params, captured);
        return;
    }
    
    if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        collectCapturedVariables(whileStmt->condition.get(), params, captured);
        collectCapturedVariablesStmt(whileStmt->body.get(), params, captured);
        return;
    }
    
    if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
        collectCapturedVariables(forStmt->iterable.get(), params, captured);
        std::set<std::string> forParams = params;
        forParams.insert(forStmt->var);
        collectCapturedVariablesStmt(forStmt->body.get(), forParams, captured);
        return;
    }
    
    if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        collectCapturedVariables(returnStmt->value.get(), params, captured);
        return;
    }
}

} // namespace tyl
