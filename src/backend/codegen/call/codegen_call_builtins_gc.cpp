// Tyl Compiler - Native Code Generator GC Builtin Calls
// Handles: gc_collect, gc_stats, gc_count, gc_pin, gc_unpin, gc_add_root, gc_remove_root,
//          set_allocator, reset_allocator, allocator_stats, allocator_peak

#include "backend/codegen/codegen_base.h"

namespace tyl {

void NativeCodeGen::emitGCCollect(CallExpr& node) {
    (void)node;
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
    asm_.call_rel32(gcCollectLabel_);
    if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
    asm_.xor_rax_rax();
}

void NativeCodeGen::emitGCStats(CallExpr& node) {
    (void)node;
    asm_.lea_rax_rip_fixup(gcDataRVA_ + 8);
    asm_.mov_rax_mem_rax();
}

void NativeCodeGen::emitGCCount(CallExpr& node) {
    (void)node;
    asm_.lea_rax_rip_fixup(gcDataRVA_ + 32);
    asm_.mov_rax_mem_rax();
}

void NativeCodeGen::emitGCPin(CallExpr& node) {
    node.args[0]->accept(*this);
    // Set pinned flag (bit 0) in object header flags at rax - 9
    asm_.code.push_back(0x80); // or byte [rax-9], 1
    asm_.code.push_back(0x48);
    asm_.code.push_back(0xF7); // -9 as signed byte
    asm_.code.push_back(0x01); // OR with 1 (GC_FLAG_PINNED)
    asm_.xor_rax_rax();
}

void NativeCodeGen::emitGCUnpin(CallExpr& node) {
    node.args[0]->accept(*this);
    // Clear pinned flag (bit 0) in object header flags at rax - 9
    asm_.code.push_back(0x80); // and byte [rax-9], 0xFE
    asm_.code.push_back(0x60);
    asm_.code.push_back(0xF7); // -9 as signed byte
    asm_.code.push_back(0xFE); // AND with ~1
    asm_.xor_rax_rax();
}

void NativeCodeGen::emitGCAddRoot(CallExpr& node) {
    node.args[0]->accept(*this);
    // For now, no-op since conservative stack scanning finds pointers
    asm_.xor_rax_rax();
}

void NativeCodeGen::emitGCRemoveRoot(CallExpr& node) {
    node.args[0]->accept(*this);
    // For now, no-op
    asm_.xor_rax_rax();
}

void NativeCodeGen::emitSetAllocator(CallExpr& node) {
    node.args[0]->accept(*this);
    asm_.push_rax();
    
    node.args[1]->accept(*this);
    asm_.mov_rdx_rax();
    
    asm_.pop_rcx();
    
    // Store in GC data section
    asm_.lea_rax_rip_fixup(gcDataRVA_ + 48);
    asm_.mov_mem_rax_rcx();
    
    asm_.lea_rax_rip_fixup(gcDataRVA_ + 56);
    asm_.code.push_back(0x48); asm_.code.push_back(0x89); asm_.code.push_back(0x10); // mov [rax], rdx
    
    asm_.xor_rax_rax();
}

void NativeCodeGen::emitResetAllocator(CallExpr& node) {
    (void)node;
    asm_.xor_rax_rax();
    asm_.lea_rcx_rip_fixup(gcDataRVA_ + 48);
    asm_.mov_mem_rcx_rax();
    
    asm_.lea_rcx_rip_fixup(gcDataRVA_ + 56);
    asm_.mov_mem_rcx_rax();
}

void NativeCodeGen::emitAllocatorStats(CallExpr& node) {
    (void)node;
    asm_.lea_rax_rip_fixup(gcDataRVA_ + 8);
    asm_.mov_rax_mem_rax();
}

void NativeCodeGen::emitAllocatorPeak(CallExpr& node) {
    (void)node;
    asm_.lea_rax_rip_fixup(gcDataRVA_ + 8);
    asm_.mov_rax_mem_rax();
}

} // namespace tyl
