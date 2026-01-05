// Tyl Compiler - Native Code Generator Index Expressions
// Handles: IndexExpr

#include "backend/codegen/codegen_base.h"

namespace tyl {

void NativeCodeGen::visit(IndexExpr& node) {
    // Handle map access with string key
    if (auto* strKey = dynamic_cast<StringLiteral*>(node.index.get())) {
        emitMapIndexAccess(node, strKey);
        return;
    }
    
    // Check for string slicing with range expression
    // String slicing: "hello"[1..4] or str[start..end]
    if (auto* rangeIdx = dynamic_cast<RangeExpr*>(node.index.get())) {
        emitStringSlice(node, rangeIdx->start.get(), rangeIdx->end.get(), false);
        return;
    }
    if (auto* incRangeIdx = dynamic_cast<InclusiveRangeExpr*>(node.index.get())) {
        emitStringSlice(node, incRangeIdx->start.get(), incRangeIdx->end.get(), true);
        return;
    }
    
    // Check for constant list access (1-based indexing)
    if (auto* ident = dynamic_cast<Identifier*>(node.object.get())) {
        auto constListIt = constListVars.find(ident->name);
        if (constListIt != constListVars.end()) {
            int64_t indexVal;
            if (tryEvalConstant(node.index.get(), indexVal)) {
                int64_t zeroBasedIndex = indexVal - 1;
                if (zeroBasedIndex >= 0 && (size_t)zeroBasedIndex < constListIt->second.size()) {
                    asm_.mov_rax_imm64(constListIt->second[zeroBasedIndex]);
                    lastExprWasFloat_ = false;
                    return;
                }
            }
            // Runtime index into constant list (now has 16-byte header)
            node.index->accept(*this);
            asm_.dec_rax();
            asm_.push_rax();
            
            node.object->accept(*this);
            asm_.add_rax_imm32(16);  // Skip header
            asm_.pop_rcx();
            
            asm_.code.push_back(0x48); asm_.code.push_back(0xC1);
            asm_.code.push_back(0xE1); asm_.code.push_back(0x03);
            
            asm_.add_rax_rcx();
            asm_.mov_rax_mem_rax();
            
            lastExprWasFloat_ = false;
            return;
        }
        
        // Check for fixed-size array access (0-based indexing)
        auto fixedArrayIt = varFixedArrayTypes_.find(ident->name);
        if (fixedArrayIt != varFixedArrayTypes_.end()) {
            emitFixedArrayIndexAccess(node, fixedArrayIt->second);
            return;
        }
    }
    
    // Check for nested fixed array access (e.g., mat[0][1] where mat is [[int; 2]; 2])
    // The object is an IndexExpr into a fixed array
    if (auto* innerIndex = dynamic_cast<IndexExpr*>(node.object.get())) {
        // Try to find the root array and determine the inner element type
        FixedArrayInfo innerInfo;
        if (getNestedFixedArrayInfo(innerIndex, innerInfo)) {
            emitFixedArrayIndexAccess(node, innerInfo);
            return;
        }
    }
    
    // Runtime list indexing (GC-allocated lists have a 16-byte header)
    node.index->accept(*this);
    asm_.dec_rax();
    asm_.push_rax();
    
    node.object->accept(*this);
    asm_.add_rax_imm32(16);
    
    asm_.pop_rcx();
    asm_.code.push_back(0x48); asm_.code.push_back(0xC1);
    asm_.code.push_back(0xE1); asm_.code.push_back(0x03);
    
    asm_.add_rax_rcx();
    asm_.mov_rax_mem_rax();
    
    lastExprWasFloat_ = false;
}

void NativeCodeGen::emitStringSlice(IndexExpr& node, Expression* startExpr, Expression* endExpr, bool inclusive) {
    // String slicing creates a str_view which is a 16-byte struct: {ptr: *u8, len: i64}
    // We allocate this on the GC heap for simplicity
    // 
    // For "hello"[1..4]:
    //   - start = 1 (1-based, so offset 0)
    //   - end = 4 (exclusive, so we take chars at indices 0, 1, 2 = "hel")
    // For "hello"[1..=4]:
    //   - start = 1 (1-based, so offset 0)
    //   - end = 4 (inclusive, so we take chars at indices 0, 1, 2, 3 = "hell")
    
    // Evaluate start index (1-based)
    startExpr->accept(*this);
    asm_.dec_rax();  // Convert to 0-based
    asm_.push_rax();  // Save start offset
    
    // Evaluate end index (1-based)
    endExpr->accept(*this);
    if (!inclusive) {
        asm_.dec_rax();  // For exclusive range, end is already the position after last char
    }
    // For inclusive range, end is the last char position (1-based), so it's already correct after -1
    asm_.push_rax();  // Save end offset (0-based, exclusive position)
    
    // Evaluate the string object
    node.object->accept(*this);
    asm_.push_rax();  // Save string pointer
    
    // Allocate str_view struct (16 bytes: ptr + len)
    // Use GC allocation for the str_view struct
    emitGCAllocRaw(16);
    
    allocLocal("$str_view_ptr");
    asm_.mov_mem_rbp_rax(locals["$str_view_ptr"]);
    
    // Pop string pointer into rcx
    asm_.pop_rcx();  // rcx = string pointer
    
    // Pop end offset into rdx
    asm_.pop_rdx();  // rdx = end offset (0-based)
    
    // Pop start offset into r8
    asm_.code.push_back(0x41); asm_.code.push_back(0x58);  // pop r8
    
    // Calculate length: end - start
    // For exclusive: len = end - start
    // For inclusive: len = end - start + 1 (but we already adjusted end above)
    asm_.code.push_back(0x48); asm_.code.push_back(0x89); asm_.code.push_back(0xD0);  // mov rax, rdx
    asm_.code.push_back(0x4C); asm_.code.push_back(0x29); asm_.code.push_back(0xC0);  // sub rax, r8
    if (inclusive) {
        asm_.inc_rax();  // For inclusive range, add 1 to length
    }
    asm_.push_rax();  // Save length
    
    // Calculate slice pointer: string_ptr + start_offset
    asm_.code.push_back(0x4C); asm_.code.push_back(0x01); asm_.code.push_back(0xC1);  // add rcx, r8
    
    // Store pointer in str_view[0]
    asm_.mov_rax_mem_rbp(locals["$str_view_ptr"]);
    asm_.mov_mem_rax_rcx();  // str_view->ptr = string_ptr + start
    
    // Store length in str_view[8]
    asm_.pop_rcx();  // rcx = length
    asm_.mov_rax_mem_rbp(locals["$str_view_ptr"]);
    asm_.add_rax_imm32(8);
    asm_.mov_mem_rax_rcx();  // str_view->len = length
    
    // Return pointer to str_view
    asm_.mov_rax_mem_rbp(locals["$str_view_ptr"]);
    
    lastExprWasFloat_ = false;
}

void NativeCodeGen::emitMapIndexAccess(IndexExpr& node, StringLiteral* strKey) {
    uint64_t hash = 5381;
    for (char c : strKey->value) {
        hash = ((hash << 5) + hash) + static_cast<uint8_t>(c);
    }
    
    uint32_t keyRva = addString(strKey->value);
    
    node.object->accept(*this);
    
    allocLocal("$map_get_ptr");
    asm_.mov_mem_rbp_rax(locals["$map_get_ptr"]);
    
    asm_.mov_rcx_mem_rax();
    
    asm_.mov_rax_imm64(static_cast<int64_t>(hash));
    asm_.code.push_back(0x48); asm_.code.push_back(0x31); asm_.code.push_back(0xD2);
    asm_.code.push_back(0x48); asm_.code.push_back(0xF7); asm_.code.push_back(0xF1);
    
    asm_.mov_rax_mem_rbp(locals["$map_get_ptr"]);
    asm_.add_rax_imm32(16);
    asm_.code.push_back(0x48); asm_.code.push_back(0xC1); 
    asm_.code.push_back(0xE2); asm_.code.push_back(0x03);
    asm_.code.push_back(0x48); asm_.code.push_back(0x01); asm_.code.push_back(0xD0);
    
    asm_.mov_rax_mem_rax();
    
    std::string searchLoop = newLabel("map_search");
    std::string foundLabel = newLabel("map_found");
    std::string notFoundLabel = newLabel("map_notfound");
    
    asm_.label(searchLoop);
    asm_.test_rax_rax();
    asm_.jz_rel32(notFoundLabel);
    
    asm_.push_rax();
    asm_.mov_rcx_mem_rax();
    asm_.mov_rdx_imm64(static_cast<int64_t>(hash));
    asm_.code.push_back(0x48); asm_.code.push_back(0x39); asm_.code.push_back(0xD1);
    asm_.pop_rax();
    asm_.jnz_rel32(searchLoop + "_next");
    
    asm_.push_rax();
    asm_.add_rax_imm32(8);
    asm_.mov_rcx_mem_rax();
    
    asm_.lea_rax_rip_fixup(keyRva);
    asm_.mov_rdx_rax();
    
    std::string cmpLoop = newLabel("strcmp");
    std::string cmpDone = newLabel("strcmp_done");
    std::string cmpNotEqual = newLabel("strcmp_ne");
    
    asm_.label(cmpLoop);
    asm_.code.push_back(0x0F); asm_.code.push_back(0xB6); asm_.code.push_back(0x01);
    asm_.code.push_back(0x44); asm_.code.push_back(0x0F); asm_.code.push_back(0xB6); 
    asm_.code.push_back(0x02);
    
    asm_.code.push_back(0x44); asm_.code.push_back(0x39); asm_.code.push_back(0xC0);
    asm_.jnz_rel32(cmpNotEqual);
    
    asm_.test_rax_rax();
    asm_.jz_rel32(cmpDone);
    
    asm_.inc_rcx();
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC2);
    asm_.jmp_rel32(cmpLoop);
    
