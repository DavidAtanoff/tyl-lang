// Tyl Compiler - x86-64 Assembler Callee-Saved Register Operations
// Handles: RBX, R12, R13, R14, R15 operations

#include "x64_assembler.h"

namespace tyl {

// RBX operations
void X64Assembler::push_rbx() { emit8(0x53); }
void X64Assembler::pop_rbx() { emit8(0x5B); }
void X64Assembler::mov_rbx_rax() { emit8(0x48); emit8(0x89); emit8(0xC3); }
void X64Assembler::mov_rax_rbx() { emit8(0x48); emit8(0x89); emit8(0xD8); }
void X64Assembler::mov_rbx_rcx() { emit8(0x48); emit8(0x89); emit8(0xCB); }
void X64Assembler::mov_rbx_rdx() { emit8(0x48); emit8(0x89); emit8(0xD3); }
void X64Assembler::mov_rbx_r8() { emit8(0x4C); emit8(0x89); emit8(0xC3); }
void X64Assembler::mov_rbx_r9() { emit8(0x4C); emit8(0x89); emit8(0xCB); }
void X64Assembler::mov_rbx_imm64(int64_t val) { emit8(0x48); emit8(0xBB); emit64(val); }
void X64Assembler::mov_rbx_imm32(int32_t val) { emit8(0xBB); emit32(val); }
void X64Assembler::xor_rbx_rbx() { emit8(0x48); emit8(0x31); emit8(0xDB); }

// R12 operations
void X64Assembler::push_r12() { emit8(0x41); emit8(0x54); }
void X64Assembler::pop_r12() { emit8(0x41); emit8(0x5C); }
void X64Assembler::mov_r12_rax() { emit8(0x49); emit8(0x89); emit8(0xC4); }
void X64Assembler::mov_rax_r12() { emit8(0x4C); emit8(0x89); emit8(0xE0); }
void X64Assembler::mov_r12_rcx() { emit8(0x49); emit8(0x89); emit8(0xCC); }
void X64Assembler::mov_r12_rdx() { emit8(0x49); emit8(0x89); emit8(0xD4); }
void X64Assembler::mov_r12_r8() { emit8(0x4D); emit8(0x89); emit8(0xC4); }
void X64Assembler::mov_r12_r9() { emit8(0x4D); emit8(0x89); emit8(0xCC); }
void X64Assembler::mov_r12_imm64(int64_t val) { emit8(0x49); emit8(0xBC); emit64(val); }
void X64Assembler::xor_r12_r12() { emit8(0x4D); emit8(0x31); emit8(0xE4); }

// R13 operations
void X64Assembler::push_r13() { emit8(0x41); emit8(0x55); }
void X64Assembler::pop_r13() { emit8(0x41); emit8(0x5D); }
void X64Assembler::mov_r13_rax() { emit8(0x49); emit8(0x89); emit8(0xC5); }
void X64Assembler::mov_rax_r13() { emit8(0x4C); emit8(0x89); emit8(0xE8); }
void X64Assembler::mov_r13_rcx() { emit8(0x49); emit8(0x89); emit8(0xCD); }
void X64Assembler::mov_r13_rdx() { emit8(0x49); emit8(0x89); emit8(0xD5); }
void X64Assembler::mov_r13_r8() { emit8(0x4D); emit8(0x89); emit8(0xC5); }
void X64Assembler::mov_r13_r9() { emit8(0x4D); emit8(0x89); emit8(0xCD); }
void X64Assembler::mov_r13_imm64(int64_t val) { emit8(0x49); emit8(0xBD); emit64(val); }
void X64Assembler::xor_r13_r13() { emit8(0x4D); emit8(0x31); emit8(0xED); }

// R14 operations
void X64Assembler::push_r14() { emit8(0x41); emit8(0x56); }
void X64Assembler::pop_r14() { emit8(0x41); emit8(0x5E); }
void X64Assembler::mov_r14_rax() { emit8(0x49); emit8(0x89); emit8(0xC6); }
void X64Assembler::mov_rax_r14() { emit8(0x4C); emit8(0x89); emit8(0xF0); }
void X64Assembler::mov_r14_rcx() { emit8(0x49); emit8(0x89); emit8(0xCE); }
void X64Assembler::mov_r14_rdx() { emit8(0x49); emit8(0x89); emit8(0xD6); }
void X64Assembler::mov_r14_r8() { emit8(0x4D); emit8(0x89); emit8(0xC6); }
void X64Assembler::mov_r14_r9() { emit8(0x4D); emit8(0x89); emit8(0xCE); }
void X64Assembler::mov_r14_imm64(int64_t val) { emit8(0x49); emit8(0xBE); emit64(val); }
void X64Assembler::xor_r14_r14() { emit8(0x4D); emit8(0x31); emit8(0xF6); }

// R15 operations
void X64Assembler::push_r15() { emit8(0x41); emit8(0x57); }
void X64Assembler::pop_r15() { emit8(0x41); emit8(0x5F); }
void X64Assembler::mov_r15_rax() { emit8(0x49); emit8(0x89); emit8(0xC7); }
void X64Assembler::mov_rax_r15() { emit8(0x4C); emit8(0x89); emit8(0xF8); }
void X64Assembler::mov_r15_rcx() { emit8(0x49); emit8(0x89); emit8(0xCF); }
void X64Assembler::mov_r15_rdx() { emit8(0x49); emit8(0x89); emit8(0xD7); }
void X64Assembler::mov_r15_r8() { emit8(0x4D); emit8(0x89); emit8(0xC7); }
void X64Assembler::mov_r15_r9() { emit8(0x4D); emit8(0x89); emit8(0xCF); }
void X64Assembler::mov_r15_imm64(int64_t val) { emit8(0x49); emit8(0xBF); emit64(val); }
void X64Assembler::xor_r15_r15() { emit8(0x4D); emit8(0x31); emit8(0xFF); }

// Move from callee-saved to RCX (for closure captures)
void X64Assembler::mov_rcx_rbx() { emit8(0x48); emit8(0x89); emit8(0xD9); }
void X64Assembler::mov_rcx_r12() { emit8(0x4C); emit8(0x89); emit8(0xE1); }
void X64Assembler::mov_rcx_r13() { emit8(0x4C); emit8(0x89); emit8(0xE9); }
void X64Assembler::mov_rcx_r14() { emit8(0x4C); emit8(0x89); emit8(0xF1); }
void X64Assembler::mov_rcx_r15() { emit8(0x4C); emit8(0x89); emit8(0xF9); }

} // namespace tyl
