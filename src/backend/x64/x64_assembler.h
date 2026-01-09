// Tyl Compiler - x86-64 Assembler
#ifndef TYL_X64_ASSEMBLER_H
#define TYL_X64_ASSEMBLER_H

#include <vector>
#include <string>
#include <map>
#include <cstdint>

namespace tyl {

class X64Assembler {
public:
    std::vector<uint8_t> code;
    std::map<std::string, size_t> labels;
    std::vector<std::pair<size_t, std::string>> labelFixups;
    std::vector<std::pair<size_t, uint32_t>> ripFixups;
    
    size_t currentOffset() const { return code.size(); }
    
    // Labels
    void label(const std::string& name);
    void fixupLabel(const std::string& name);
    void fixupRIP(uint32_t targetRVA);
    void resolve(uint32_t codeRVA = 0x1000);
    
    // Data movement
    void mov_rax_imm64(int64_t val);
    void mov_rcx_imm64(int64_t val);
    void mov_rdx_imm64(int64_t val);
    void mov_r8_imm64(int64_t val);
    void mov_r8d_imm32(int32_t val);  // Direct 32-bit immediate to r8d (6 bytes)
    void mov_rax_imm32(int32_t val);
    void mov_ecx_imm32(int32_t val);
    void mov_edx_imm32(int32_t val);
    void mov_rax_rcx();
    void mov_rcx_rax();
    void mov_rdx_rax();
    void mov_r8_rax();
    void mov_r8_rcx();
    void mov_rax_mem_rbp(int32_t offset);
    void mov_rcx_mem_rbp(int32_t offset);
    void mov_rdx_mem_rbp(int32_t offset);
    void mov_mem_rbp_rax(int32_t offset);
    void mov_mem_rbp_rcx(int32_t offset);
    void mov_mem_rbp_rdx(int32_t offset);
    void mov_rax_mem_rax();
    void mov_mem_rcx_rax();
    void mov_mem_rax_rcx();
    void mov_rcx_mem_rax();
    void mov_rdx_mem_rax();  // rdx = [rax]
    void mov_rax_mem_rcx();  // rax = [rcx]
    void mov_rax_mem_rsp(int32_t offset);  // rax = [rsp + offset]
    void mov_rcx_mem_rax(int32_t offset);  // rcx = [rax + offset]
    void mov_rdx_mem_rax(int32_t offset);  // rdx = [rax + offset]
    void mov_r8_mem_rax(int32_t offset);   // r8 = [rax + offset]
    void mov_r9_mem_rcx(int32_t offset);   // r9 = [rcx + offset]
    void mov_mem_rax_rcx(int32_t offset);  // [rax + offset] = rcx
    void mov_mem_rax_rdx(int32_t offset);  // [rax + offset] = rdx
    void mov_mem_rcx_rax(int32_t offset);  // [rcx + offset] = rax
    
    // LEA
    void lea_rcx_rip_fixup(uint32_t targetRVA);
    void lea_rax_rip_fixup(uint32_t targetRVA);
    void lea_rax_rbp(int32_t offset);
    void lea_rcx_rbp(int32_t offset);
    void lea_rdx_rbp_offset(int32_t offset);
    void lea_rcx_rax_offset(int32_t offset);  // rcx = rax + offset
    
    // Stack
    void push_rbp();
    void pop_rbp();
    void push_rax();
    void pop_rax();
    void push_rcx();
    void pop_rcx();
    void push_rdx();
    void pop_rdx();
    void push_rdi();
    void pop_rdi();
    void pop_r8();
    void pop_r9();
    void push_r9();
    void mov_rbp_rsp();
    void mov_rsp_rbp();
    void sub_rsp_imm32(int32_t val);
    void add_rsp_imm32(int32_t val);
    
