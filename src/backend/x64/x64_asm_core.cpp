// Tyl Compiler - x86-64 Assembler Core Instructions
// Handles: basic mov, push, pop, arithmetic, comparison

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

// Basic MOV instructions
void X64Assembler::mov_rax_imm64(int64_t val) { emit8(0x48); emit8(0xB8); emit64(val); }
void X64Assembler::mov_rcx_imm64(int64_t val) { emit8(0x48); emit8(0xB9); emit64(val); }
void X64Assembler::mov_rdx_imm64(int64_t val) { emit8(0x48); emit8(0xBA); emit64(val); }
void X64Assembler::mov_r8_imm64(int64_t val) { emit8(0x49); emit8(0xB8); emit64(val); }
void X64Assembler::mov_r8d_imm32(int32_t val) { emit8(0x41); emit8(0xB8); emit32(val); }
void X64Assembler::mov_rax_imm32(int32_t val) { emit8(0x48); emit8(0xC7); emit8(0xC0); emit32(val); }
void X64Assembler::mov_ecx_imm32(int32_t val) { emit8(0xB9); emit32(val); }
void X64Assembler::mov_edx_imm32(int32_t val) { emit8(0xBA); emit32(val); }

// Register-to-register MOV
void X64Assembler::mov_rax_rcx() { emit8(0x48); emit8(0x89); emit8(0xC8); }
void X64Assembler::mov_rcx_rax() { emit8(0x48); emit8(0x89); emit8(0xC1); }
void X64Assembler::mov_rdx_rax() { emit8(0x48); emit8(0x89); emit8(0xC2); }
void X64Assembler::mov_r8_rax() { emit8(0x49); emit8(0x89); emit8(0xC0); }
void X64Assembler::mov_r8_rcx() { emit8(0x49); emit8(0x89); emit8(0xC8); }

// Memory operations with RBP
void X64Assembler::mov_rax_mem_rbp(int32_t offset) { emit8(0x48); emit8(0x8B); emit8(0x85); emit32(offset); }
void X64Assembler::mov_rcx_mem_rbp(int32_t offset) { emit8(0x48); emit8(0x8B); emit8(0x8D); emit32(offset); }
void X64Assembler::mov_rdx_mem_rbp(int32_t offset) { emit8(0x48); emit8(0x8B); emit8(0x95); emit32(offset); }
void X64Assembler::mov_mem_rbp_rax(int32_t offset) { emit8(0x48); emit8(0x89); emit8(0x85); emit32(offset); }
void X64Assembler::mov_mem_rbp_rcx(int32_t offset) { emit8(0x48); emit8(0x89); emit8(0x8D); emit32(offset); }
void X64Assembler::mov_mem_rbp_rdx(int32_t offset) { emit8(0x48); emit8(0x89); emit8(0x95); emit32(offset); }

// Memory operations with registers
void X64Assembler::mov_rax_mem_rax() { emit8(0x48); emit8(0x8B); emit8(0x00); }
void X64Assembler::mov_mem_rcx_rax() { emit8(0x48); emit8(0x89); emit8(0x01); }
void X64Assembler::mov_mem_rax_rcx() { emit8(0x48); emit8(0x89); emit8(0x08); }
void X64Assembler::mov_rcx_mem_rax() { emit8(0x48); emit8(0x8B); emit8(0x08); }
void X64Assembler::mov_rdx_mem_rax() { emit8(0x48); emit8(0x8B); emit8(0x10); }  // rdx = [rax]
void X64Assembler::mov_rax_mem_rcx() { emit8(0x48); emit8(0x8B); emit8(0x01); }

// LEA instructions
void X64Assembler::lea_rcx_rip_fixup(uint32_t targetRVA) { emit8(0x48); emit8(0x8D); emit8(0x0D); fixupRIP(targetRVA); }
void X64Assembler::lea_rax_rip_fixup(uint32_t targetRVA) { emit8(0x48); emit8(0x8D); emit8(0x05); fixupRIP(targetRVA); }
void X64Assembler::lea_rax_rbp(int32_t offset) { emit8(0x48); emit8(0x8D); emit8(0x85); emit32(offset); }
void X64Assembler::lea_rcx_rbp(int32_t offset) { emit8(0x48); emit8(0x8D); emit8(0x8D); emit32(offset); }
void X64Assembler::lea_rdx_rbp_offset(int32_t offset) { emit8(0x48); emit8(0x8D); emit8(0x95); emit32(offset); }

