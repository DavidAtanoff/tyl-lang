// Tyl Compiler - x86-64 Assembler Implementation
#include "x64_assembler.h"

namespace tyl {

void X64Assembler::emit8(uint8_t b) { code.push_back(b); }

void X64Assembler::emit32(int32_t val) {
    code.push_back(val & 0xFF);
    code.push_back((val >> 8) & 0xFF);
    code.push_back((val >> 16) & 0xFF);
    code.push_back((val >> 24) & 0xFF);
}

void X64Assembler::emit64(int64_t val) {
    for (int i = 0; i < 8; i++) code.push_back((val >> (i * 8)) & 0xFF);
}

void X64Assembler::label(const std::string& name) { labels[name] = code.size(); }
void X64Assembler::fixupLabel(const std::string& name) { labelFixups.push_back({code.size(), name}); emit32(0); }
void X64Assembler::fixupRIP(uint32_t targetRVA) { ripFixups.push_back({code.size(), targetRVA}); emit32(0); }

void X64Assembler::resolve(uint32_t codeRVA) {
    for (auto& [offset, name] : labelFixups) {
        auto it = labels.find(name);
        if (it != labels.end()) {
            int32_t rel = (int32_t)(it->second - offset - 4);
            code[offset] = rel & 0xFF;
            code[offset + 1] = (rel >> 8) & 0xFF;
            code[offset + 2] = (rel >> 16) & 0xFF;
            code[offset + 3] = (rel >> 24) & 0xFF;
        }
    }
    for (auto& [offset, targetRVA] : ripFixups) {
        uint32_t ripAtExec = codeRVA + (uint32_t)offset + 4;
        int32_t rel = (int32_t)(targetRVA - ripAtExec);
        code[offset] = rel & 0xFF;
        code[offset + 1] = (rel >> 8) & 0xFF;
        code[offset + 2] = (rel >> 16) & 0xFF;
        code[offset + 3] = (rel >> 24) & 0xFF;
    }
}

void X64Assembler::mov_rax_imm64(int64_t val) { emit8(0x48); emit8(0xB8); emit64(val); }
void X64Assembler::mov_rcx_imm64(int64_t val) { emit8(0x48); emit8(0xB9); emit64(val); }
void X64Assembler::mov_rdx_imm64(int64_t val) { emit8(0x48); emit8(0xBA); emit64(val); }
void X64Assembler::mov_r8_imm64(int64_t val) { emit8(0x49); emit8(0xB8); emit64(val); }
void X64Assembler::mov_r8d_imm32(int32_t val) { emit8(0x41); emit8(0xB8); emit32(val); }  // mov r8d, imm32
void X64Assembler::mov_rax_imm32(int32_t val) { emit8(0x48); emit8(0xC7); emit8(0xC0); emit32(val); }
void X64Assembler::mov_ecx_imm32(int32_t val) { emit8(0xB9); emit32(val); }
void X64Assembler::mov_edx_imm32(int32_t val) { emit8(0xBA); emit32(val); }
void X64Assembler::mov_rax_rcx() { emit8(0x48); emit8(0x89); emit8(0xC8); }
void X64Assembler::mov_rcx_rax() { emit8(0x48); emit8(0x89); emit8(0xC1); }
void X64Assembler::mov_rdx_rax() { emit8(0x48); emit8(0x89); emit8(0xC2); }
void X64Assembler::mov_r8_rax() { emit8(0x49); emit8(0x89); emit8(0xC0); }
void X64Assembler::mov_r8_rcx() { emit8(0x49); emit8(0x89); emit8(0xC8); }
void X64Assembler::mov_rax_mem_rbp(int32_t offset) { emit8(0x48); emit8(0x8B); emit8(0x85); emit32(offset); }
void X64Assembler::mov_rcx_mem_rbp(int32_t offset) { emit8(0x48); emit8(0x8B); emit8(0x8D); emit32(offset); }
void X64Assembler::mov_rdx_mem_rbp(int32_t offset) { emit8(0x48); emit8(0x8B); emit8(0x95); emit32(offset); }
void X64Assembler::mov_mem_rbp_rax(int32_t offset) { emit8(0x48); emit8(0x89); emit8(0x85); emit32(offset); }
void X64Assembler::mov_mem_rbp_rcx(int32_t offset) { emit8(0x48); emit8(0x89); emit8(0x8D); emit32(offset); }
void X64Assembler::mov_mem_rbp_rdx(int32_t offset) { emit8(0x48); emit8(0x89); emit8(0x95); emit32(offset); }
void X64Assembler::mov_rax_mem_rax() { emit8(0x48); emit8(0x8B); emit8(0x00); }
void X64Assembler::mov_mem_rcx_rax() { emit8(0x48); emit8(0x89); emit8(0x01); }
void X64Assembler::mov_mem_rax_rcx() { emit8(0x48); emit8(0x89); emit8(0x08); }
void X64Assembler::mov_rcx_mem_rax() { emit8(0x48); emit8(0x8B); emit8(0x08); }
void X64Assembler::mov_rdx_mem_rax() { emit8(0x48); emit8(0x8B); emit8(0x10); }  // rdx = [rax]
void X64Assembler::mov_rax_mem_rcx() { emit8(0x48); emit8(0x8B); emit8(0x01); }  // mov rax, [rcx]
void X64Assembler::lea_rcx_rip_fixup(uint32_t targetRVA) { emit8(0x48); emit8(0x8D); emit8(0x0D); fixupRIP(targetRVA); }
void X64Assembler::lea_rax_rip_fixup(uint32_t targetRVA) { emit8(0x48); emit8(0x8D); emit8(0x05); fixupRIP(targetRVA); }
void X64Assembler::lea_rax_rbp(int32_t offset) { emit8(0x48); emit8(0x8D); emit8(0x85); emit32(offset); }
void X64Assembler::lea_rcx_rbp(int32_t offset) { emit8(0x48); emit8(0x8D); emit8(0x8D); emit32(offset); }
void X64Assembler::lea_rdx_rbp_offset(int32_t offset) { emit8(0x48); emit8(0x8D); emit8(0x95); emit32(offset); }


void X64Assembler::push_rbp() { emit8(0x55); }
void X64Assembler::pop_rbp() { emit8(0x5D); }
void X64Assembler::push_rax() { emit8(0x50); }
void X64Assembler::pop_rax() { emit8(0x58); }
void X64Assembler::push_rcx() { emit8(0x51); }
void X64Assembler::pop_rcx() { emit8(0x59); }
void X64Assembler::push_rdx() { emit8(0x52); }
void X64Assembler::pop_rdx() { emit8(0x5A); }
void X64Assembler::push_rdi() { emit8(0x57); }
void X64Assembler::pop_rdi() { emit8(0x5F); }
void X64Assembler::pop_r8() { emit8(0x41); emit8(0x58); }
void X64Assembler::pop_r9() { emit8(0x41); emit8(0x59); }
void X64Assembler::mov_rbp_rsp() { emit8(0x48); emit8(0x89); emit8(0xE5); }
void X64Assembler::mov_rsp_rbp() { emit8(0x48); emit8(0x89); emit8(0xEC); }
void X64Assembler::sub_rsp_imm32(int32_t val) { emit8(0x48); emit8(0x81); emit8(0xEC); emit32(val); }
void X64Assembler::add_rsp_imm32(int32_t val) { emit8(0x48); emit8(0x81); emit8(0xC4); emit32(val); }
void X64Assembler::add_rax_rcx() { emit8(0x48); emit8(0x01); emit8(0xC8); }
void X64Assembler::sub_rax_rcx() { emit8(0x48); emit8(0x29); emit8(0xC8); }
void X64Assembler::imul_rax_rcx() { emit8(0x48); emit8(0x0F); emit8(0xAF); emit8(0xC1); }
void X64Assembler::cqo() { emit8(0x48); emit8(0x99); }
void X64Assembler::idiv_rcx() { emit8(0x48); emit8(0xF7); emit8(0xF9); }
void X64Assembler::neg_rax() { emit8(0x48); emit8(0xF7); emit8(0xD8); }
void X64Assembler::inc_rax() { emit8(0x48); emit8(0xFF); emit8(0xC0); }
void X64Assembler::inc_rcx() { emit8(0x48); emit8(0xFF); emit8(0xC1); }
void X64Assembler::dec_rax() { emit8(0x48); emit8(0xFF); emit8(0xC8); }
void X64Assembler::cmp_rax_rcx() { emit8(0x48); emit8(0x39); emit8(0xC8); }
void X64Assembler::cmp_rax_imm32(int32_t val) { emit8(0x48); emit8(0x3D); emit32(val); }
void X64Assembler::cmp_rax_imm8(int8_t val) { 
    // cmp rax, imm8 (sign-extended) - 48 83 F8 ib
    emit8(0x48); emit8(0x83); emit8(0xF8); emit8(static_cast<uint8_t>(val)); 
}
void X64Assembler::cmp_rax_mem_rbp(int32_t offset) { emit8(0x48); emit8(0x3B); emit8(0x85); emit32(offset); }
void X64Assembler::test_rax_rax() { emit8(0x48); emit8(0x85); emit8(0xC0); }
void X64Assembler::sete_al() { emit8(0x0F); emit8(0x94); emit8(0xC0); }
void X64Assembler::setne_al() { emit8(0x0F); emit8(0x95); emit8(0xC0); }
void X64Assembler::setl_al() { emit8(0x0F); emit8(0x9C); emit8(0xC0); }
void X64Assembler::setg_al() { emit8(0x0F); emit8(0x9F); emit8(0xC0); }
void X64Assembler::setle_al() { emit8(0x0F); emit8(0x9E); emit8(0xC0); }
void X64Assembler::setge_al() { emit8(0x0F); emit8(0x9D); emit8(0xC0); }
void X64Assembler::movzx_rax_al() { emit8(0x48); emit8(0x0F); emit8(0xB6); emit8(0xC0); }
void X64Assembler::xor_rax_rax() { emit8(0x48); emit8(0x31); emit8(0xC0); }
void X64Assembler::xor_ecx_ecx() { emit8(0x31); emit8(0xC9); }
void X64Assembler::and_rax_rcx() { emit8(0x48); emit8(0x21); emit8(0xC8); }
void X64Assembler::or_rax_rcx() { emit8(0x48); emit8(0x09); emit8(0xC8); }

// ============================================
// Callee-saved register operations
// ============================================

// RBX operations
void X64Assembler::push_rbx() { emit8(0x53); }
void X64Assembler::pop_rbx() { emit8(0x5B); }
void X64Assembler::mov_rbx_rax() { emit8(0x48); emit8(0x89); emit8(0xC3); }  // mov rbx, rax
void X64Assembler::mov_rax_rbx() { emit8(0x48); emit8(0x89); emit8(0xD8); }  // mov rax, rbx
void X64Assembler::mov_rbx_rcx() { emit8(0x48); emit8(0x89); emit8(0xCB); }  // mov rbx, rcx
void X64Assembler::mov_rbx_rdx() { emit8(0x48); emit8(0x89); emit8(0xD3); }  // mov rbx, rdx
void X64Assembler::mov_rbx_r8() { emit8(0x4C); emit8(0x89); emit8(0xC3); }   // mov rbx, r8
void X64Assembler::mov_rbx_r9() { emit8(0x4C); emit8(0x89); emit8(0xCB); }   // mov rbx, r9
void X64Assembler::mov_rbx_imm64(int64_t val) { emit8(0x48); emit8(0xBB); emit64(val); }
void X64Assembler::mov_rbx_imm32(int32_t val) { emit8(0xBB); emit32(val); }  // mov ebx, imm32 (zero-extends)
void X64Assembler::xor_rbx_rbx() { emit8(0x48); emit8(0x31); emit8(0xDB); }

// R12 operations
void X64Assembler::push_r12() { emit8(0x41); emit8(0x54); }
void X64Assembler::pop_r12() { emit8(0x41); emit8(0x5C); }
void X64Assembler::mov_r12_rax() { emit8(0x49); emit8(0x89); emit8(0xC4); }  // mov r12, rax
void X64Assembler::mov_rax_r12() { emit8(0x4C); emit8(0x89); emit8(0xE0); }  // mov rax, r12
void X64Assembler::mov_r12_rcx() { emit8(0x49); emit8(0x89); emit8(0xCC); }  // mov r12, rcx
void X64Assembler::mov_r12_rdx() { emit8(0x49); emit8(0x89); emit8(0xD4); }  // mov r12, rdx
void X64Assembler::mov_r12_r8() { emit8(0x4D); emit8(0x89); emit8(0xC4); }   // mov r12, r8
void X64Assembler::mov_r12_r9() { emit8(0x4D); emit8(0x89); emit8(0xCC); }   // mov r12, r9
void X64Assembler::mov_r12_imm64(int64_t val) { emit8(0x49); emit8(0xBC); emit64(val); }
void X64Assembler::xor_r12_r12() { emit8(0x4D); emit8(0x31); emit8(0xE4); }

// R13 operations
void X64Assembler::push_r13() { emit8(0x41); emit8(0x55); }
void X64Assembler::pop_r13() { emit8(0x41); emit8(0x5D); }
void X64Assembler::mov_r13_rax() { emit8(0x49); emit8(0x89); emit8(0xC5); }  // mov r13, rax
void X64Assembler::mov_rax_r13() { emit8(0x4C); emit8(0x89); emit8(0xE8); }  // mov rax, r13
void X64Assembler::mov_r13_rcx() { emit8(0x49); emit8(0x89); emit8(0xCD); }  // mov r13, rcx
void X64Assembler::mov_r13_rdx() { emit8(0x49); emit8(0x89); emit8(0xD5); }  // mov r13, rdx
void X64Assembler::mov_r13_r8() { emit8(0x4D); emit8(0x89); emit8(0xC5); }   // mov r13, r8
void X64Assembler::mov_r13_r9() { emit8(0x4D); emit8(0x89); emit8(0xCD); }   // mov r13, r9
void X64Assembler::mov_r13_imm64(int64_t val) { emit8(0x49); emit8(0xBD); emit64(val); }
void X64Assembler::xor_r13_r13() { emit8(0x4D); emit8(0x31); emit8(0xED); }

// R14 operations
void X64Assembler::push_r14() { emit8(0x41); emit8(0x56); }
void X64Assembler::pop_r14() { emit8(0x41); emit8(0x5E); }
void X64Assembler::mov_r14_rax() { emit8(0x49); emit8(0x89); emit8(0xC6); }  // mov r14, rax
void X64Assembler::mov_rax_r14() { emit8(0x4C); emit8(0x89); emit8(0xF0); }  // mov rax, r14
void X64Assembler::mov_r14_rcx() { emit8(0x49); emit8(0x89); emit8(0xCE); }  // mov r14, rcx
void X64Assembler::mov_r14_rdx() { emit8(0x49); emit8(0x89); emit8(0xD6); }  // mov r14, rdx
void X64Assembler::mov_r14_r8() { emit8(0x4D); emit8(0x89); emit8(0xC6); }   // mov r14, r8
void X64Assembler::mov_r14_r9() { emit8(0x4D); emit8(0x89); emit8(0xCE); }   // mov r14, r9
void X64Assembler::mov_r14_imm64(int64_t val) { emit8(0x49); emit8(0xBE); emit64(val); }
void X64Assembler::xor_r14_r14() { emit8(0x4D); emit8(0x31); emit8(0xF6); }

// R15 operations
void X64Assembler::push_r15() { emit8(0x41); emit8(0x57); }
void X64Assembler::pop_r15() { emit8(0x41); emit8(0x5F); }
void X64Assembler::mov_r15_rax() { emit8(0x49); emit8(0x89); emit8(0xC7); }  // mov r15, rax
void X64Assembler::mov_rax_r15() { emit8(0x4C); emit8(0x89); emit8(0xF8); }  // mov rax, r15
void X64Assembler::mov_r15_rcx() { emit8(0x49); emit8(0x89); emit8(0xCF); }  // mov r15, rcx
void X64Assembler::mov_r15_rdx() { emit8(0x49); emit8(0x89); emit8(0xD7); }  // mov r15, rdx
void X64Assembler::mov_r15_r8() { emit8(0x4D); emit8(0x89); emit8(0xC7); }   // mov r15, r8
void X64Assembler::mov_r15_r9() { emit8(0x4D); emit8(0x89); emit8(0xCF); }   // mov r15, r9
void X64Assembler::mov_r15_imm64(int64_t val) { emit8(0x49); emit8(0xBF); emit64(val); }
void X64Assembler::xor_r15_r15() { emit8(0x4D); emit8(0x31); emit8(0xFF); }

// Move from callee-saved to RCX (for closure captures)
void X64Assembler::mov_rcx_rbx() { emit8(0x48); emit8(0x89); emit8(0xD9); }  // mov rcx, rbx
void X64Assembler::mov_rcx_r12() { emit8(0x4C); emit8(0x89); emit8(0xE1); }  // mov rcx, r12
void X64Assembler::mov_rcx_r13() { emit8(0x4C); emit8(0x89); emit8(0xE9); }  // mov rcx, r13
void X64Assembler::mov_rcx_r14() { emit8(0x4C); emit8(0x89); emit8(0xF1); }  // mov rcx, r14
void X64Assembler::mov_rcx_r15() { emit8(0x4C); emit8(0x89); emit8(0xF9); }  // mov rcx, r15

void X64Assembler::jmp_rel32(const std::string& lbl) { emit8(0xE9); fixupLabel(lbl); }
void X64Assembler::jz_rel32(const std::string& lbl) { emit8(0x0F); emit8(0x84); fixupLabel(lbl); }
void X64Assembler::je_rel32(const std::string& lbl) { emit8(0x0F); emit8(0x84); fixupLabel(lbl); }  // Same as jz
void X64Assembler::jnz_rel32(const std::string& lbl) { emit8(0x0F); emit8(0x85); fixupLabel(lbl); }
void X64Assembler::jne_rel32(const std::string& lbl) { emit8(0x0F); emit8(0x85); fixupLabel(lbl); }  // Same as jnz
void X64Assembler::jge_rel32(const std::string& lbl) { emit8(0x0F); emit8(0x8D); fixupLabel(lbl); }
void X64Assembler::jl_rel32(const std::string& lbl) { emit8(0x0F); emit8(0x8C); fixupLabel(lbl); }
void X64Assembler::jle_rel32(const std::string& lbl) { emit8(0x0F); emit8(0x8E); fixupLabel(lbl); }
void X64Assembler::jg_rel32(const std::string& lbl) { emit8(0x0F); emit8(0x8F); fixupLabel(lbl); }
void X64Assembler::ja_rel32(const std::string& lbl) { emit8(0x0F); emit8(0x87); fixupLabel(lbl); }
void X64Assembler::jb_rel32(const std::string& lbl) { emit8(0x0F); emit8(0x82); fixupLabel(lbl); }
void X64Assembler::jae_rel32(const std::string& lbl) { emit8(0x0F); emit8(0x83); fixupLabel(lbl); }
void X64Assembler::jbe_rel32(const std::string& lbl) { emit8(0x0F); emit8(0x86); fixupLabel(lbl); }
void X64Assembler::call_rel32(const std::string& lbl) { emit8(0xE8); fixupLabel(lbl); }
void X64Assembler::call_mem_rip(uint32_t iatRVA) { emit8(0xFF); emit8(0x15); fixupRIP(iatRVA); }
void X64Assembler::call_rax() { emit8(0xFF); emit8(0xD0); }
void X64Assembler::ret() { emit8(0xC3); }
void X64Assembler::nop() { emit8(0x90); }
void X64Assembler::int3() { emit8(0xCC); }

// ============================================
// SSE/SSE2 Floating Point Instructions
// ============================================

// movsd xmm0, [rip+disp32] - Load 64-bit float from memory
void X64Assembler::movsd_xmm0_mem_rip(uint32_t targetRVA) {
    emit8(0xF2); emit8(0x0F); emit8(0x10); emit8(0x05);  // movsd xmm0, [rip+disp32]
    fixupRIP(targetRVA);
}

// movsd xmm0, [rbp+offset]
void X64Assembler::movsd_xmm0_mem_rbp(int32_t offset) {
    emit8(0xF2); emit8(0x0F); emit8(0x10); emit8(0x85);  // movsd xmm0, [rbp+disp32]
    emit32(offset);
}

// movsd xmm1, [rbp+offset]
void X64Assembler::movsd_xmm1_mem_rbp(int32_t offset) {
    emit8(0xF2); emit8(0x0F); emit8(0x10); emit8(0x8D);  // movsd xmm1, [rbp+disp32]
    emit32(offset);
}

// movsd [rbp+offset], xmm0
void X64Assembler::movsd_mem_rbp_xmm0(int32_t offset) {
    emit8(0xF2); emit8(0x0F); emit8(0x11); emit8(0x85);  // movsd [rbp+disp32], xmm0
    emit32(offset);
}

// movsd xmm0, xmm1
void X64Assembler::movsd_xmm0_xmm1() {
    emit8(0xF2); emit8(0x0F); emit8(0x10); emit8(0xC1);  // movsd xmm0, xmm1
}

// movsd xmm1, xmm0
void X64Assembler::movsd_xmm1_xmm0() {
    emit8(0xF2); emit8(0x0F); emit8(0x10); emit8(0xC8);  // movsd xmm1, xmm0
}

// movq xmm0, rax - Move quadword from GPR to XMM
void X64Assembler::movq_xmm0_rax() {
    emit8(0x66); emit8(0x48); emit8(0x0F); emit8(0x6E); emit8(0xC0);  // movq xmm0, rax
}

// movq rax, xmm0 - Move quadword from XMM to GPR
void X64Assembler::movq_rax_xmm0() {
    emit8(0x66); emit8(0x48); emit8(0x0F); emit8(0x7E); emit8(0xC0);  // movq rax, xmm0
}

// movq xmm1, rcx
void X64Assembler::movq_xmm1_rcx() {
    emit8(0x66); emit8(0x48); emit8(0x0F); emit8(0x6E); emit8(0xC9);  // movq xmm1, rcx
}

// movq rcx, xmm1
void X64Assembler::movq_rcx_xmm1() {
    emit8(0x66); emit8(0x48); emit8(0x0F); emit8(0x7E); emit8(0xC9);  // movq rcx, xmm1
}

// addsd xmm0, xmm1
void X64Assembler::addsd_xmm0_xmm1() {
    emit8(0xF2); emit8(0x0F); emit8(0x58); emit8(0xC1);  // addsd xmm0, xmm1
}

// subsd xmm0, xmm1
void X64Assembler::subsd_xmm0_xmm1() {
    emit8(0xF2); emit8(0x0F); emit8(0x5C); emit8(0xC1);  // subsd xmm0, xmm1
}

// mulsd xmm0, xmm1
void X64Assembler::mulsd_xmm0_xmm1() {
    emit8(0xF2); emit8(0x0F); emit8(0x59); emit8(0xC1);  // mulsd xmm0, xmm1
}

// divsd xmm0, xmm1
void X64Assembler::divsd_xmm0_xmm1() {
    emit8(0xF2); emit8(0x0F); emit8(0x5E); emit8(0xC1);  // divsd xmm0, xmm1
}

// addsd xmm0, [rbp+offset]
void X64Assembler::addsd_xmm0_mem_rbp(int32_t offset) {
    emit8(0xF2); emit8(0x0F); emit8(0x58); emit8(0x85);
    emit32(offset);
}

// subsd xmm0, [rbp+offset]
void X64Assembler::subsd_xmm0_mem_rbp(int32_t offset) {
    emit8(0xF2); emit8(0x0F); emit8(0x5C); emit8(0x85);
    emit32(offset);
}

// mulsd xmm0, [rbp+offset]
void X64Assembler::mulsd_xmm0_mem_rbp(int32_t offset) {
    emit8(0xF2); emit8(0x0F); emit8(0x59); emit8(0x85);
    emit32(offset);
}

// divsd xmm0, [rbp+offset]
void X64Assembler::divsd_xmm0_mem_rbp(int32_t offset) {
    emit8(0xF2); emit8(0x0F); emit8(0x5E); emit8(0x85);
    emit32(offset);
}

// comisd xmm0, xmm1 - Compare and set EFLAGS
void X64Assembler::comisd_xmm0_xmm1() {
    emit8(0x66); emit8(0x0F); emit8(0x2F); emit8(0xC1);  // comisd xmm0, xmm1
}

// ucomisd xmm0, xmm1 - Unordered compare (handles NaN properly)
void X64Assembler::ucomisd_xmm0_xmm1() {
    emit8(0x66); emit8(0x0F); emit8(0x2E); emit8(0xC1);  // ucomisd xmm0, xmm1
}

// comisd xmm0, [rbp+offset]
void X64Assembler::comisd_xmm0_mem_rbp(int32_t offset) {
    emit8(0x66); emit8(0x0F); emit8(0x2F); emit8(0x85);
    emit32(offset);
}

// cvtsi2sd xmm0, rax - Convert int64 to double
void X64Assembler::cvtsi2sd_xmm0_rax() {
    emit8(0xF2); emit8(0x48); emit8(0x0F); emit8(0x2A); emit8(0xC0);  // cvtsi2sd xmm0, rax
}

// cvtsi2sd xmm1, rax - Convert int64 to double in xmm1
void X64Assembler::cvtsi2sd_xmm1_rax() {
    emit8(0xF2); emit8(0x48); emit8(0x0F); emit8(0x2A); emit8(0xC8);  // cvtsi2sd xmm1, rax
}

// cvtsi2sd xmm0, ecx - Convert int32 to double
void X64Assembler::cvtsi2sd_xmm0_ecx() {
    emit8(0xF2); emit8(0x0F); emit8(0x2A); emit8(0xC1);  // cvtsi2sd xmm0, ecx
}

// cvttsd2si rax, xmm0 - Convert double to int64 (truncate)
void X64Assembler::cvttsd2si_rax_xmm0() {
    emit8(0xF2); emit8(0x48); emit8(0x0F); emit8(0x2C); emit8(0xC0);  // cvttsd2si rax, xmm0
}

// cvttsd2si eax, xmm0 - Convert double to int32 (truncate)
void X64Assembler::cvttsd2si_eax_xmm0() {
    emit8(0xF2); emit8(0x0F); emit8(0x2C); emit8(0xC0);  // cvttsd2si eax, xmm0
}

// xorpd xmm0, xmm1 - XOR packed double
void X64Assembler::xorpd_xmm0_xmm1() {
    emit8(0x66); emit8(0x0F); emit8(0x57); emit8(0xC1);  // xorpd xmm0, xmm1
}

// sqrtsd xmm0, xmm0 - Square root
void X64Assembler::sqrtsd_xmm0_xmm0() {
    emit8(0xF2); emit8(0x0F); emit8(0x51); emit8(0xC0);  // sqrtsd xmm0, xmm0
}

// sqrtsd xmm0, xmm1
void X64Assembler::sqrtsd_xmm0_xmm1() {
    emit8(0xF2); emit8(0x0F); emit8(0x51); emit8(0xC1);  // sqrtsd xmm0, xmm1
}

// xorpd xmm0, xmm0 - Zero xmm0
void X64Assembler::xorpd_xmm0_xmm0() {
    emit8(0x66); emit8(0x0F); emit8(0x57); emit8(0xC0);  // xorpd xmm0, xmm0
}

// xorpd xmm1, xmm1 - Zero xmm1
void X64Assembler::xorpd_xmm1_xmm1() {
    emit8(0x66); emit8(0x0F); emit8(0x57); emit8(0xC9);  // xorpd xmm1, xmm1
}

// movss xmm0, [rbp+offset] - Load 32-bit float
void X64Assembler::movss_xmm0_mem_rbp(int32_t offset) {
    emit8(0xF3); emit8(0x0F); emit8(0x10); emit8(0x85);
    emit32(offset);
}

// movss [rbp+offset], xmm0 - Store 32-bit float
void X64Assembler::movss_mem_rbp_xmm0(int32_t offset) {
    emit8(0xF3); emit8(0x0F); emit8(0x11); emit8(0x85);
    emit32(offset);
}

// cvtss2sd xmm0, xmm0 - Convert f32 to f64
void X64Assembler::cvtss2sd_xmm0_xmm0() {
    emit8(0xF3); emit8(0x0F); emit8(0x5A); emit8(0xC0);
}

// cvtsd2ss xmm0, xmm0 - Convert f64 to f32
void X64Assembler::cvtsd2ss_xmm0_xmm0() {
    emit8(0xF2); emit8(0x0F); emit8(0x5A); emit8(0xC0);
}

// ============================================
// SSE/AVX Packed SIMD Instructions (for vectorization)
// ============================================

// Packed integer operations (SSE2) - 4 x 32-bit integers

// movdqu xmm0, [rbp+offset] - Load 128-bit unaligned
void X64Assembler::movdqu_xmm0_mem(int32_t offset) {
    emit8(0xF3); emit8(0x0F); emit8(0x6F); emit8(0x85);
    emit32(offset);
}

// movdqu [rbp+offset], xmm0 - Store 128-bit unaligned
void X64Assembler::movdqu_mem_xmm0(int32_t offset) {
    emit8(0xF3); emit8(0x0F); emit8(0x7F); emit8(0x85);
    emit32(offset);
}

// movdqa xmm0, xmm1 - Move aligned
void X64Assembler::movdqa_xmm0_xmm1() {
    emit8(0x66); emit8(0x0F); emit8(0x6F); emit8(0xC1);
}

// movdqa xmm1, xmm0
void X64Assembler::movdqa_xmm1_xmm0() {
    emit8(0x66); emit8(0x0F); emit8(0x6F); emit8(0xC8);
}

// paddd xmm0, xmm1 - Packed add 4 x int32
void X64Assembler::paddd_xmm0_xmm1() {
    emit8(0x66); emit8(0x0F); emit8(0xFE); emit8(0xC1);
}

// paddd xmm0, [rbp+offset]
void X64Assembler::paddd_xmm0_mem(int32_t offset) {
    emit8(0x66); emit8(0x0F); emit8(0xFE); emit8(0x85);
    emit32(offset);
}

// psubd xmm0, xmm1 - Packed subtract 4 x int32
void X64Assembler::psubd_xmm0_xmm1() {
    emit8(0x66); emit8(0x0F); emit8(0xFA); emit8(0xC1);
}

// pmulld xmm0, xmm1 - Packed multiply 4 x int32 (SSE4.1)
void X64Assembler::pmulld_xmm0_xmm1() {
    emit8(0x66); emit8(0x0F); emit8(0x38); emit8(0x40); emit8(0xC1);
}

// paddq xmm0, xmm1 - Packed add 2 x int64
void X64Assembler::paddq_xmm0_xmm1() {
    emit8(0x66); emit8(0x0F); emit8(0xD4); emit8(0xC1);
}

// psubq xmm0, xmm1 - Packed subtract 2 x int64
void X64Assembler::psubq_xmm0_xmm1() {
    emit8(0x66); emit8(0x0F); emit8(0xFB); emit8(0xC1);
}

// Packed double operations (SSE2) - 2 x 64-bit doubles

// movupd xmm0, [rbp+offset] - Load 2 doubles unaligned
void X64Assembler::movupd_xmm0_mem(int32_t offset) {
    emit8(0x66); emit8(0x0F); emit8(0x10); emit8(0x85);
    emit32(offset);
}

// movupd [rbp+offset], xmm0 - Store 2 doubles unaligned
void X64Assembler::movupd_mem_xmm0(int32_t offset) {
    emit8(0x66); emit8(0x0F); emit8(0x11); emit8(0x85);
    emit32(offset);
}

// movapd xmm0, xmm1
void X64Assembler::movapd_xmm0_xmm1() {
    emit8(0x66); emit8(0x0F); emit8(0x28); emit8(0xC1);
}

// movapd xmm1, xmm0
void X64Assembler::movapd_xmm1_xmm0() {
    emit8(0x66); emit8(0x0F); emit8(0x28); emit8(0xC8);
}

// addpd xmm0, xmm1 - Packed add 2 x double
void X64Assembler::addpd_xmm0_xmm1() {
    emit8(0x66); emit8(0x0F); emit8(0x58); emit8(0xC1);
}

// addpd xmm0, [rbp+offset]
void X64Assembler::addpd_xmm0_mem(int32_t offset) {
    emit8(0x66); emit8(0x0F); emit8(0x58); emit8(0x85);
    emit32(offset);
}

// subpd xmm0, xmm1 - Packed subtract 2 x double
void X64Assembler::subpd_xmm0_xmm1() {
    emit8(0x66); emit8(0x0F); emit8(0x5C); emit8(0xC1);
}

// mulpd xmm0, xmm1 - Packed multiply 2 x double
void X64Assembler::mulpd_xmm0_xmm1() {
    emit8(0x66); emit8(0x0F); emit8(0x59); emit8(0xC1);
}

// divpd xmm0, xmm1 - Packed divide 2 x double
void X64Assembler::divpd_xmm0_xmm1() {
    emit8(0x66); emit8(0x0F); emit8(0x5E); emit8(0xC1);
}

// Packed float operations (SSE) - 4 x 32-bit floats

// movups xmm0, [rbp+offset] - Load 4 floats unaligned
void X64Assembler::movups_xmm0_mem(int32_t offset) {
    emit8(0x0F); emit8(0x10); emit8(0x85);
    emit32(offset);
}

// movups [rbp+offset], xmm0 - Store 4 floats unaligned
void X64Assembler::movups_mem_xmm0(int32_t offset) {
    emit8(0x0F); emit8(0x11); emit8(0x85);
    emit32(offset);
}

// addps xmm0, xmm1 - Packed add 4 x float
void X64Assembler::addps_xmm0_xmm1() {
    emit8(0x0F); emit8(0x58); emit8(0xC1);
}

// addps xmm0, [rbp+offset]
void X64Assembler::addps_xmm0_mem(int32_t offset) {
    emit8(0x0F); emit8(0x58); emit8(0x85);
    emit32(offset);
}

// subps xmm0, xmm1 - Packed subtract 4 x float
void X64Assembler::subps_xmm0_xmm1() {
    emit8(0x0F); emit8(0x5C); emit8(0xC1);
}

// mulps xmm0, xmm1 - Packed multiply 4 x float
void X64Assembler::mulps_xmm0_xmm1() {
    emit8(0x0F); emit8(0x59); emit8(0xC1);
}

// divps xmm0, xmm1 - Packed divide 4 x float
void X64Assembler::divps_xmm0_xmm1() {
    emit8(0x0F); emit8(0x5E); emit8(0xC1);
}

// Horizontal operations (for reductions)

// haddpd xmm0, xmm0 - Horizontal add doubles (SSE3)
void X64Assembler::haddpd_xmm0_xmm0() {
    emit8(0x66); emit8(0x0F); emit8(0x7C); emit8(0xC0);
}

// haddps xmm0, xmm0 - Horizontal add floats (SSE3)
void X64Assembler::haddps_xmm0_xmm0() {
    emit8(0xF2); emit8(0x0F); emit8(0x7C); emit8(0xC0);
}

// phaddd xmm0, xmm0 - Horizontal add int32 (SSSE3)
void X64Assembler::phaddd_xmm0_xmm0() {
    emit8(0x66); emit8(0x0F); emit8(0x38); emit8(0x02); emit8(0xC0);
}

// Shuffle and permute

// pshufd xmm0, xmm0, imm8 - Shuffle int32
void X64Assembler::pshufd_xmm0_xmm0_imm8(uint8_t imm) {
    emit8(0x66); emit8(0x0F); emit8(0x70); emit8(0xC0); emit8(imm);
}

// shufpd xmm0, xmm1, imm8 - Shuffle doubles
void X64Assembler::shufpd_xmm0_xmm1_imm8(uint8_t imm) {
    emit8(0x66); emit8(0x0F); emit8(0xC6); emit8(0xC1); emit8(imm);
}

// movddup xmm0, [rbp+offset] - Broadcast double to both lanes (SSE3)
void X64Assembler::movddup_xmm0_mem(int32_t offset) {
    emit8(0xF2); emit8(0x0F); emit8(0x12); emit8(0x85);
    emit32(offset);
}

// pshufd xmm0, [rbp+offset], 0x00 - Broadcast int32 to all 4 lanes
void X64Assembler::pshufd_broadcast_xmm0(int32_t offset) {
    emit8(0x66); emit8(0x0F); emit8(0x70); emit8(0x85);
    emit32(offset);
    emit8(0x00);  // Broadcast lowest element to all
}

// pextrd eax, xmm0, imm8 - Extract int32 from xmm0 (SSE4.1)
void X64Assembler::pextrd_eax_xmm0_imm8(uint8_t idx) {
    emit8(0x66); emit8(0x0F); emit8(0x3A); emit8(0x16); emit8(0xC0); emit8(idx);
}

// extractps eax, xmm0, imm8 - Extract float from xmm0 (SSE4.1)
void X64Assembler::extractps_eax_xmm0_imm8(uint8_t idx) {
    emit8(0x66); emit8(0x0F); emit8(0x3A); emit8(0x17); emit8(0xC0); emit8(idx);
}

// pxor xmm0, xmm0 - Zero xmm0
void X64Assembler::pxor_xmm0_xmm0() {
    emit8(0x66); emit8(0x0F); emit8(0xEF); emit8(0xC0);
}

// pxor xmm1, xmm1 - Zero xmm1
void X64Assembler::pxor_xmm1_xmm1() {
    emit8(0x66); emit8(0x0F); emit8(0xEF); emit8(0xC9);
}

// ============================================
// Advanced Instruction Selection Optimizations
// ============================================

// LEA for complex address calculations

// lea rax, [rax + rcx]
void X64Assembler::lea_rax_rax_rcx() {
    emit8(0x48); emit8(0x8D); emit8(0x04); emit8(0x08);  // lea rax, [rax + rcx*1]
}

// lea rax, [rax + rcx*2]
void X64Assembler::lea_rax_rax_rcx_2() {
    emit8(0x48); emit8(0x8D); emit8(0x04); emit8(0x48);  // lea rax, [rax + rcx*2]
}

// lea rax, [rax + rcx*4]
void X64Assembler::lea_rax_rax_rcx_4() {
    emit8(0x48); emit8(0x8D); emit8(0x04); emit8(0x88);  // lea rax, [rax + rcx*4]
}

// lea rax, [rax + rcx*8]
void X64Assembler::lea_rax_rax_rcx_8() {
    emit8(0x48); emit8(0x8D); emit8(0x04); emit8(0xC8);  // lea rax, [rax + rcx*8]
}

// lea rax, [rcx + disp32]
void X64Assembler::lea_rax_rcx_imm32(int32_t disp) {
    emit8(0x48); emit8(0x8D); emit8(0x81);  // lea rax, [rcx + disp32]
    emit32(disp);
}

// lea rax, [rax + disp32]
void X64Assembler::lea_rax_rax_imm32(int32_t disp) {
    emit8(0x48); emit8(0x8D); emit8(0x80);  // lea rax, [rax + disp32]
    emit32(disp);
}

// lea rax, [rcx + rcx] (multiply by 2)
void X64Assembler::lea_rax_rcx_rcx() {
    emit8(0x48); emit8(0x8D); emit8(0x04); emit8(0x09);  // lea rax, [rcx + rcx*1]
}

// lea rax, [rcx + rcx*2] (multiply by 3)
void X64Assembler::lea_rax_rcx_rcx_2() {
    emit8(0x48); emit8(0x8D); emit8(0x04); emit8(0x49);  // lea rax, [rcx + rcx*2]
}

// lea rax, [rcx*4]
void X64Assembler::lea_rax_rcx_4() {
    emit8(0x48); emit8(0x8D); emit8(0x04); emit8(0x8D);  // lea rax, [rcx*4 + 0]
    emit32(0);
}

// lea rax, [rcx*8]
void X64Assembler::lea_rax_rcx_8() {
    emit8(0x48); emit8(0x8D); emit8(0x04); emit8(0xCD);  // lea rax, [rcx*8 + 0]
    emit32(0);
}

// Conditional moves (branchless conditionals)

// cmove rax, rcx - if ZF: rax = rcx
void X64Assembler::cmove_rax_rcx() {
    emit8(0x48); emit8(0x0F); emit8(0x44); emit8(0xC1);
}

// cmovne rax, rcx - if !ZF: rax = rcx
void X64Assembler::cmovne_rax_rcx() {
    emit8(0x48); emit8(0x0F); emit8(0x45); emit8(0xC1);
}

// cmovl rax, rcx - if SF!=OF: rax = rcx (signed less)
void X64Assembler::cmovl_rax_rcx() {
    emit8(0x48); emit8(0x0F); emit8(0x4C); emit8(0xC1);
}

// cmovg rax, rcx - if ZF=0 && SF=OF: rax = rcx (signed greater)
void X64Assembler::cmovg_rax_rcx() {
    emit8(0x48); emit8(0x0F); emit8(0x4F); emit8(0xC1);
}

// cmovle rax, rcx - if ZF=1 || SF!=OF: rax = rcx
void X64Assembler::cmovle_rax_rcx() {
    emit8(0x48); emit8(0x0F); emit8(0x4E); emit8(0xC1);
}

// cmovge rax, rcx - if SF=OF: rax = rcx
void X64Assembler::cmovge_rax_rcx() {
    emit8(0x48); emit8(0x0F); emit8(0x4D); emit8(0xC1);
}

// cmova rax, rcx - if CF=0 && ZF=0: rax = rcx (unsigned above)
void X64Assembler::cmova_rax_rcx() {
    emit8(0x48); emit8(0x0F); emit8(0x47); emit8(0xC1);
}

// cmovb rax, rcx - if CF=1: rax = rcx (unsigned below)
void X64Assembler::cmovb_rax_rcx() {
    emit8(0x48); emit8(0x0F); emit8(0x42); emit8(0xC1);
}

// cmovae rax, rcx - if CF=0: rax = rcx
void X64Assembler::cmovae_rax_rcx() {
    emit8(0x48); emit8(0x0F); emit8(0x43); emit8(0xC1);
}

// cmovbe rax, rcx - if CF=1 || ZF=1: rax = rcx
void X64Assembler::cmovbe_rax_rcx() {
    emit8(0x48); emit8(0x0F); emit8(0x46); emit8(0xC1);
}

// Shift instructions for strength reduction

// shl rax, imm8
void X64Assembler::shl_rax_imm8(uint8_t count) {
    emit8(0x48); emit8(0xC1); emit8(0xE0); emit8(count);
}

// shr rax, imm8 (logical)
void X64Assembler::shr_rax_imm8(uint8_t count) {
    emit8(0x48); emit8(0xC1); emit8(0xE8); emit8(count);
}

// sar rax, imm8 (arithmetic)
void X64Assembler::sar_rax_imm8(uint8_t count) {
    emit8(0x48); emit8(0xC1); emit8(0xF8); emit8(count);
}

// shl rax, cl
void X64Assembler::shl_rax_cl() {
    emit8(0x48); emit8(0xD3); emit8(0xE0);
}

// shr rax, cl
void X64Assembler::shr_rax_cl() {
    emit8(0x48); emit8(0xD3); emit8(0xE8);
}

// sar rax, cl
void X64Assembler::sar_rax_cl() {
    emit8(0x48); emit8(0xD3); emit8(0xF8);
}

// Bit manipulation

// bsr rax, rcx - Bit scan reverse
void X64Assembler::bsr_rax_rcx() {
    emit8(0x48); emit8(0x0F); emit8(0xBD); emit8(0xC1);
}

// bsf rax, rcx - Bit scan forward
void X64Assembler::bsf_rax_rcx() {
    emit8(0x48); emit8(0x0F); emit8(0xBC); emit8(0xC1);
}

// popcnt rax, rcx - Population count
void X64Assembler::popcnt_rax_rcx() {
    emit8(0xF3); emit8(0x48); emit8(0x0F); emit8(0xB8); emit8(0xC1);
}

// lzcnt rax, rcx - Leading zero count
void X64Assembler::lzcnt_rax_rcx() {
    emit8(0xF3); emit8(0x48); emit8(0x0F); emit8(0xBD); emit8(0xC1);
}

// tzcnt rax, rcx - Trailing zero count
void X64Assembler::tzcnt_rax_rcx() {
    emit8(0xF3); emit8(0x48); emit8(0x0F); emit8(0xBC); emit8(0xC1);
}

// Additional arithmetic

// add rax, imm32
void X64Assembler::add_rax_imm32(int32_t val) {
    emit8(0x48); emit8(0x05); emit32(val);
}

// sub rax, imm32
void X64Assembler::sub_rax_imm32(int32_t val) {
    emit8(0x48); emit8(0x2D); emit32(val);
}

// imul rax, rcx, imm32
void X64Assembler::imul_rax_rcx_imm32(int32_t val) {
    emit8(0x48); emit8(0x69); emit8(0xC1); emit32(val);
}

// imul rax, rax, imm32
void X64Assembler::imul_rax_rax_imm32(int32_t val) {
    emit8(0x48); emit8(0x69); emit8(0xC0); emit32(val);
}

// Test with immediate

// test rax, imm32
void X64Assembler::test_rax_imm32(int32_t val) {
    emit8(0x48); emit8(0xA9); emit32(val);
}

// test al, imm8
void X64Assembler::test_al_imm8(uint8_t val) {
    emit8(0xA8); emit8(val);
}

// Additional moves

// mov rax, rdx
void X64Assembler::mov_rax_rdx() {
    emit8(0x48); emit8(0x89); emit8(0xD0);
}

// mov rdx, rcx
void X64Assembler::mov_rdx_rcx() {
    emit8(0x48); emit8(0x89); emit8(0xCA);
}

// mov rcx, rdx
void X64Assembler::mov_rcx_rdx() {
    emit8(0x48); emit8(0x89); emit8(0xD1);
}

// mov rdi, rax
void X64Assembler::mov_rdi_rax() {
    emit8(0x48); emit8(0x89); emit8(0xC7);
}

// mov rax, rdi
void X64Assembler::mov_rax_rdi() {
    emit8(0x48); emit8(0x89); emit8(0xF8);
}

// mov rcx, rdi
void X64Assembler::mov_rcx_rdi() {
    emit8(0x48); emit8(0x89); emit8(0xF9);
}

// mov [rdi], rax
void X64Assembler::mov_mem_rdi_rax() {
    emit8(0x48); emit8(0x89); emit8(0x07);
}

// mov rsi, rax
void X64Assembler::mov_rsi_rax() {
    emit8(0x48); emit8(0x89); emit8(0xC6);
}

// mov rsi, rcx
void X64Assembler::mov_rsi_rcx() {
    emit8(0x48); emit8(0x89); emit8(0xCE);
}

// mov rsi, [rbp + offset]
void X64Assembler::mov_rsi_mem_rbp(int32_t offset) {
    emit8(0x48); emit8(0x8B); emit8(0xB5); emit32(offset);
}

// mov rsi, [rax + offset]
void X64Assembler::mov_rsi_mem_rax(int32_t offset) {
    emit8(0x48); emit8(0x8B); emit8(0xB0); emit32(offset);
}

// mov rdi, [rbp + offset]
void X64Assembler::mov_rdi_mem_rbp(int32_t offset) {
    emit8(0x48); emit8(0x8B); emit8(0xBD); emit32(offset);
}

// add rcx, imm32
void X64Assembler::add_rcx_imm32(int32_t val) {
    emit8(0x48); emit8(0x81); emit8(0xC1); emit32(val);
}

// ============================================
// Channel-related instructions
// ============================================

// mov rax, [rsp + offset]
void X64Assembler::mov_rax_mem_rsp(int32_t offset) {
    emit8(0x48); emit8(0x8B); emit8(0x84); emit8(0x24); emit32(offset);
}

// mov rcx, [rax + offset]
void X64Assembler::mov_rcx_mem_rax(int32_t offset) {
    emit8(0x48); emit8(0x8B); emit8(0x88); emit32(offset);
}

// mov rdx, [rax + offset]
void X64Assembler::mov_rdx_mem_rax(int32_t offset) {
    emit8(0x48); emit8(0x8B); emit8(0x90); emit32(offset);
}

// mov r8, [rax + offset]
void X64Assembler::mov_r8_mem_rax(int32_t offset) {
    emit8(0x4C); emit8(0x8B); emit8(0x80); emit32(offset);
}

// mov r9, [rcx + offset]
void X64Assembler::mov_r9_mem_rcx(int32_t offset) {
    emit8(0x4C); emit8(0x8B); emit8(0x89); emit32(offset);
}

// mov [rax + offset], rcx
void X64Assembler::mov_mem_rax_rcx(int32_t offset) {
    emit8(0x48); emit8(0x89); emit8(0x88); emit32(offset);
}

// mov [rax + offset], rdx
void X64Assembler::mov_mem_rax_rdx(int32_t offset) {
    emit8(0x48); emit8(0x89); emit8(0x90); emit32(offset);
}

// mov [rcx + offset], rax
void X64Assembler::mov_mem_rcx_rax(int32_t offset) {
    emit8(0x48); emit8(0x89); emit8(0x81); emit32(offset);
}

// push r9
void X64Assembler::push_r9() {
    emit8(0x41); emit8(0x51);
}

// dec rcx
void X64Assembler::dec_rcx() {
    emit8(0x48); emit8(0xFF); emit8(0xC9);
}

// test rcx, rcx
void X64Assembler::test_rcx_rcx() {
    emit8(0x48); emit8(0x85); emit8(0xC9);
}

// xor rcx, rcx
void X64Assembler::xor_rcx_rcx() {
    emit8(0x48); emit8(0x31); emit8(0xC9);
}

// xor rdx, rdx
void X64Assembler::xor_rdx_rdx() {
    emit8(0x48); emit8(0x31); emit8(0xD2);
}

// xor r8, r8
void X64Assembler::xor_r8_r8() {
    emit8(0x4D); emit8(0x31); emit8(0xC0);
}

// xor r9, r9
void X64Assembler::xor_r9_r9() {
    emit8(0x4D); emit8(0x31); emit8(0xC9);
}

// div rdx - DEPRECATED, use idiv_rcx instead
// This was incorrectly implemented - div rdx divides RDX:RAX by RDX which is invalid
void X64Assembler::div_rdx() {
    // Use div rcx instead - divides RDX:RAX by RCX
    // Result: RAX = quotient, RDX = remainder
    emit8(0x48); emit8(0xF7); emit8(0xF1);  // div rcx
}

// imul rdx, r8 (rdx = rdx * r8)
void X64Assembler::imul_rdx_r8() {
    emit8(0x49); emit8(0x0F); emit8(0xAF); emit8(0xD0);  // imul rdx, r8
}

// add rcx, rdx
void X64Assembler::add_rcx_rdx() {
    emit8(0x48); emit8(0x01); emit8(0xD1);  // add rcx, rdx
}

// cmp rcx, rdx
void X64Assembler::cmp_rcx_rdx() {
    emit8(0x48); emit8(0x39); emit8(0xD1);  // cmp rcx, rdx
}

// lea rcx, [rax + offset]
void X64Assembler::lea_rcx_rax_offset(int32_t offset) {
    emit8(0x48); emit8(0x8D); emit8(0x88); emit32(offset);  // lea rcx, [rax + disp32]
}

// xchg rax, rcx
void X64Assembler::xchg_rax_rcx() {
    emit8(0x48); emit8(0x91);  // xchg rax, rcx
}

} // namespace tyl