    // Arithmetic
    void add_rax_rcx();
    void sub_rax_rcx();
    void imul_rax_rcx();
    void cqo();
    void idiv_rcx();
    void div_rdx();     // unsigned divide by rdx
    void neg_rax();
    void not_rax();     // bitwise NOT
    void imul_rdx_r8(); // rdx = rdx * r8
    void add_rcx_rdx(); // rcx += rdx
    void inc_rax();
    void inc_rcx();
    void dec_rax();
    void dec_rcx();
    
    // Comparison
    void cmp_rax_rcx();
    void cmp_rax_imm32(int32_t val);
    void cmp_rax_imm8(int8_t val);   // Compare RAX with 8-bit sign-extended immediate
    void cmp_rax_mem_rbp(int32_t offset);
    void cmp_rcx_rdx();  // compare rcx with rdx
    void test_rax_rax();
    void test_rcx_rcx();
    void sete_al();
    void setne_al();
    void setl_al();
    void setg_al();
    void setle_al();
    void setge_al();
    void movzx_rax_al();
    
    // Logical
    void xor_rax_rax();
    void xor_rax_rcx();  // Bitwise XOR
    void xor_ecx_ecx();
    void xor_rcx_rcx();
    void xor_rdx_rdx();
    void xor_r8_r8();
    void xor_r9_r9();
    void and_rax_rcx();
    void or_rax_rcx();
    
    // ============================================
    // Callee-saved register operations (for register allocation)
    // ============================================
    
    // RBX operations
    void push_rbx();
    void pop_rbx();
    void mov_rbx_rax();
    void mov_rax_rbx();
    void mov_rbx_rcx();      // For parameter passing
    void mov_rbx_rdx();
    void mov_rbx_r8();
    void mov_rbx_r9();
    void mov_rbx_imm64(int64_t val);
    void mov_rbx_imm32(int32_t val);
    void xor_rbx_rbx();
    
    // R12 operations
    void push_r12();
    void pop_r12();
    void mov_r12_rax();
    void mov_rax_r12();
    void mov_r12_rcx();
    void mov_r12_rdx();
    void mov_r12_r8();
    void mov_r12_r9();
    void mov_r12_imm64(int64_t val);
    void xor_r12_r12();
    
    // R13 operations
    void push_r13();
    void pop_r13();
    void mov_r13_rax();
    void mov_rax_r13();
    void mov_r13_rcx();
    void mov_r13_rdx();
    void mov_r13_r8();
    void mov_r13_r9();
    void mov_r13_imm64(int64_t val);
    void xor_r13_r13();
    
    // R14 operations
    void push_r14();
    void pop_r14();
    void mov_r14_rax();
    void mov_rax_r14();
    void mov_r14_rcx();
    void mov_r14_rdx();
    void mov_r14_r8();
    void mov_r14_r9();
    void mov_r14_imm64(int64_t val);
    void xor_r14_r14();
    
    // R15 operations
    void push_r15();
    void pop_r15();
    void mov_r15_rax();
    void mov_rax_r15();
    void mov_r15_rcx();
    void mov_r15_rdx();
    void mov_r15_r8();
    void mov_r15_r9();
    void mov_r15_imm64(int64_t val);
    void xor_r15_r15();
    
    // Move from callee-saved to RCX (for closure captures)
    void mov_rcx_rbx();
    void mov_rcx_r12();
    void mov_rcx_r13();
    void mov_rcx_r14();
    void mov_rcx_r15();
    
    // Control flow
    void jmp_rel32(const std::string& label);
    void jz_rel32(const std::string& label);
    void je_rel32(const std::string& label);   // Jump if equal (same as jz)
    void jnz_rel32(const std::string& label);
    void jne_rel32(const std::string& label);  // Jump if not equal (same as jnz)
    void jge_rel32(const std::string& label);
    void jl_rel32(const std::string& label);
    void jle_rel32(const std::string& label);
    void jg_rel32(const std::string& label);
    void ja_rel32(const std::string& label);   // Jump if above (unsigned)
    void jb_rel32(const std::string& label);   // Jump if below (unsigned)
    void jae_rel32(const std::string& label);  // Jump if above or equal
    void jbe_rel32(const std::string& label);  // Jump if below or equal
    void call_rel32(const std::string& label);
    void call_mem_rip(uint32_t iatRVA);
    void call_rax();
    void ret();
    void nop();
    void int3();
    