    asm_.label(cmpNotEqual);
    asm_.pop_rax();
    asm_.jmp_rel32(searchLoop + "_next");
    
    asm_.label(cmpDone);
    asm_.pop_rax();
    asm_.jmp_rel32(foundLabel);
    
    asm_.label(searchLoop + "_next");
    asm_.add_rax_imm32(24);
    asm_.mov_rax_mem_rax();
    asm_.jmp_rel32(searchLoop);
    
    asm_.label(notFoundLabel);
    asm_.xor_rax_rax();
    std::string endLabel = newLabel("map_get_end");
    asm_.jmp_rel32(endLabel);
    
    asm_.label(foundLabel);
    asm_.add_rax_imm32(16);
    asm_.mov_rax_mem_rax();
    
    asm_.label(endLabel);
    lastExprWasFloat_ = false;
}

void NativeCodeGen::emitFixedArrayIndexAccess(IndexExpr& node, const FixedArrayInfo& info) {
    node.index->accept(*this);
    asm_.dec_rax();  // Convert 1-based index to 0-based
    asm_.push_rax();
    
    node.object->accept(*this);
    asm_.pop_rcx();
    
    // Check if element type is itself an array (multi-dimensional)
    bool isNestedArray = !info.elementType.empty() && info.elementType[0] == '[';
    
    // For nested arrays, elements are stored as pointers (8 bytes each)
    // For scalar elements, use the actual element size
    int32_t actualElementSize = isNestedArray ? 8 : info.elementSize;
    
    // Multiply index by element size
    if (actualElementSize == 8) {
        asm_.code.push_back(0x48); asm_.code.push_back(0xC1);
        asm_.code.push_back(0xE1); asm_.code.push_back(0x03);  // shl rcx, 3
    } else if (actualElementSize == 4) {
        asm_.code.push_back(0x48); asm_.code.push_back(0xC1);
        asm_.code.push_back(0xE1); asm_.code.push_back(0x02);  // shl rcx, 2
    } else if (actualElementSize == 2) {
        asm_.code.push_back(0x48); asm_.code.push_back(0xD1);
        asm_.code.push_back(0xE1);  // shl rcx, 1
    } else if (actualElementSize != 1) {
        asm_.mov_rdx_imm64(actualElementSize);
        asm_.code.push_back(0x48); asm_.code.push_back(0x0F);
        asm_.code.push_back(0xAF); asm_.code.push_back(0xCA);  // imul rcx, rdx
    }
    
    asm_.add_rax_rcx();  // rax = base + index * elementSize
    
    // Check if element type is itself an array (multi-dimensional)
    // If so, load the pointer to the sub-array
    if (isNestedArray) {
        // For nested arrays, load the pointer stored at this location
        // The outer array stores pointers to inner arrays
        asm_.mov_rax_mem_rax();  // Load the pointer to the inner array
        // The inner array has a 16-byte header (length + capacity), skip it
        asm_.add_rax_imm32(16);
        lastExprWasFloat_ = false;
    } else {
        // For scalar elements, load the value
        if (info.elementSize == 1) {
            asm_.code.push_back(0x48);
            asm_.code.push_back(0x0F);
            asm_.code.push_back(0xB6);
            asm_.code.push_back(0x00);  // movzx rax, byte [rax]
        } else if (info.elementSize == 2) {
            asm_.code.push_back(0x48);
            asm_.code.push_back(0x0F);
            asm_.code.push_back(0xB7);
            asm_.code.push_back(0x00);  // movzx rax, word [rax]
        } else if (info.elementSize == 4) {
            asm_.code.push_back(0x8B);
            asm_.code.push_back(0x00);  // mov eax, [rax]
        } else {
            asm_.mov_rax_mem_rax();  // mov rax, [rax]
        }
        
        lastExprWasFloat_ = isFloatTypeName(info.elementType);
    }
}

