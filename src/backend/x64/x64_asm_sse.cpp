// Tyl Compiler - x86-64 Assembler SSE/SSE2 Instructions
// Handles: floating point operations, SIMD

#include "x64_assembler.h"

namespace tyl {

// ============================================
// SSE/SSE2 Floating Point Instructions
// ============================================

void X64Assembler::movsd_xmm0_mem_rip(uint32_t targetRVA) {
    emit8(0xF2); emit8(0x0F); emit8(0x10); emit8(0x05);
    fixupRIP(targetRVA);
}

void X64Assembler::movsd_xmm0_mem_rbp(int32_t offset) {
    emit8(0xF2); emit8(0x0F); emit8(0x10); emit8(0x85);
    emit32(offset);
}

void X64Assembler::movsd_xmm1_mem_rbp(int32_t offset) {
    emit8(0xF2); emit8(0x0F); emit8(0x10); emit8(0x8D);
    emit32(offset);
}

void X64Assembler::movsd_mem_rbp_xmm0(int32_t offset) {
    emit8(0xF2); emit8(0x0F); emit8(0x11); emit8(0x85);
    emit32(offset);
}

void X64Assembler::movsd_xmm0_xmm1() {
    emit8(0xF2); emit8(0x0F); emit8(0x10); emit8(0xC1);
}

void X64Assembler::movsd_xmm1_xmm0() {
    emit8(0xF2); emit8(0x0F); emit8(0x10); emit8(0xC8);
}

void X64Assembler::movq_xmm0_rax() {
    emit8(0x66); emit8(0x48); emit8(0x0F); emit8(0x6E); emit8(0xC0);
}

void X64Assembler::movq_rax_xmm0() {
    emit8(0x66); emit8(0x48); emit8(0x0F); emit8(0x7E); emit8(0xC0);
}

void X64Assembler::movq_xmm1_rcx() {
    emit8(0x66); emit8(0x48); emit8(0x0F); emit8(0x6E); emit8(0xC9);
}

void X64Assembler::movq_rcx_xmm1() {
    emit8(0x66); emit8(0x48); emit8(0x0F); emit8(0x7E); emit8(0xC9);
}

// Scalar double arithmetic
void X64Assembler::addsd_xmm0_xmm1() {
    emit8(0xF2); emit8(0x0F); emit8(0x58); emit8(0xC1);
}

void X64Assembler::subsd_xmm0_xmm1() {
    emit8(0xF2); emit8(0x0F); emit8(0x5C); emit8(0xC1);
}

void X64Assembler::mulsd_xmm0_xmm1() {
    emit8(0xF2); emit8(0x0F); emit8(0x59); emit8(0xC1);
}

void X64Assembler::divsd_xmm0_xmm1() {
    emit8(0xF2); emit8(0x0F); emit8(0x5E); emit8(0xC1);
}

void X64Assembler::addsd_xmm0_mem_rbp(int32_t offset) {
    emit8(0xF2); emit8(0x0F); emit8(0x58); emit8(0x85);
    emit32(offset);
}

void X64Assembler::subsd_xmm0_mem_rbp(int32_t offset) {
    emit8(0xF2); emit8(0x0F); emit8(0x5C); emit8(0x85);
    emit32(offset);
}

void X64Assembler::mulsd_xmm0_mem_rbp(int32_t offset) {
    emit8(0xF2); emit8(0x0F); emit8(0x59); emit8(0x85);
    emit32(offset);
}

void X64Assembler::divsd_xmm0_mem_rbp(int32_t offset) {
    emit8(0xF2); emit8(0x0F); emit8(0x5E); emit8(0x85);
    emit32(offset);
}

// Comparison
void X64Assembler::comisd_xmm0_xmm1() {
    emit8(0x66); emit8(0x0F); emit8(0x2F); emit8(0xC1);
}

void X64Assembler::ucomisd_xmm0_xmm1() {
    emit8(0x66); emit8(0x0F); emit8(0x2E); emit8(0xC1);
}

void X64Assembler::comisd_xmm0_mem_rbp(int32_t offset) {
    emit8(0x66); emit8(0x0F); emit8(0x2F); emit8(0x85);
    emit32(offset);
}

// Conversion
void X64Assembler::cvtsi2sd_xmm0_rax() {
    emit8(0xF2); emit8(0x48); emit8(0x0F); emit8(0x2A); emit8(0xC0);
}

void X64Assembler::cvtsi2sd_xmm1_rax() {
    emit8(0xF2); emit8(0x48); emit8(0x0F); emit8(0x2A); emit8(0xC8);
}

void X64Assembler::cvtsi2sd_xmm0_ecx() {
    emit8(0xF2); emit8(0x0F); emit8(0x2A); emit8(0xC1);
}

void X64Assembler::cvttsd2si_rax_xmm0() {
    emit8(0xF2); emit8(0x48); emit8(0x0F); emit8(0x2C); emit8(0xC0);
}

void X64Assembler::cvttsd2si_eax_xmm0() {
    emit8(0xF2); emit8(0x0F); emit8(0x2C); emit8(0xC0);
}

// Logical
void X64Assembler::xorpd_xmm0_xmm1() {
    emit8(0x66); emit8(0x0F); emit8(0x57); emit8(0xC1);
}

void X64Assembler::xorpd_xmm0_xmm0() {
    emit8(0x66); emit8(0x0F); emit8(0x57); emit8(0xC0);
}

void X64Assembler::xorpd_xmm1_xmm1() {
    emit8(0x66); emit8(0x0F); emit8(0x57); emit8(0xC9);
}

// Square root
void X64Assembler::sqrtsd_xmm0_xmm0() {
    emit8(0xF2); emit8(0x0F); emit8(0x51); emit8(0xC0);
}

void X64Assembler::sqrtsd_xmm0_xmm1() {
    emit8(0xF2); emit8(0x0F); emit8(0x51); emit8(0xC1);
}

// Single precision
void X64Assembler::movss_xmm0_mem_rbp(int32_t offset) {
    emit8(0xF3); emit8(0x0F); emit8(0x10); emit8(0x85);
    emit32(offset);
}

void X64Assembler::movss_mem_rbp_xmm0(int32_t offset) {
    emit8(0xF3); emit8(0x0F); emit8(0x11); emit8(0x85);
    emit32(offset);
}

void X64Assembler::cvtss2sd_xmm0_xmm0() {
    emit8(0xF3); emit8(0x0F); emit8(0x5A); emit8(0xC0);
}

void X64Assembler::cvtsd2ss_xmm0_xmm0() {
    emit8(0xF2); emit8(0x0F); emit8(0x5A); emit8(0xC0);
}

// ============================================
// Packed SIMD Instructions
// ============================================

// Packed integer (SSE2)
void X64Assembler::movdqu_xmm0_mem(int32_t offset) {
    emit8(0xF3); emit8(0x0F); emit8(0x6F); emit8(0x85);
    emit32(offset);
}

void X64Assembler::movdqu_mem_xmm0(int32_t offset) {
    emit8(0xF3); emit8(0x0F); emit8(0x7F); emit8(0x85);
    emit32(offset);
}

void X64Assembler::movdqa_xmm0_xmm1() {
    emit8(0x66); emit8(0x0F); emit8(0x6F); emit8(0xC1);
}

void X64Assembler::movdqa_xmm1_xmm0() {
    emit8(0x66); emit8(0x0F); emit8(0x6F); emit8(0xC8);
}

void X64Assembler::paddd_xmm0_xmm1() {
    emit8(0x66); emit8(0x0F); emit8(0xFE); emit8(0xC1);
}

void X64Assembler::paddd_xmm0_mem(int32_t offset) {
    emit8(0x66); emit8(0x0F); emit8(0xFE); emit8(0x85);
    emit32(offset);
}

void X64Assembler::psubd_xmm0_xmm1() {
    emit8(0x66); emit8(0x0F); emit8(0xFA); emit8(0xC1);
}

void X64Assembler::pmulld_xmm0_xmm1() {
    emit8(0x66); emit8(0x0F); emit8(0x38); emit8(0x40); emit8(0xC1);
}

void X64Assembler::paddq_xmm0_xmm1() {
    emit8(0x66); emit8(0x0F); emit8(0xD4); emit8(0xC1);
}

void X64Assembler::psubq_xmm0_xmm1() {
    emit8(0x66); emit8(0x0F); emit8(0xFB); emit8(0xC1);
}

// Packed double (SSE2)
void X64Assembler::movupd_xmm0_mem(int32_t offset) {
    emit8(0x66); emit8(0x0F); emit8(0x10); emit8(0x85);
    emit32(offset);
}

void X64Assembler::movupd_mem_xmm0(int32_t offset) {
    emit8(0x66); emit8(0x0F); emit8(0x11); emit8(0x85);
    emit32(offset);
}

void X64Assembler::movapd_xmm0_xmm1() {
    emit8(0x66); emit8(0x0F); emit8(0x28); emit8(0xC1);
}

void X64Assembler::movapd_xmm1_xmm0() {
    emit8(0x66); emit8(0x0F); emit8(0x28); emit8(0xC8);
}

void X64Assembler::addpd_xmm0_xmm1() {
    emit8(0x66); emit8(0x0F); emit8(0x58); emit8(0xC1);
}

void X64Assembler::addpd_xmm0_mem(int32_t offset) {
    emit8(0x66); emit8(0x0F); emit8(0x58); emit8(0x85);
    emit32(offset);
}

void X64Assembler::subpd_xmm0_xmm1() {
    emit8(0x66); emit8(0x0F); emit8(0x5C); emit8(0xC1);
}

void X64Assembler::mulpd_xmm0_xmm1() {
    emit8(0x66); emit8(0x0F); emit8(0x59); emit8(0xC1);
}

void X64Assembler::divpd_xmm0_xmm1() {
    emit8(0x66); emit8(0x0F); emit8(0x5E); emit8(0xC1);
}

// Packed float (SSE)
void X64Assembler::movups_xmm0_mem(int32_t offset) {
    emit8(0x0F); emit8(0x10); emit8(0x85);
    emit32(offset);
}

void X64Assembler::movups_mem_xmm0(int32_t offset) {
    emit8(0x0F); emit8(0x11); emit8(0x85);
    emit32(offset);
}

void X64Assembler::addps_xmm0_xmm1() {
    emit8(0x0F); emit8(0x58); emit8(0xC1);
}

void X64Assembler::addps_xmm0_mem(int32_t offset) {
    emit8(0x0F); emit8(0x58); emit8(0x85);
    emit32(offset);
}

void X64Assembler::subps_xmm0_xmm1() {
    emit8(0x0F); emit8(0x5C); emit8(0xC1);
}

void X64Assembler::mulps_xmm0_xmm1() {
    emit8(0x0F); emit8(0x59); emit8(0xC1);
}

void X64Assembler::divps_xmm0_xmm1() {
    emit8(0x0F); emit8(0x5E); emit8(0xC1);
}

// Horizontal operations
void X64Assembler::haddpd_xmm0_xmm0() {
    emit8(0x66); emit8(0x0F); emit8(0x7C); emit8(0xC0);
}

void X64Assembler::haddps_xmm0_xmm0() {
    emit8(0xF2); emit8(0x0F); emit8(0x7C); emit8(0xC0);
}

void X64Assembler::phaddd_xmm0_xmm0() {
    emit8(0x66); emit8(0x0F); emit8(0x38); emit8(0x02); emit8(0xC0);
}

// Shuffle
void X64Assembler::pshufd_xmm0_xmm0_imm8(uint8_t imm) {
    emit8(0x66); emit8(0x0F); emit8(0x70); emit8(0xC0); emit8(imm);
}

void X64Assembler::shufpd_xmm0_xmm1_imm8(uint8_t imm) {
    emit8(0x66); emit8(0x0F); emit8(0xC6); emit8(0xC1); emit8(imm);
}

void X64Assembler::movddup_xmm0_mem(int32_t offset) {
    emit8(0xF2); emit8(0x0F); emit8(0x12); emit8(0x85);
    emit32(offset);
}

void X64Assembler::pshufd_broadcast_xmm0(int32_t offset) {
    emit8(0x66); emit8(0x0F); emit8(0x70); emit8(0x85);
    emit32(offset);
    emit8(0x00);
}

void X64Assembler::pextrd_eax_xmm0_imm8(uint8_t idx) {
    emit8(0x66); emit8(0x0F); emit8(0x3A); emit8(0x16); emit8(0xC0); emit8(idx);
}

void X64Assembler::extractps_eax_xmm0_imm8(uint8_t idx) {
    emit8(0x66); emit8(0x0F); emit8(0x3A); emit8(0x17); emit8(0xC0); emit8(idx);
}

void X64Assembler::pxor_xmm0_xmm0() {
    emit8(0x66); emit8(0x0F); emit8(0xEF); emit8(0xC0);
}

void X64Assembler::pxor_xmm1_xmm1() {
    emit8(0x66); emit8(0x0F); emit8(0xEF); emit8(0xC9);
}

} // namespace tyl
