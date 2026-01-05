// Tyl Compiler - Extended List Builtins for Native Code Generation
// Additional list functions ported from stdlib/list/list.cpp

#include "backend/codegen/codegen_base.h"

namespace tyl {

// first(list) -> value - Get first element
void NativeCodeGen::emitListFirst(CallExpr& node) {
    node.args[0]->accept(*this);
    // List pointer in rax, first element at offset 0 (after size)
    asm_.mov_rax_mem_rax(); // Load first element
}

// last(list) -> value - Get last element
void NativeCodeGen::emitListLast(CallExpr& node) {
    std::string listName;
    size_t listSize = 0;
    bool knownSize = false;
    
    if (auto* ident = dynamic_cast<Identifier*>(node.args[0].get())) {
        listName = ident->name;
        auto sizeIt = listSizes.find(listName);
        if (sizeIt != listSizes.end()) {
            listSize = sizeIt->second;
            knownSize = true;
        }
    }
    
    node.args[0]->accept(*this);
    
    if (knownSize && listSize > 0) {
        asm_.add_rax_imm32((int32_t)((listSize - 1) * 8));
        asm_.mov_rax_mem_rax();
    } else {
        // Runtime: load size, compute offset
        asm_.mov_rcx_mem_rax(); // size
        asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC9); // dec rcx
        asm_.code.push_back(0x48); asm_.code.push_back(0xC1); asm_.code.push_back(0xE1); asm_.code.push_back(0x03); // shl rcx, 3
        asm_.code.push_back(0x48); asm_.code.push_back(0x01); asm_.code.push_back(0xC8); // add rax, rcx
        asm_.mov_rax_mem_rax();
    }
}

// get(list, index) -> value - Get element at index
void NativeCodeGen::emitListGet(CallExpr& node) {
    int64_t idx;
    if (tryEvalConstant(node.args[1].get(), idx)) {
        node.args[0]->accept(*this);
        if (idx >= 0) {
            asm_.add_rax_imm32((int32_t)(idx * 8));
        }
        asm_.mov_rax_mem_rax();
        return;
    }
    
    node.args[0]->accept(*this);
    asm_.push_rax();
    node.args[1]->accept(*this);
    asm_.code.push_back(0x48); asm_.code.push_back(0xC1); asm_.code.push_back(0xE0); asm_.code.push_back(0x03); // shl rax, 3
    asm_.pop_rcx();
    asm_.code.push_back(0x48); asm_.code.push_back(0x01); asm_.code.push_back(0xC8); // add rax, rcx
    asm_.mov_rax_mem_rax();
}

// reverse(list) -> list - Reverse list
void NativeCodeGen::emitListReverse(CallExpr& node) {
    std::string listName;
    size_t listSize = 0;
    bool knownSize = false;
    
    if (auto* ident = dynamic_cast<Identifier*>(node.args[0].get())) {
        listName = ident->name;
        auto sizeIt = listSizes.find(listName);
        if (sizeIt != listSizes.end()) {
            listSize = sizeIt->second;
            knownSize = true;
        }
    }
    
    if (knownSize && listSize > 0) {
        // Allocate new list
        emitGCAllocList(listSize);
        allocLocal("$rev_list");
        asm_.mov_mem_rbp_rax(locals["$rev_list"]);
        
        node.args[0]->accept(*this);
        asm_.mov_rcx_rax();
        
        // Copy in reverse order
        for (size_t i = 0; i < listSize; i++) {
            // Load from source[size-1-i]
            asm_.mov_rax_rcx();
            asm_.add_rax_imm32((int32_t)((listSize - 1 - i) * 8));
            asm_.mov_rax_mem_rax();
            
            // Store to dest[i]
            asm_.mov_rdx_mem_rbp(locals["$rev_list"]);
            // add rdx, imm32
            asm_.code.push_back(0x48); asm_.code.push_back(0x81); asm_.code.push_back(0xC2);
            int32_t off1 = (int32_t)(i * 8);
            asm_.code.push_back(off1 & 0xFF); asm_.code.push_back((off1 >> 8) & 0xFF);
            asm_.code.push_back((off1 >> 16) & 0xFF); asm_.code.push_back((off1 >> 24) & 0xFF);
            // mov [rdx], rax
            asm_.code.push_back(0x48); asm_.code.push_back(0x89); asm_.code.push_back(0x02);
        }
        
        asm_.mov_rax_mem_rbp(locals["$rev_list"]);
    } else {
        // Runtime - return original for now
        node.args[0]->accept(*this);
    }
}

