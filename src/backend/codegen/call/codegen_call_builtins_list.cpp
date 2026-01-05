// Tyl Compiler - Native Code Generator List Builtin Calls
// Handles: push, pop, range, contains (for lists)

#include "backend/codegen/codegen_base.h"

namespace tyl {

void NativeCodeGen::emitListPush(CallExpr& node) {
    node.args[0]->accept(*this);
    asm_.push_rax();
    
    node.args[1]->accept(*this);
    asm_.push_rax();
    
    std::string listName;
    size_t oldSize = 0;
    bool knownSize = false;
    if (auto* ident = dynamic_cast<Identifier*>(node.args[0].get())) {
        listName = ident->name;
        auto sizeIt = listSizes.find(listName);
        if (sizeIt != listSizes.end()) {
            oldSize = sizeIt->second;
            knownSize = true;
        }
    }
    
    if (knownSize && oldSize > 0) {
        size_t newSize = oldSize + 1;
        
        emitGCAllocList(newSize);
        
        allocLocal("$push_newlist");
        asm_.mov_mem_rbp_rax(locals["$push_newlist"]);
        
        for (size_t i = 0; i < oldSize; i++) {
            asm_.code.push_back(0x48); asm_.code.push_back(0x8B);
            asm_.code.push_back(0x44); asm_.code.push_back(0x24);
            asm_.code.push_back(0x08);
            
            if (i > 0) {
                asm_.add_rax_imm32((int32_t)(i * 8));
            }
            asm_.mov_rax_mem_rax();
            
            asm_.mov_rcx_mem_rbp(locals["$push_newlist"]);
            if (i > 0) {
                asm_.add_rcx_imm32((int32_t)(i * 8));
            }
            asm_.mov_mem_rcx_rax();
        }
        
        asm_.pop_rax();
        asm_.mov_rcx_mem_rbp(locals["$push_newlist"]);
        asm_.add_rcx_imm32((int32_t)(oldSize * 8));
        asm_.mov_mem_rcx_rax();
        
        asm_.pop_rcx();
        
        asm_.mov_rax_mem_rbp(locals["$push_newlist"]);
        
        if (!listName.empty()) {
            listSizes[listName] = newSize;
        }
    } else {
        allocLocal("$push_oldlist");
        allocLocal("$push_element");
        allocLocal("$push_oldsize");
        allocLocal("$push_newlist");
        
        asm_.pop_rax();
        asm_.mov_mem_rbp_rax(locals["$push_element"]);
        asm_.pop_rax();
        asm_.mov_mem_rbp_rax(locals["$push_oldlist"]);
        
        asm_.mov_rax_mem_rax();
        asm_.mov_mem_rbp_rax(locals["$push_oldsize"]);
        
        asm_.add_rax_imm32(2);
        asm_.code.push_back(0x48); asm_.code.push_back(0xC1);
        asm_.code.push_back(0xE0); asm_.code.push_back(0x03);
        asm_.push_rax();
        
        if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
        asm_.call_mem_rip(pe_.getImportRVA("GetProcessHeap"));
        asm_.mov_rcx_rax();
        asm_.xor_rax_rax();
        asm_.mov_rdx_rax();
        asm_.code.push_back(0x41); asm_.code.push_back(0x58);
        asm_.call_mem_rip(pe_.getImportRVA("HeapAlloc"));
        if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
        
        asm_.mov_mem_rbp_rax(locals["$push_newlist"]);
        
        asm_.mov_rcx_mem_rbp(locals["$push_oldsize"]);
        asm_.inc_rcx();
        asm_.mov_mem_rax_rcx();
        
        allocLocal("$push_idx");
        asm_.xor_rax_rax();
        asm_.mov_mem_rbp_rax(locals["$push_idx"]);
        
        std::string copyLoop = newLabel("push_copy");
        std::string copyDone = newLabel("push_done");
        
        asm_.label(copyLoop);
        asm_.mov_rax_mem_rbp(locals["$push_idx"]);
        asm_.cmp_rax_mem_rbp(locals["$push_oldsize"]);
        asm_.jge_rel32(copyDone);
        
        asm_.mov_rcx_mem_rbp(locals["$push_oldlist"]);
        asm_.mov_rax_mem_rbp(locals["$push_idx"]);
        asm_.inc_rax();
        asm_.code.push_back(0x48); asm_.code.push_back(0xC1);
        asm_.code.push_back(0xE0); asm_.code.push_back(0x03);
        asm_.add_rax_rcx();
        asm_.mov_rax_mem_rax();
        asm_.push_rax();
        
        asm_.mov_rcx_mem_rbp(locals["$push_newlist"]);
        asm_.mov_rax_mem_rbp(locals["$push_idx"]);
        asm_.inc_rax();
        asm_.code.push_back(0x48); asm_.code.push_back(0xC1);
        asm_.code.push_back(0xE0); asm_.code.push_back(0x03);
        asm_.add_rax_rcx();
        asm_.pop_rcx();
        asm_.mov_mem_rax_rcx();
        
        asm_.mov_rax_mem_rbp(locals["$push_idx"]);
        asm_.inc_rax();
        asm_.mov_mem_rbp_rax(locals["$push_idx"]);
        asm_.jmp_rel32(copyLoop);
        
        asm_.label(copyDone);
        
        asm_.mov_rcx_mem_rbp(locals["$push_newlist"]);
        asm_.mov_rax_mem_rbp(locals["$push_oldsize"]);
        asm_.inc_rax();
        asm_.code.push_back(0x48); asm_.code.push_back(0xC1);
        asm_.code.push_back(0xE0); asm_.code.push_back(0x03);
        asm_.add_rax_rcx();
        asm_.mov_rcx_mem_rbp(locals["$push_element"]);
        asm_.mov_mem_rax_rcx();
        
        asm_.mov_rax_mem_rbp(locals["$push_newlist"]);
    }
}

void NativeCodeGen::emitListPop(CallExpr& node) {
    node.args[0]->accept(*this);
    
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
        asm_.add_rax_imm32((int32_t)((listSize - 1) * 8));
        asm_.mov_rax_mem_rax();
        
        if (!listName.empty()) {
            listSizes[listName] = listSize - 1;
        }
    } else {
        allocLocal("$pop_list");
        asm_.mov_mem_rbp_rax(locals["$pop_list"]);
        
        asm_.mov_rcx_mem_rax();
        
        asm_.code.push_back(0x48); asm_.code.push_back(0xC1);
        asm_.code.push_back(0xE1); asm_.code.push_back(0x03);
        asm_.add_rax_rcx();
        asm_.mov_rax_mem_rax();
    }
}

void NativeCodeGen::emitListContains(CallExpr& node) {
    std::string haystack, needle;
    bool haystackConst = tryEvalConstantString(node.args[0].get(), haystack);
    bool needleConst = tryEvalConstantString(node.args[1].get(), needle);
    
    if (haystackConst && needleConst) {
        bool found = haystack.find(needle) != std::string::npos;
        asm_.mov_rax_imm64(found ? 1 : 0);
        return;
    }
    asm_.xor_rax_rax();
}

void NativeCodeGen::emitRange(CallExpr& node) {
    (void)node;
    asm_.xor_rax_rax();
}

} // namespace tyl