    // ============================================
    // SSE/SSE2 Floating Point Instructions (XMM)
    // ============================================
    
    // Move scalar double (64-bit float)
    void movsd_xmm0_mem_rip(uint32_t targetRVA);  // Load from memory via RIP-relative
    void movsd_xmm0_mem_rbp(int32_t offset);      // Load from stack
    void movsd_xmm1_mem_rbp(int32_t offset);      // Load from stack to xmm1
    void movsd_mem_rbp_xmm0(int32_t offset);      // Store to stack
    void movsd_xmm0_xmm1();                        // xmm0 = xmm1
    void movsd_xmm1_xmm0();                        // xmm1 = xmm0
    
    // Move between XMM and general purpose registers
    void movq_xmm0_rax();                          // xmm0 = rax (bit copy)
    void movq_rax_xmm0();                          // rax = xmm0 (bit copy)
    void movq_xmm1_rcx();                          // xmm1 = rcx (bit copy)
    void movq_rcx_xmm1();                          // rcx = xmm1 (bit copy)
    
    // Arithmetic (scalar double)
    void addsd_xmm0_xmm1();                        // xmm0 += xmm1
    void subsd_xmm0_xmm1();                        // xmm0 -= xmm1
    void mulsd_xmm0_xmm1();                        // xmm0 *= xmm1
    void divsd_xmm0_xmm1();                        // xmm0 /= xmm1
    
    // Arithmetic with memory operand
    void addsd_xmm0_mem_rbp(int32_t offset);
    void subsd_xmm0_mem_rbp(int32_t offset);
    void mulsd_xmm0_mem_rbp(int32_t offset);
    void divsd_xmm0_mem_rbp(int32_t offset);
    
    // Comparison
    void comisd_xmm0_xmm1();                       // Compare and set EFLAGS
    void ucomisd_xmm0_xmm1();                      // Unordered compare (handles NaN)
    void comisd_xmm0_mem_rbp(int32_t offset);
    
    // Conversion
    void cvtsi2sd_xmm0_rax();                      // Convert int64 to double
    void cvtsi2sd_xmm1_rax();                      // Convert int64 to double in xmm1
    void cvtsi2sd_xmm0_ecx();                      // Convert int32 to double
    void cvttsd2si_rax_xmm0();                     // Convert double to int64 (truncate)
    void cvttsd2si_eax_xmm0();                     // Convert double to int32 (truncate)
    
    // Negation (xor with sign bit)
    void xorpd_xmm0_xmm1();                        // XOR packed double
    
    // Square root
    void sqrtsd_xmm0_xmm0();                       // xmm0 = sqrt(xmm0)
    void sqrtsd_xmm0_xmm1();                       // xmm0 = sqrt(xmm1)
    
    // Zero XMM register
    void xorpd_xmm0_xmm0();                        // xmm0 = 0.0
    void xorpd_xmm1_xmm1();                        // xmm1 = 0.0
    
    // Move scalar single (32-bit float) - for f32 support
    void movss_xmm0_mem_rbp(int32_t offset);
    void movss_mem_rbp_xmm0(int32_t offset);
    void cvtss2sd_xmm0_xmm0();                     // Convert f32 to f64
    void cvtsd2ss_xmm0_xmm0();                     // Convert f64 to f32
    
    // ============================================
    // SSE/AVX Packed SIMD Instructions (for vectorization)
    // ============================================
    