// index(list, value) -> int - Find index of value (-1 if not found)
void NativeCodeGen::emitListIndex(CallExpr& node) {
    // Simplified - return -1 for now
    (void)node;
    asm_.mov_rax_imm64(-1);
}

// includes(list, value) -> bool - Check if list contains value
void NativeCodeGen::emitListIncludes(CallExpr& node) {
    // Simplified - return 0 for now
    (void)node;
    asm_.xor_rax_rax();
}

// take(list, n) -> list - Take first n elements
void NativeCodeGen::emitListTake(CallExpr& node) {
    int64_t n;
    std::string listName;
    size_t listSize = 0;
    bool knownSize = false;
    
    if (auto* ident = dynamic_cast<Identifier*>(node.args[0].get())) {
        listName = ident->name;
        auto sizeIt = listSizes.find(listName);
        if (sizeIt != listSizes.end()) {
            listSize = sizeIt->second;
            knownSize = true;
        }
    }
    
    if (knownSize && tryEvalConstant(node.args[1].get(), n)) {
        size_t takeCount = std::min((size_t)n, listSize);
        if (takeCount == 0) {
            emitGCAllocList(0);
            return;
        }
        
        emitGCAllocList(takeCount);
        allocLocal("$take_list");
        asm_.mov_mem_rbp_rax(locals["$take_list"]);
        
        node.args[0]->accept(*this);
        asm_.mov_rcx_rax();
        
        for (size_t i = 0; i < takeCount; i++) {
            asm_.mov_rax_rcx();
            asm_.add_rax_imm32((int32_t)(i * 8));
            asm_.mov_rax_mem_rax();
            
            asm_.mov_rdx_mem_rbp(locals["$take_list"]);
            // add rdx, imm32
            asm_.code.push_back(0x48); asm_.code.push_back(0x81); asm_.code.push_back(0xC2);
            int32_t off2 = (int32_t)(i * 8);
            asm_.code.push_back(off2 & 0xFF); asm_.code.push_back((off2 >> 8) & 0xFF);
            asm_.code.push_back((off2 >> 16) & 0xFF); asm_.code.push_back((off2 >> 24) & 0xFF);
            // mov [rdx], rax
            asm_.code.push_back(0x48); asm_.code.push_back(0x89); asm_.code.push_back(0x02);
        }
        
        asm_.mov_rax_mem_rbp(locals["$take_list"]);
        if (!listName.empty()) {
            listSizes[listName + "_take"] = takeCount;
        }
    } else {
        // Runtime - return original for now
        node.args[0]->accept(*this);
    }
}