bool NativeCodeGen::getNestedFixedArrayInfo(IndexExpr* indexExpr, FixedArrayInfo& outInfo) {
    // Walk up the chain of IndexExprs to find the root fixed array variable
    // Then compute the element type at this nesting level
    
    // First, find the root identifier
    Expression* current = indexExpr->object.get();
    int nestingLevel = 1;  // We're already one level deep
    
    while (auto* innerIndex = dynamic_cast<IndexExpr*>(current)) {
        current = innerIndex->object.get();
        nestingLevel++;
    }
    
    // current should now be an Identifier
    auto* rootIdent = dynamic_cast<Identifier*>(current);
    if (!rootIdent) return false;
    
    // Look up the root array's type info
    auto it = varFixedArrayTypes_.find(rootIdent->name);
    if (it == varFixedArrayTypes_.end()) return false;
    
    // Now we need to "peel off" nestingLevel layers of array types
    std::string elemType = it->second.elementType;
    
    for (int i = 1; i < nestingLevel; i++) {
        // elemType should be something like "[int; 2]"
        if (elemType.empty() || elemType[0] != '[') return false;
        
        // Parse the inner element type
        std::string inner = elemType.substr(1, elemType.size() - 2);
        
        // Find the semicolon that separates element type from size
        int bracketDepth = 0;
        size_t semicolonPos = std::string::npos;
        for (size_t j = 0; j < inner.size(); j++) {
            if (inner[j] == '[') bracketDepth++;
            else if (inner[j] == ']') bracketDepth--;
            else if (inner[j] == ';' && bracketDepth == 0) {
                semicolonPos = j;
                break;
            }
        }
        
        if (semicolonPos == std::string::npos) return false;
        
        elemType = inner.substr(0, semicolonPos);
        // Trim whitespace
        while (!elemType.empty() && elemType.back() == ' ') elemType.pop_back();
    }
    
    // Now elemType is the element type at this nesting level
    // Parse it to get size and inner element type
    if (elemType.empty() || elemType[0] != '[') {
        // It's a scalar type
        outInfo.elementType = elemType;
        outInfo.elementSize = getTypeSize(elemType);
        outInfo.size = 1;  // Not really used for scalar access
    } else {
        // It's still an array type - parse it
        std::string inner = elemType.substr(1, elemType.size() - 2);
        
        int bracketDepth = 0;
        size_t semicolonPos = std::string::npos;
        for (size_t j = 0; j < inner.size(); j++) {
            if (inner[j] == '[') bracketDepth++;
            else if (inner[j] == ']') bracketDepth--;
            else if (inner[j] == ';' && bracketDepth == 0) {
                semicolonPos = j;
                break;
            }
        }
        
        if (semicolonPos != std::string::npos) {
            std::string innerElemType = inner.substr(0, semicolonPos);
            std::string sizeStr = inner.substr(semicolonPos + 1);
            
            // Trim whitespace
            while (!innerElemType.empty() && innerElemType.back() == ' ') innerElemType.pop_back();
            while (!sizeStr.empty() && sizeStr[0] == ' ') sizeStr = sizeStr.substr(1);
            while (!sizeStr.empty() && sizeStr.back() == ' ') sizeStr.pop_back();
            
            outInfo.elementType = innerElemType;
            outInfo.size = std::stoull(sizeStr);
            // Element size is the size of the inner element type, not the whole array
            outInfo.elementSize = getTypeSize(innerElemType);
        } else {
            return false;
        }
    }
    
    return true;
}

} // namespace tyl