    // Packed integer operations (SSE2) - 4 x 32-bit integers
    void movdqu_xmm0_mem(int32_t offset);          // Load 128-bit unaligned
    void movdqu_mem_xmm0(int32_t offset);          // Store 128-bit unaligned
    void movdqa_xmm0_xmm1();                       // Move aligned xmm1 -> xmm0
    void movdqa_xmm1_xmm0();                       // Move aligned xmm0 -> xmm1
    
    void paddd_xmm0_xmm1();                        // Packed add 4 x int32
    void paddd_xmm0_mem(int32_t offset);           // Packed add from memory
    void psubd_xmm0_xmm1();                        // Packed subtract 4 x int32
    void pmulld_xmm0_xmm1();                       // Packed multiply 4 x int32 (SSE4.1)
    
    // Packed 64-bit integer operations
    void paddq_xmm0_xmm1();                        // Packed add 2 x int64
    void psubq_xmm0_xmm1();                        // Packed subtract 2 x int64
    
    // Packed double operations (SSE2) - 2 x 64-bit doubles
    void movupd_xmm0_mem(int32_t offset);          // Load 2 doubles unaligned
    void movupd_mem_xmm0(int32_t offset);          // Store 2 doubles unaligned
    void movapd_xmm0_xmm1();                       // Move aligned
    void movapd_xmm1_xmm0();
    
    void addpd_xmm0_xmm1();                        // Packed add 2 x double
    void addpd_xmm0_mem(int32_t offset);           // Packed add from memory
    void subpd_xmm0_xmm1();                        // Packed subtract 2 x double
    void mulpd_xmm0_xmm1();                        // Packed multiply 2 x double
    void divpd_xmm0_xmm1();                        // Packed divide 2 x double
    
    // Packed float operations (SSE) - 4 x 32-bit floats
    void movups_xmm0_mem(int32_t offset);          // Load 4 floats unaligned
    void movups_mem_xmm0(int32_t offset);          // Store 4 floats unaligned
    
    void addps_xmm0_xmm1();                        // Packed add 4 x float
    void addps_xmm0_mem(int32_t offset);           // Packed add from memory
    void subps_xmm0_xmm1();                        // Packed subtract 4 x float
    void mulps_xmm0_xmm1();                        // Packed multiply 4 x float
    void divps_xmm0_xmm1();                        // Packed divide 4 x float
    
    // Horizontal operations (for reductions)
    void haddpd_xmm0_xmm0();                       // Horizontal add doubles
    void haddps_xmm0_xmm0();                       // Horizontal add floats
    void phaddd_xmm0_xmm0();                       // Horizontal add int32 (SSSE3)
    
    // Shuffle and permute
    void pshufd_xmm0_xmm0_imm8(uint8_t imm);       // Shuffle int32
    void shufpd_xmm0_xmm1_imm8(uint8_t imm);       // Shuffle doubles
    
    // Broadcast (load single value to all lanes)
    void movddup_xmm0_mem(int32_t offset);         // Broadcast double to both lanes
    void pshufd_broadcast_xmm0(int32_t offset);    // Broadcast int32 to all 4 lanes
    
    // Extract scalar from vector
    void pextrd_eax_xmm0_imm8(uint8_t idx);        // Extract int32 from xmm0
    void extractps_eax_xmm0_imm8(uint8_t idx);     // Extract float from xmm0
    
    // Zero upper bits (for clean scalar operations after SIMD)
    void pxor_xmm0_xmm0();                         // Zero xmm0
    void pxor_xmm1_xmm1();                         // Zero xmm1
    
    // ============================================
    // Advanced Instruction Selection Optimizations
    // ============================================
    