// Stack operations
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

// Frame setup
void X64Assembler::mov_rbp_rsp() { emit8(0x48); emit8(0x89); emit8(0xE5); }
void X64Assembler::mov_rsp_rbp() { emit8(0x48); emit8(0x89); emit8(0xEC); }
void X64Assembler::sub_rsp_imm32(int32_t val) { emit8(0x48); emit8(0x81); emit8(0xEC); emit32(val); }
void X64Assembler::add_rsp_imm32(int32_t val) { emit8(0x48); emit8(0x81); emit8(0xC4); emit32(val); }

// Arithmetic
void X64Assembler::add_rax_rcx() { emit8(0x48); emit8(0x01); emit8(0xC8); }
void X64Assembler::sub_rax_rcx() { emit8(0x48); emit8(0x29); emit8(0xC8); }
void X64Assembler::imul_rax_rcx() { emit8(0x48); emit8(0x0F); emit8(0xAF); emit8(0xC1); }
void X64Assembler::cqo() { emit8(0x48); emit8(0x99); }
void X64Assembler::idiv_rcx() { emit8(0x48); emit8(0xF7); emit8(0xF9); }
void X64Assembler::neg_rax() { emit8(0x48); emit8(0xF7); emit8(0xD8); }
void X64Assembler::inc_rax() { emit8(0x48); emit8(0xFF); emit8(0xC0); }
void X64Assembler::inc_rcx() { emit8(0x48); emit8(0xFF); emit8(0xC1); }
void X64Assembler::dec_rax() { emit8(0x48); emit8(0xFF); emit8(0xC8); }

// Comparison
void X64Assembler::cmp_rax_rcx() { emit8(0x48); emit8(0x39); emit8(0xC8); }
void X64Assembler::cmp_rax_imm32(int32_t val) { emit8(0x48); emit8(0x3D); emit32(val); }
void X64Assembler::cmp_rax_mem_rbp(int32_t offset) { emit8(0x48); emit8(0x3B); emit8(0x85); emit32(offset); }
void X64Assembler::test_rax_rax() { emit8(0x48); emit8(0x85); emit8(0xC0); }

// Set byte on condition
void X64Assembler::sete_al() { emit8(0x0F); emit8(0x94); emit8(0xC0); }
void X64Assembler::setne_al() { emit8(0x0F); emit8(0x95); emit8(0xC0); }
void X64Assembler::setl_al() { emit8(0x0F); emit8(0x9C); emit8(0xC0); }
void X64Assembler::setg_al() { emit8(0x0F); emit8(0x9F); emit8(0xC0); }
void X64Assembler::setle_al() { emit8(0x0F); emit8(0x9E); emit8(0xC0); }
void X64Assembler::setge_al() { emit8(0x0F); emit8(0x9D); emit8(0xC0); }
void X64Assembler::movzx_rax_al() { emit8(0x48); emit8(0x0F); emit8(0xB6); emit8(0xC0); }

// Logical
void X64Assembler::xor_rax_rax() { emit8(0x48); emit8(0x31); emit8(0xC0); }
void X64Assembler::xor_ecx_ecx() { emit8(0x31); emit8(0xC9); }
void X64Assembler::and_rax_rcx() { emit8(0x48); emit8(0x21); emit8(0xC8); }
void X64Assembler::or_rax_rcx() { emit8(0x48); emit8(0x09); emit8(0xC8); }

// Control flow
void X64Assembler::jmp_rel32(const std::string& lbl) { emit8(0xE9); fixupLabel(lbl); }
void X64Assembler::jz_rel32(const std::string& lbl) { emit8(0x0F); emit8(0x84); fixupLabel(lbl); }
void X64Assembler::jnz_rel32(const std::string& lbl) { emit8(0x0F); emit8(0x85); fixupLabel(lbl); }
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

} // namespace tyl
