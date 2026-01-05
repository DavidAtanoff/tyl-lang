// Tyl Compiler - Native Code Generator Async Expressions
// Handles: AwaitExpr, SpawnExpr, PropagateExpr, DSLBlock

#include "backend/codegen/codegen_base.h"

namespace tyl {

void NativeCodeGen::visit(AwaitExpr& node) {
    node.operand->accept(*this);
    
    asm_.cmp_rax_imm32(0x1000);
    std::string notHandle = newLabel("await_not_handle");
    std::string done = newLabel("await_done");
    asm_.jl_rel32(notHandle);
    
    allocLocal("$await_handle");
    asm_.mov_mem_rbp_rax(locals["$await_handle"]);
    
    asm_.mov_rcx_rax();
    asm_.mov_rdx_imm64(0xFFFFFFFF);
    
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("WaitForSingleObject"));
    if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
    
    allocLocal("$await_result");
    asm_.mov_rcx_mem_rbp(locals["$await_handle"]);
    asm_.lea_rdx_rbp_offset(locals["$await_result"]);
    
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("GetExitCodeThread"));
    if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
    
    asm_.mov_rcx_mem_rbp(locals["$await_handle"]);
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("CloseHandle"));
    if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
    
    asm_.mov_rax_mem_rbp(locals["$await_result"]);
    asm_.jmp_rel32(done);
    
    asm_.label(notHandle);
    asm_.label(done);
}

void NativeCodeGen::visit(SpawnExpr& node) {
    if (auto* call = dynamic_cast<CallExpr*>(node.operand.get())) {
        if (auto* ident = dynamic_cast<Identifier*>(call->callee.get())) {
            if (asm_.labels.count(ident->name)) {
                std::string thunkLabel = newLabel("spawn_thunk_" + ident->name);
                std::string afterThunk = newLabel("spawn_after_thunk");
                
                asm_.jmp_rel32(afterThunk);
                asm_.label(thunkLabel);
                
                asm_.push_rbp();
                asm_.mov_rbp_rsp();
                asm_.push_rdi();
                asm_.sub_rsp_imm32(0x30);
                
                if (call->args.size() == 1) {
                    asm_.mov_mem_rbp_rcx(-0x10);
                }
                
                asm_.mov_ecx_imm32(-11);
                asm_.call_mem_rip(pe_.getImportRVA("GetStdHandle"));
                asm_.mov_rdi_rax();
                
                if (call->args.size() == 1) {
                    asm_.mov_rcx_mem_rbp(-0x10);
                }
                
                asm_.call_rel32(ident->name);
                
                asm_.add_rsp_imm32(0x30);
                asm_.pop_rdi();
                asm_.pop_rbp();
                asm_.ret();
                
                asm_.label(afterThunk);
                
                if (call->args.size() == 1) {
                    call->args[0]->accept(*this);
                    asm_.code.push_back(0x49); asm_.code.push_back(0x89); asm_.code.push_back(0xC1);
                } else {
                    asm_.code.push_back(0x4D); asm_.code.push_back(0x31); asm_.code.push_back(0xC9);
                }
                
                asm_.code.push_back(0x4C); asm_.code.push_back(0x8D); asm_.code.push_back(0x05);
                asm_.fixupLabel(thunkLabel);
                
                asm_.xor_rax_rax();
                asm_.mov_rcx_rax();
                asm_.mov_rdx_rax();
                
                asm_.code.push_back(0x48); asm_.code.push_back(0x89);
                asm_.code.push_back(0x44); asm_.code.push_back(0x24); asm_.code.push_back(0x20);
                
                asm_.code.push_back(0x48); asm_.code.push_back(0x89);
                asm_.code.push_back(0x44); asm_.code.push_back(0x24); asm_.code.push_back(0x28);
                
                if (!stackAllocated_) asm_.sub_rsp_imm32(0x30);
                asm_.call_mem_rip(pe_.getImportRVA("CreateThread"));
                if (!stackAllocated_) asm_.add_rsp_imm32(0x30);
                
                return;
            }
        }
    }
    
    node.operand->accept(*this);
}

void NativeCodeGen::visit(DSLBlock& node) {
    uint32_t offset = addString(node.rawContent);
    asm_.lea_rax_rip_fixup(offset);
}

void NativeCodeGen::visit(PropagateExpr& node) {
    node.operand->accept(*this);
    
    asm_.push_rax();
    
    // Check error bit
    asm_.code.push_back(0x48); asm_.code.push_back(0x83); 
    asm_.code.push_back(0xE0); asm_.code.push_back(0x01);
    
    std::string okLabel = newLabel("propagate_ok");
    
    asm_.test_rax_rax();
    asm_.jnz_rel32(okLabel);
    
    asm_.pop_rax();
    
    // Early return on error
    asm_.code.push_back(0x48); asm_.code.push_back(0x89); asm_.code.push_back(0xEC);
    asm_.code.push_back(0x5D);
    asm_.code.push_back(0xC3);
    
    asm_.label(okLabel);
    asm_.pop_rax();
    // Shift right to get value
    asm_.code.push_back(0x48); asm_.code.push_back(0xD1); asm_.code.push_back(0xE8);
}

} // namespace tyl
