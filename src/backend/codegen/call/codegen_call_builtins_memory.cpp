// Tyl Compiler - Native Code Generator Memory Builtin Calls
// Handles: alloc, free, stackalloc, sizeof, alignof, offsetof, placement_new, memcpy, memset, memmove, memcmp

#include "backend/codegen/codegen_base.h"

namespace tyl {

void NativeCodeGen::emitMemAlloc(CallExpr& node) {
    node.args[0]->accept(*this);
    asm_.mov_r8_rax();
    
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("GetProcessHeap"));
    asm_.mov_rcx_rax();
    asm_.mov_rdx_imm64(0x08);
    asm_.call_mem_rip(pe_.getImportRVA("HeapAlloc"));
    if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
}

void NativeCodeGen::emitMemFree(CallExpr& node) {
    node.args[0]->accept(*this);
    asm_.mov_r8_rax();
    
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("GetProcessHeap"));
    asm_.mov_rcx_rax();
    asm_.xor_rax_rax();
    asm_.mov_rdx_rax();
    asm_.call_mem_rip(pe_.getImportRVA("HeapFree"));
    if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
    
    asm_.xor_rax_rax();
}

void NativeCodeGen::emitMemStackAlloc(CallExpr& node) {
    node.args[0]->accept(*this);
    // Align size to 16 bytes for stack alignment
    asm_.add_rax_imm32(15);
    asm_.code.push_back(0x48); asm_.code.push_back(0x83);
    asm_.code.push_back(0xE0); asm_.code.push_back(0xF0); // and rax, ~15
    // sub rsp, rax
    asm_.code.push_back(0x48); asm_.code.push_back(0x29);
    asm_.code.push_back(0xC4); // sub rsp, rax
    // mov rax, rsp (return the stack pointer)
    asm_.code.push_back(0x48); asm_.code.push_back(0x89);
    asm_.code.push_back(0xE0); // mov rax, rsp
}

void NativeCodeGen::emitMemSizeof(CallExpr& node) {
    if (auto* typeIdent = dynamic_cast<Identifier*>(node.args[0].get())) {
        std::string typeName = typeIdent->name;
        int64_t size = getTypeSize(typeName);
        asm_.mov_rax_imm64(size);
        return;
    }
    // Default to pointer size
    asm_.mov_rax_imm64(8);
}

void NativeCodeGen::emitMemAlignof(CallExpr& node) {
    if (auto* typeIdent = dynamic_cast<Identifier*>(node.args[0].get())) {
        std::string typeName = typeIdent->name;
        int64_t alignment = getTypeAlignment(typeName);
        asm_.mov_rax_imm64(alignment);
        return;
    }
    // Default to pointer alignment
    asm_.mov_rax_imm64(8);
}

void NativeCodeGen::emitMemOffsetof(CallExpr& node) {
    if (auto* recordIdent = dynamic_cast<Identifier*>(node.args[0].get())) {
        if (auto* fieldIdent = dynamic_cast<Identifier*>(node.args[1].get())) {
            std::string recordName = recordIdent->name;
            std::string fieldName = fieldIdent->name;
            int64_t offset = 0;
            
            auto it = recordTypes_.find(recordName);
            if (it != recordTypes_.end()) {
                for (size_t i = 0; i < it->second.fieldNames.size(); i++) {
                    if (it->second.fieldNames[i] == fieldName) {
                        offset = getRecordFieldOffset(recordName, static_cast<int>(i));
                        offset -= 8; // Subtract GC header
                        break;
                    }
                }
            }
            
            asm_.mov_rax_imm64(offset);
            return;
        }
    }
    asm_.mov_rax_imm64(0);
}

void NativeCodeGen::emitMemPlacementNew(CallExpr& node) {
    node.args[0]->accept(*this);
    asm_.push_rax();
    
    node.args[1]->accept(*this);
    asm_.mov_rcx_rax();
    
    asm_.pop_rax();
    asm_.code.push_back(0x48); asm_.code.push_back(0x89);
    asm_.code.push_back(0x08); // mov [rax], rcx
}

void NativeCodeGen::emitMemcpy(CallExpr& node) {
    node.args[0]->accept(*this);
    asm_.push_rax();
    node.args[1]->accept(*this);
    asm_.push_rax();
    node.args[2]->accept(*this);
    
    asm_.push_rdi();
    asm_.code.push_back(0x56); // push rsi
    
    asm_.mov_rcx_rax();
    
    asm_.code.push_back(0x48); asm_.code.push_back(0x8B);
    asm_.code.push_back(0x74); asm_.code.push_back(0x24);
    asm_.code.push_back(0x10); // mov rsi, [rsp+16] - src
    
    asm_.code.push_back(0x48); asm_.code.push_back(0x8B);
    asm_.code.push_back(0x7C); asm_.code.push_back(0x24);
    asm_.code.push_back(0x18); // mov rdi, [rsp+24] - dst
    
    asm_.code.push_back(0xFC); // cld
    asm_.code.push_back(0xF3); // rep
    asm_.code.push_back(0xA4); // movsb
    
    asm_.code.push_back(0x5E); // pop rsi
    asm_.pop_rdi();
    
    asm_.pop_rax();
    asm_.pop_rax();
}