// drop(list, n) -> list - Drop first n elements
void NativeCodeGen::emitListDrop(CallExpr& node) {
    int64_t n;
    std::string listName;
    size_t listSize = 0;
    bool knownSize = false;
    
    if (auto* ident = dynamic_cast<Identifier*>(node.args[0].get())) {
        listName = ident->name;
        auto sizeIt = listSizes.find(listName);
        if (sizeIt != listSizes.end()) {
            listSize = sizeIt->second;
            knownSize = true;
        }
    }
    
    if (knownSize && tryEvalConstant(node.args[1].get(), n)) {
        if (n >= (int64_t)listSize) {
            emitGCAllocList(0);
            return;
        }
        
        size_t dropCount = (size_t)n;
        size_t newSize = listSize - dropCount;
        
        emitGCAllocList(newSize);
        allocLocal("$drop_list");
        asm_.mov_mem_rbp_rax(locals["$drop_list"]);
        
        node.args[0]->accept(*this);
        asm_.mov_rcx_rax();
        
        for (size_t i = 0; i < newSize; i++) {
            asm_.mov_rax_rcx();
            asm_.add_rax_imm32((int32_t)((dropCount + i) * 8));
            asm_.mov_rax_mem_rax();
            
            asm_.mov_rdx_mem_rbp(locals["$drop_list"]);
            // add rdx, imm32
            asm_.code.push_back(0x48); asm_.code.push_back(0x81); asm_.code.push_back(0xC2);
            int32_t off3 = (int32_t)(i * 8);
            asm_.code.push_back(off3 & 0xFF); asm_.code.push_back((off3 >> 8) & 0xFF);
            asm_.code.push_back((off3 >> 16) & 0xFF); asm_.code.push_back((off3 >> 24) & 0xFF);
            // mov [rdx], rax
            asm_.code.push_back(0x48); asm_.code.push_back(0x89); asm_.code.push_back(0x02);
        }
        
        asm_.mov_rax_mem_rbp(locals["$drop_list"]);
    } else {
        node.args[0]->accept(*this);
    }
}

// min_of(list) -> value - Find minimum value
void NativeCodeGen::emitListMinOf(CallExpr& node) {
    std::string listName;
    size_t listSize = 0;
    bool knownSize = false;
    
    if (auto* ident = dynamic_cast<Identifier*>(node.args[0].get())) {
        listName = ident->name;
        auto sizeIt = listSizes.find(listName);
        if (sizeIt != listSizes.end()) {
            listSize = sizeIt->second;
            knownSize = true;
        }
    }
    
    if (knownSize && listSize > 0) {
        node.args[0]->accept(*this);
        asm_.mov_rcx_rax();
        
        // Load first element as initial min
        asm_.mov_rax_mem_rcx();
        
        for (size_t i = 1; i < listSize; i++) {
            asm_.push_rax();
            asm_.mov_rax_rcx();
            asm_.add_rax_imm32((int32_t)(i * 8));
            asm_.mov_rax_mem_rax();
            asm_.pop_rdx();
            
            // if rax < rdx, keep rax, else use rdx
            // cmp rax, rdx
            asm_.code.push_back(0x48); asm_.code.push_back(0x39); asm_.code.push_back(0xD0);
            // cmovg rax, rdx
            asm_.code.push_back(0x48); asm_.code.push_back(0x0F); asm_.code.push_back(0x4F); asm_.code.push_back(0xC2);
        }
    } else {
        node.args[0]->accept(*this);
        asm_.mov_rax_mem_rax();
    }
}

// max_of(list) -> value - Find maximum value
void NativeCodeGen::emitListMaxOf(CallExpr& node) {
    std::string listName;
    size_t listSize = 0;
    bool knownSize = false;
    
    if (auto* ident = dynamic_cast<Identifier*>(node.args[0].get())) {
        listName = ident->name;
        auto sizeIt = listSizes.find(listName);
        if (sizeIt != listSizes.end()) {
            listSize = sizeIt->second;
            knownSize = true;
        }
    }
    
    if (knownSize && listSize > 0) {
        node.args[0]->accept(*this);
        asm_.mov_rcx_rax();
        
        asm_.mov_rax_mem_rcx();
        
        for (size_t i = 1; i < listSize; i++) {
            asm_.push_rax();
            asm_.mov_rax_rcx();
            asm_.add_rax_imm32((int32_t)(i * 8));
            asm_.mov_rax_mem_rax();
            asm_.pop_rdx();
            
            // cmp rax, rdx
            asm_.code.push_back(0x48); asm_.code.push_back(0x39); asm_.code.push_back(0xD0);
            // cmovl rax, rdx
            asm_.code.push_back(0x48); asm_.code.push_back(0x0F); asm_.code.push_back(0x4C); asm_.code.push_back(0xC2);
        }
    } else {
        node.args[0]->accept(*this);
        asm_.mov_rax_mem_rax();
    }
}

} // namespace tyl
