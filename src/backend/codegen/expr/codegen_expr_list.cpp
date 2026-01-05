// Tyl Compiler - Native Code Generator List Expressions
// Handles: ListExpr, ListCompExpr, RangeExpr

#include "backend/codegen/codegen_base.h"

namespace tyl {

void NativeCodeGen::visit(RangeExpr& node) {
    // RangeExpr is typically used in for loops and list comprehensions
    // When evaluated directly, we create a list from the range
    int64_t startVal, endVal;
    bool startConst = tryEvalConstant(node.start.get(), startVal);
    bool endConst = tryEvalConstant(node.end.get(), endVal);
    
    if (startConst && endConst) {
        // Constant range - create a list
        int64_t size = endVal - startVal + 1;
        if (size <= 0) {
            emitGCAllocList(4);
            return;
        }
        
        emitGCAllocList(static_cast<size_t>(size));
        
        allocLocal("$range_ptr");
        asm_.mov_mem_rbp_rax(locals["$range_ptr"]);
        
        // Set count
        asm_.mov_rcx_imm64(size);
        asm_.mov_rax_mem_rbp(locals["$range_ptr"]);
        asm_.mov_mem_rax_rcx();
        
        // Fill elements
        for (int64_t i = 0; i < size; i++) {
            asm_.mov_rax_mem_rbp(locals["$range_ptr"]);
            asm_.add_rax_imm32(16 + static_cast<int32_t>(i * 8));
            asm_.mov_rcx_imm64(startVal + i);
            asm_.mov_mem_rax_rcx();
        }
        
        asm_.mov_rax_mem_rbp(locals["$range_ptr"]);
    } else {
        // Dynamic range - evaluate at runtime
        node.start->accept(*this);
        asm_.push_rax();
        node.end->accept(*this);
        // For now, just return the end value (ranges are typically used in for loops)
        asm_.pop_rcx();
    }
    
    lastExprWasFloat_ = false;
}

void NativeCodeGen::visit(ListExpr& node) {
    if (node.elements.empty()) {
        emitGCAllocList(4);
        return;
    }
    
    std::vector<int64_t> values;
    bool allConstant = true;
    for (auto& elem : node.elements) {
        int64_t val;
        if (tryEvalConstant(elem.get(), val)) {
            values.push_back(val);
        } else {
            allConstant = false;
            break;
        }
    }
    
    if (allConstant) {
        // Create static data with list header structure:
        // [0]: length (8 bytes)
        // [8]: capacity (8 bytes)
        // [16+]: elements (8 bytes each)
        std::vector<uint8_t> data;
        
        // Length
        int64_t len = static_cast<int64_t>(values.size());
        for (int i = 0; i < 8; i++) {
            data.push_back((len >> (i * 8)) & 0xFF);
        }
        // Capacity (same as length for constant lists)
        for (int i = 0; i < 8; i++) {
            data.push_back((len >> (i * 8)) & 0xFF);
        }
        // Elements
        for (int64_t val : values) {
            for (int i = 0; i < 8; i++) {
                data.push_back((val >> (i * 8)) & 0xFF);
            }
        }
        uint32_t rva = pe_.addData(data.data(), data.size());
        asm_.lea_rax_rip_fixup(rva);
    } else {
        size_t capacity = node.elements.size();
        if (capacity < 4) capacity = 4;
        
        emitGCAllocList(capacity);
        
        std::string listPtrName = "$list_ptr_" + std::to_string(labelCounter++);
        allocLocal(listPtrName);
        asm_.mov_mem_rbp_rax(locals[listPtrName]);
        
        asm_.mov_rcx_imm64(static_cast<int64_t>(node.elements.size()));
        asm_.mov_rax_mem_rbp(locals[listPtrName]);
        asm_.code.push_back(0x48);
        asm_.code.push_back(0x89);
        asm_.code.push_back(0x08);
        
        for (size_t i = 0; i < node.elements.size(); i++) {
            node.elements[i]->accept(*this);
            
            asm_.mov_rcx_mem_rbp(locals[listPtrName]);
            
            int32_t offset = 16 + static_cast<int32_t>(i * 8);
            asm_.add_rcx_imm32(offset);
            asm_.mov_mem_rcx_rax();
        }
        
        asm_.mov_rax_mem_rbp(locals[listPtrName]);
    }
}

void NativeCodeGen::visit(ListCompExpr& node) {
    int64_t listSize = 0;
    bool sizeKnown = false;
    
    if (auto* range = dynamic_cast<RangeExpr*>(node.iterable.get())) {
        int64_t startVal, endVal;
        if (tryEvalConstant(range->start.get(), startVal) && 
            tryEvalConstant(range->end.get(), endVal)) {
            listSize = endVal - startVal + 1;
            if (listSize < 0) listSize = 0;
            sizeKnown = true;
        }
    } else if (auto* call = dynamic_cast<CallExpr*>(node.iterable.get())) {
        if (auto* calleeId = dynamic_cast<Identifier*>(call->callee.get())) {
            if (calleeId->name == "range") {
                if (call->args.size() == 1) {
                    int64_t endVal;
                    if (tryEvalConstant(call->args[0].get(), endVal)) {
                        listSize = endVal;
                        sizeKnown = true;
                    }
                } else if (call->args.size() >= 2) {
                    int64_t startVal, endVal;
                    if (tryEvalConstant(call->args[0].get(), startVal) &&
                        tryEvalConstant(call->args[1].get(), endVal)) {
                        listSize = endVal - startVal;
                        if (listSize < 0) listSize = 0;
                        sizeKnown = true;
                    }
                }
            }
        }
    }
    
    if (!sizeKnown || listSize <= 0) {
        asm_.xor_rax_rax();
        return;
    }
    
    emitGCAllocList(static_cast<size_t>(listSize));
    
    allocLocal("$listcomp_ptr");
    asm_.mov_mem_rbp_rax(locals["$listcomp_ptr"]);
    
    allocLocal("$listcomp_idx");
    asm_.xor_rax_rax();
    asm_.mov_mem_rbp_rax(locals["$listcomp_idx"]);
    
    allocLocal(node.var);
    
    if (auto* range = dynamic_cast<RangeExpr*>(node.iterable.get())) {
        range->start->accept(*this);
    } else if (auto* call = dynamic_cast<CallExpr*>(node.iterable.get())) {
        if (call->args.size() == 1) {
            asm_.xor_rax_rax();
        } else {
            call->args[0]->accept(*this);
        }
    }
    asm_.mov_mem_rbp_rax(locals[node.var]);
    
    allocLocal("$listcomp_end");
    if (auto* range = dynamic_cast<RangeExpr*>(node.iterable.get())) {
        range->end->accept(*this);
    } else if (auto* call = dynamic_cast<CallExpr*>(node.iterable.get())) {
        if (call->args.size() == 1) {
            call->args[0]->accept(*this);
        } else {
            call->args[1]->accept(*this);
        }
    }
    asm_.mov_mem_rbp_rax(locals["$listcomp_end"]);
    
    std::string loopLabel = newLabel("listcomp_loop");
    std::string endLabel = newLabel("listcomp_end");
    
    asm_.label(loopLabel);
    
    asm_.mov_rax_mem_rbp(locals[node.var]);
    asm_.cmp_rax_mem_rbp(locals["$listcomp_end"]);
    
    if (dynamic_cast<RangeExpr*>(node.iterable.get())) {
        asm_.jg_rel32(endLabel);
    } else {
        asm_.jge_rel32(endLabel);
    }
    
    if (node.condition) {
        std::string skipLabel = newLabel("listcomp_skip");
        node.condition->accept(*this);
        asm_.test_rax_rax();
        asm_.jz_rel32(skipLabel);
        
        node.expr->accept(*this);
        
        asm_.mov_rcx_mem_rbp(locals["$listcomp_ptr"]);
        asm_.add_rcx_imm32(16);
        asm_.mov_rdx_mem_rbp(locals["$listcomp_idx"]);
        asm_.code.push_back(0x48); asm_.code.push_back(0xC1);
        asm_.code.push_back(0xE2); asm_.code.push_back(0x03);
        asm_.code.push_back(0x48); asm_.code.push_back(0x01); asm_.code.push_back(0xD1);
        asm_.mov_mem_rcx_rax();
        
        asm_.mov_rax_mem_rbp(locals["$listcomp_idx"]);
        asm_.inc_rax();
        asm_.mov_mem_rbp_rax(locals["$listcomp_idx"]);
        
        asm_.label(skipLabel);
    } else {
        node.expr->accept(*this);
        
        asm_.push_rax();
        asm_.mov_rcx_mem_rbp(locals["$listcomp_ptr"]);
        asm_.add_rcx_imm32(16);
        asm_.mov_rdx_mem_rbp(locals["$listcomp_idx"]);
        asm_.code.push_back(0x48); asm_.code.push_back(0xC1);
        asm_.code.push_back(0xE2); asm_.code.push_back(0x03);
        asm_.code.push_back(0x48); asm_.code.push_back(0x01); asm_.code.push_back(0xD1);
        asm_.pop_rax();
        asm_.mov_mem_rcx_rax();
        
        asm_.mov_rax_mem_rbp(locals["$listcomp_idx"]);
        asm_.inc_rax();
        asm_.mov_mem_rbp_rax(locals["$listcomp_idx"]);
    }
    
    asm_.mov_rax_mem_rbp(locals[node.var]);
    asm_.inc_rax();
    asm_.mov_mem_rbp_rax(locals[node.var]);
    
    asm_.jmp_rel32(loopLabel);
    
    asm_.label(endLabel);
    
    asm_.mov_rax_mem_rbp(locals["$listcomp_ptr"]);
    
    listSizes["$listcomp_result"] = (size_t)listSize;
    lastExprWasFloat_ = false;
}

// Syntax Redesign - New Expression Visitors
void NativeCodeGen::visit(PlaceholderExpr& node) {
    (void)node;
    // Placeholder _ in expression context
    // When used outside of match arms, _ should have been transformed into a lambda
    // If we get here, it means _ is being used as a value directly
    // 
    // In contexts like:
    //   let f = _ * 2  -- should be transformed to |x| x * 2
    //   list.map(_ + 1) -- should be transformed to list.map(|x| x + 1)
    //
    // For now, if we reach here, the placeholder wasn't transformed
    // We'll treat it as accessing the implicit "it" variable if one exists
    // Otherwise, return 0 as a fallback
    
    // Check if there's an implicit "it" variable in scope
    auto itIt = locals.find("it");
    if (itIt != locals.end()) {
        asm_.mov_rax_mem_rbp(itIt->second);
        return;
    }
    
    // Check for "_" variable (from match arms)
    auto underscoreIt = locals.find("_");
    if (underscoreIt != locals.end()) {
        asm_.mov_rax_mem_rbp(underscoreIt->second);
        return;
    }
    
    // Fallback: return 0
    asm_.xor_rax_rax();
}

void NativeCodeGen::visit(InclusiveRangeExpr& node) {
    // Inclusive range is like RangeExpr but includes the end value
    // For now, create a list from start to end (inclusive)
    int64_t startVal, endVal;
    bool startConst = tryEvalConstant(node.start.get(), startVal);
    bool endConst = tryEvalConstant(node.end.get(), endVal);
    
    if (startConst && endConst) {
        // Constant range - create list at compile time
        int64_t size = endVal - startVal + 1;  // +1 for inclusive
        if (size <= 0) {
            emitGCAllocList(4);
            return;
        }
        
        emitGCAllocList(static_cast<size_t>(size));
        
        // Store list pointer
        allocLocal("$incrange_ptr");
        asm_.mov_mem_rbp_rax(locals["$incrange_ptr"]);
        
        // Set length
        asm_.mov_rcx_rax();
        asm_.mov_rax_imm64(size);
        asm_.mov_mem_rcx_rax();  // list[0] = length
        
        // Fill elements
        for (int64_t i = 0; i <= endVal - startVal; i++) {
            asm_.mov_rcx_mem_rbp(locals["$incrange_ptr"]);
            int32_t offset = 16 + static_cast<int32_t>(i * 8);
            asm_.add_rcx_imm32(offset);
            asm_.mov_rax_imm64(startVal + i);
            asm_.mov_mem_rcx_rax();
        }
        
        asm_.mov_rax_mem_rbp(locals["$incrange_ptr"]);
    } else {
        // Dynamic range - evaluate at runtime
        node.start->accept(*this);
        asm_.push_rax();
        node.end->accept(*this);
        // For now, just return the end value
        // TODO: Implement dynamic inclusive range
    }
}

void NativeCodeGen::visit(SafeNavExpr& node) {
    // Safe navigation: obj?.member
    // If obj is nil (0), result is nil; otherwise access member
    std::string nilLabel = newLabel("safenav_nil");
    std::string endLabel = newLabel("safenav_end");
    
    // Evaluate object
    node.object->accept(*this);
    
    // Check if nil
    asm_.test_rax_rax();
    asm_.jz_rel32(nilLabel);
    
    // Not nil - access member
    // Get the record type from the object
    // For now, assume it's a record and access the field
    // TODO: Look up field offset from record type info
    
    // Simple implementation: treat as first field access
    // This needs proper record type tracking
    asm_.mov_rax_mem_rax();  // Dereference to get first field
    asm_.jmp_rel32(endLabel);
    
    // Nil case - return nil (0)
    asm_.label(nilLabel);
    asm_.xor_rax_rax();
    
    asm_.label(endLabel);
}

void NativeCodeGen::visit(TypeCheckExpr& node) {
    // Type check: value is Type
    // Implements runtime type checking using type IDs stored in record headers
    // Also uses compile-time type information when available
    
    // First, check if we can resolve this at compile time
    if (auto* id = dynamic_cast<Identifier*>(node.value.get())) {
        auto typeIt = varTypes_.find(id->name);
        if (typeIt != varTypes_.end()) {
            // We know the type at compile time
            std::string actualType = typeIt->second;
            std::string targetType = node.typeName;
            
            // Normalize type names
            if (actualType == "i64" || actualType == "i32" || actualType == "i16" || actualType == "i8") actualType = "int";
            if (actualType == "f64" || actualType == "f32") actualType = "float";
            if (actualType == "string") actualType = "str";
            if (targetType == "i64" || targetType == "i32" || targetType == "i16" || targetType == "i8") targetType = "int";
            if (targetType == "f64" || targetType == "f32") targetType = "float";
            if (targetType == "string") targetType = "str";
            
            // Check for exact match or compatible types
            bool matches = (actualType == targetType);
            
            // Check for record type inheritance/compatibility
            if (!matches && recordTypes_.count(actualType) && recordTypes_.count(targetType)) {
                // For now, only exact record type matches
                matches = (actualType == targetType);
            }
            
            // Emit constant result
            asm_.mov_rax_imm64(matches ? 1 : 0);
            return;
        }
    }
    
    // Fall back to runtime check - evaluate the value first
    node.value->accept(*this);
    
    // Check if we're checking against a known record type
    auto it = recordTypes_.find(node.typeName);
    if (it != recordTypes_.end()) {
        // We have a record type - check the type ID in the record header
        // Record layout: [fieldCount:8][typeId:8][fields...]
        // The type ID is at offset 8 from the record pointer
        
        // Save the pointer
        asm_.push_rax();
        
        // Check if pointer is null first
        asm_.test_rax_rax();
        std::string nullLabel = newLabel("typecheck_null");
        std::string endLabel = newLabel("typecheck_end");
        asm_.jz_rel32(nullLabel);
        
        // Load type ID from record: [rax+8]
        asm_.code.push_back(0x48); asm_.code.push_back(0x8B);
        asm_.code.push_back(0x40); asm_.code.push_back(0x08);  // mov rax, [rax+8]
        
        // Get the expected type ID for this type name
        uint64_t expectedTypeId = 0;
        auto typeIdIt = typeIds_.find(node.typeName);
        if (typeIdIt != typeIds_.end()) {
            expectedTypeId = typeIdIt->second;
        } else {
            // Assign a new type ID
            expectedTypeId = nextTypeId_++;
            typeIds_[node.typeName] = expectedTypeId;
        }
        
        // Compare with expected type ID
        asm_.mov_rcx_imm64(static_cast<int64_t>(expectedTypeId));
        asm_.cmp_rax_rcx();
        
        // Set result: 1 if equal, 0 if not
        asm_.pop_rcx();  // Discard saved pointer
        asm_.xor_rax_rax();
        // sete al
        asm_.code.push_back(0x0F); asm_.code.push_back(0x94); asm_.code.push_back(0xC0);
        asm_.jmp_rel32(endLabel);
        
        asm_.label(nullLabel);
        asm_.pop_rcx();  // Discard saved pointer
        asm_.xor_rax_rax();  // null is not any type
        
        asm_.label(endLabel);
    }
    // Check for primitive types using tracked type info
    else if (node.typeName == "int" || node.typeName == "i64") {
        // Check if the variable is tracked as an int
        if (auto* id = dynamic_cast<Identifier*>(node.value.get())) {
            auto typeIt = varTypes_.find(id->name);
            if (typeIt != varTypes_.end()) {
                bool isInt = (typeIt->second == "int" || typeIt->second == "i64" || 
                              typeIt->second == "i32" || typeIt->second == "i16" || typeIt->second == "i8");
                asm_.mov_rax_imm64(isInt ? 1 : 0);
            } else if (!floatVars.count(id->name) && !constStrVars.count(id->name)) {
                asm_.mov_rax_imm64(1);  // Assume int if not float or string
            } else {
                asm_.mov_rax_imm64(0);
            }
        } else {
            // For expressions, check if it's not a float
            asm_.mov_rax_imm64(lastExprWasFloat_ ? 0 : 1);
        }
    }
    else if (isFloatTypeName(node.typeName)) {
        // Check if the variable is tracked as a float
        if (auto* id = dynamic_cast<Identifier*>(node.value.get())) {
            if (floatVars.count(id->name)) {
                asm_.mov_rax_imm64(1);  // It's a float
            } else {
                asm_.mov_rax_imm64(0);  // Not a float
            }
        } else {
            asm_.mov_rax_imm64(lastExprWasFloat_ ? 1 : 0);
        }
    }
    else if (node.typeName == "str" || node.typeName == "string") {
        // Check if the variable is tracked as a string
        if (auto* id = dynamic_cast<Identifier*>(node.value.get())) {
            if (constStrVars.count(id->name)) {
                asm_.mov_rax_imm64(1);  // It's a string
            } else {
                auto typeIt = varTypes_.find(id->name);
                if (typeIt != varTypes_.end() && (typeIt->second == "str" || typeIt->second == "string")) {
                    asm_.mov_rax_imm64(1);
                } else {
                    asm_.mov_rax_imm64(0);
                }
            }
        } else {
            // String check - assume non-null pointer is a string (fallback)
            asm_.test_rax_rax();
            asm_.code.push_back(0x0F); asm_.code.push_back(0x95); asm_.code.push_back(0xC0);  // setne al
            asm_.code.push_back(0x48); asm_.code.push_back(0x0F); asm_.code.push_back(0xB6); asm_.code.push_back(0xC0);  // movzx rax, al
        }
    }
    else if (node.typeName == "bool") {
        // Check if the variable is tracked as a bool
        if (auto* id = dynamic_cast<Identifier*>(node.value.get())) {
            auto typeIt = varTypes_.find(id->name);
            if (typeIt != varTypes_.end() && typeIt->second == "bool") {
                asm_.mov_rax_imm64(1);
            } else {
                asm_.mov_rax_imm64(0);
            }
        } else {
            // Bool is 0 or 1 - check if value is 0 or 1
            asm_.cmp_rax_imm32(1);
            // setbe al (set if below or equal, i.e., 0 or 1)
            asm_.code.push_back(0x0F); asm_.code.push_back(0x96); asm_.code.push_back(0xC0);
            asm_.code.push_back(0x48); asm_.code.push_back(0x0F); asm_.code.push_back(0xB6); asm_.code.push_back(0xC0);  // movzx rax, al
        }
    }
    else if (node.typeName == "nil") {
        // Check if value is null/nil (0)
        asm_.test_rax_rax();
        asm_.code.push_back(0x0F); asm_.code.push_back(0x94); asm_.code.push_back(0xC0);  // sete al
        asm_.code.push_back(0x48); asm_.code.push_back(0x0F); asm_.code.push_back(0xB6); asm_.code.push_back(0xC0);  // movzx rax, al
    }
    else if (node.typeName == "list") {
        // Check if the variable is tracked as a list
        if (auto* id = dynamic_cast<Identifier*>(node.value.get())) {
            if (listVars.count(id->name)) {
                asm_.mov_rax_imm64(1);
            } else {
                asm_.mov_rax_imm64(0);
            }
        } else {
            asm_.mov_rax_imm64(0);  // Can't determine at runtime without more info
        }
    }
    else {
        // Unknown type - check if variable has this exact type
        if (auto* id = dynamic_cast<Identifier*>(node.value.get())) {
            auto typeIt = varTypes_.find(id->name);
            if (typeIt != varTypes_.end() && typeIt->second == node.typeName) {
                asm_.mov_rax_imm64(1);
            } else {
                asm_.mov_rax_imm64(0);
            }
        } else {
            asm_.xor_rax_rax();  // Unknown type - return false
        }
    }
}

} // namespace tyl