void NativeCodeGen::emitMemset(CallExpr& node) {
    node.args[0]->accept(*this);
    asm_.push_rax();
    node.args[1]->accept(*this);
    asm_.push_rax();
    node.args[2]->accept(*this);
    
    asm_.push_rdi();
    
    asm_.mov_rcx_rax();
    
    asm_.code.push_back(0x48); asm_.code.push_back(0x8B);
    asm_.code.push_back(0x44); asm_.code.push_back(0x24);
    asm_.code.push_back(0x08); // mov rax, [rsp+8] - val
    
    asm_.code.push_back(0x48); asm_.code.push_back(0x8B);
    asm_.code.push_back(0x7C); asm_.code.push_back(0x24);
    asm_.code.push_back(0x10); // mov rdi, [rsp+16] - ptr
    
    asm_.code.push_back(0xFC); // cld
    asm_.code.push_back(0xF3); // rep
    asm_.code.push_back(0xAA); // stosb
    
    asm_.pop_rdi();
    
    asm_.pop_rax();
    asm_.pop_rax();
}

void NativeCodeGen::emitMemmove(CallExpr& node) {
    node.args[0]->accept(*this);
    asm_.push_rax();
    node.args[1]->accept(*this);
    asm_.push_rax();
    node.args[2]->accept(*this);
    
    asm_.push_rdi();
    asm_.code.push_back(0x56); // push rsi
    
    asm_.mov_rcx_rax();
    
    asm_.code.push_back(0x48); asm_.code.push_back(0x8B);
    asm_.code.push_back(0x74); asm_.code.push_back(0x24);
    asm_.code.push_back(0x10); // mov rsi, [rsp+16] - src
    
    asm_.code.push_back(0x48); asm_.code.push_back(0x8B);
    asm_.code.push_back(0x7C); asm_.code.push_back(0x24);
    asm_.code.push_back(0x18); // mov rdi, [rsp+24] - dst
    
    // Check if dst > src (need to copy backwards)
    asm_.code.push_back(0x48); asm_.code.push_back(0x39);
    asm_.code.push_back(0xF7); // cmp rdi, rsi
    
    std::string forwardLabel = newLabel("memmove_forward");
    std::string doneLabel = newLabel("memmove_done");
    
    asm_.jbe_rel32(forwardLabel);
    
    // Copy backwards
    asm_.code.push_back(0x48); asm_.code.push_back(0x01);
    asm_.code.push_back(0xCF); // add rdi, rcx
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF);
    asm_.code.push_back(0xCF); // dec rdi
    
    asm_.code.push_back(0x48); asm_.code.push_back(0x01);
    asm_.code.push_back(0xCE); // add rsi, rcx
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF);
    asm_.code.push_back(0xCE); // dec rsi
    
    asm_.code.push_back(0xFD); // std
    asm_.code.push_back(0xF3); // rep
    asm_.code.push_back(0xA4); // movsb
    asm_.code.push_back(0xFC); // cld
    
    asm_.jmp_rel32(doneLabel);
    
    asm_.label(forwardLabel);
    asm_.code.push_back(0xFC); // cld
    asm_.code.push_back(0xF3); // rep
    asm_.code.push_back(0xA4); // movsb
    
    asm_.label(doneLabel);
    asm_.code.push_back(0x5E); // pop rsi
    asm_.pop_rdi();
    
    asm_.pop_rax();
    asm_.pop_rax();
}

void NativeCodeGen::emitMemcmp(CallExpr& node) {
    node.args[0]->accept(*this);
    asm_.push_rax();
    node.args[1]->accept(*this);
    asm_.push_rax();
    node.args[2]->accept(*this);
    
    asm_.push_rdi();
    asm_.code.push_back(0x56); // push rsi
    
    asm_.mov_rcx_rax();
    
    asm_.code.push_back(0x48); asm_.code.push_back(0x8B);
    asm_.code.push_back(0x7C); asm_.code.push_back(0x24);
    asm_.code.push_back(0x10); // mov rdi, [rsp+16] - b
    
    asm_.code.push_back(0x48); asm_.code.push_back(0x8B);
    asm_.code.push_back(0x74); asm_.code.push_back(0x24);
    asm_.code.push_back(0x18); // mov rsi, [rsp+24] - a
    
    std::string loopLabel = newLabel("memcmp_loop");
    std::string equalLabel = newLabel("memcmp_equal");
    std::string lessLabel = newLabel("memcmp_less");
    std::string greaterLabel = newLabel("memcmp_greater");
    std::string doneLabel = newLabel("memcmp_done");
    
    asm_.label(loopLabel);
    asm_.code.push_back(0x48); asm_.code.push_back(0x85);
    asm_.code.push_back(0xC9); // test rcx, rcx
    asm_.jz_rel32(equalLabel);
    
    asm_.code.push_back(0x0F); asm_.code.push_back(0xB6);
    asm_.code.push_back(0x06); // movzx eax, byte [rsi]
    
    asm_.code.push_back(0x0F); asm_.code.push_back(0xB6);
    asm_.code.push_back(0x17); // movzx edx, byte [rdi]
    
    asm_.code.push_back(0x39); asm_.code.push_back(0xD0); // cmp eax, edx
    
    asm_.jl_rel32(lessLabel);
    asm_.jg_rel32(greaterLabel);
    
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF);
    asm_.code.push_back(0xC6); // inc rsi
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF);
    asm_.code.push_back(0xC7); // inc rdi
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF);
    asm_.code.push_back(0xC9); // dec rcx
    
    asm_.jmp_rel32(loopLabel);
    
    asm_.label(lessLabel);
    asm_.mov_rax_imm64(-1);
    asm_.jmp_rel32(doneLabel);
    
    asm_.label(greaterLabel);
    asm_.mov_rax_imm64(1);
    asm_.jmp_rel32(doneLabel);
    
    asm_.label(equalLabel);
    asm_.xor_rax_rax();
    
    asm_.label(doneLabel);
    asm_.code.push_back(0x5E); // pop rsi
    asm_.pop_rdi();
    
    asm_.add_rsp_imm32(16);
}

} // namespace tyl