    // LEA for complex address calculations (a + b*scale + disp)
    void lea_rax_rax_rcx();                        // rax = rax + rcx
    void lea_rax_rax_rcx_2();                      // rax = rax + rcx*2
    void lea_rax_rax_rcx_4();                      // rax = rax + rcx*4
    void lea_rax_rax_rcx_8();                      // rax = rax + rcx*8
    void lea_rax_rcx_imm32(int32_t disp);          // rax = rcx + disp
    void lea_rax_rax_imm32(int32_t disp);          // rax = rax + disp
    void lea_rax_rcx_rcx();                        // rax = rcx + rcx (multiply by 2)
    void lea_rax_rcx_rcx_2();                      // rax = rcx + rcx*2 (multiply by 3)
    void lea_rax_rcx_4();                          // rax = rcx*4
    void lea_rax_rcx_8();                          // rax = rcx*8
    
    // Conditional moves (branchless conditionals)
    void cmove_rax_rcx();                          // if ZF: rax = rcx
    void cmovne_rax_rcx();                         // if !ZF: rax = rcx
    void cmovl_rax_rcx();                          // if SF!=OF: rax = rcx (signed less)
    void cmovg_rax_rcx();                          // if ZF=0 && SF=OF: rax = rcx (signed greater)
    void cmovle_rax_rcx();                         // if ZF=1 || SF!=OF: rax = rcx
    void cmovge_rax_rcx();                         // if SF=OF: rax = rcx
    void cmova_rax_rcx();                          // if CF=0 && ZF=0: rax = rcx (unsigned above)
    void cmovb_rax_rcx();                          // if CF=1: rax = rcx (unsigned below)
    void cmovae_rax_rcx();                         // if CF=0: rax = rcx
    void cmovbe_rax_rcx();                         // if CF=1 || ZF=1: rax = rcx
    
    // Shift instructions for strength reduction
    void shl_rax_imm8(uint8_t count);              // rax <<= count
    void shr_rax_imm8(uint8_t count);              // rax >>= count (logical)
    void sar_rax_imm8(uint8_t count);              // rax >>= count (arithmetic)
    void shl_rax_cl();                             // rax <<= cl
    void shr_rax_cl();                             // rax >>= cl
    void sar_rax_cl();                             // rax >>= cl
    
    // Bit manipulation
    void bsr_rax_rcx();                            // Bit scan reverse (find highest set bit)
    void bsf_rax_rcx();                            // Bit scan forward (find lowest set bit)
    void popcnt_rax_rcx();                         // Population count (count set bits)
    void lzcnt_rax_rcx();                          // Leading zero count
    void tzcnt_rax_rcx();                          // Trailing zero count
    
    // Additional arithmetic
    void add_rax_imm32(int32_t val);               // rax += imm32
    void sub_rax_imm32(int32_t val);               // rax -= imm32
    void imul_rax_rcx_imm32(int32_t val);          // rax = rcx * imm32
    void imul_rax_rax_imm32(int32_t val);          // rax = rax * imm32
    
    // Test with immediate
    void test_rax_imm32(int32_t val);              // test rax, imm32
    void test_al_imm8(uint8_t val);                // test al, imm8
    
    // Additional moves
    void mov_rax_rdx();
    void mov_rdx_rcx();
    void mov_rcx_rdx();
    void xchg_rax_rcx();  // exchange rax and rcx
    void mov_rdi_rax();
    void mov_rax_rdi();
    void mov_rcx_rdi();
    void mov_mem_rdi_rax();                        // [rdi] = rax
    void mov_rsi_rax();                            // rsi = rax
    void mov_rsi_rcx();                            // rsi = rcx
    void mov_rsi_mem_rbp(int32_t offset);          // rsi = [rbp + offset]
    void mov_rsi_mem_rax(int32_t offset);          // rsi = [rax + offset]
    void mov_rdi_mem_rbp(int32_t offset);          // rdi = [rbp + offset]
    
    // Additional arithmetic with rcx
    void add_rcx_imm32(int32_t val);               // rcx += imm32
    
private:
    void emit8(uint8_t b);
    void emit32(int32_t val);
    void emit64(int64_t val);
};

} // namespace tyl

#endif // TYL_X64_ASSEMBLER_H
