// Tyl Compiler - Extended Numeric Type Builtins
// Implements: BigInt, BigFloat, Decimal, Rational, Fixed, Vec, Mat operations

#include "backend/codegen/codegen_base.h"

namespace tyl {

// ============================================================================
// BigInt Operations
// BigInt is stored as: [sign: 8 bytes][length: 8 bytes][value: 8 bytes]
// ============================================================================

// bigint(value: int) -> BigInt - Create BigInt from int
void NativeCodeGen::emitBigIntNew(CallExpr& node) {
    int id = labelCounter++;
    std::string valVar = "$bigint_val_" + std::to_string(id);
    std::string ptrVar = "$bigint_ptr_" + std::to_string(id);
    
    // Evaluate the integer value first
    node.args[0]->accept(*this);
    
    // Save value to stack before allocation (allocation clobbers registers)
    allocLocal(valVar);
    asm_.mov_mem_rbp_rax(locals[valVar]);
    
    // Allocate 24 bytes using GC
    emitGCAllocRaw(24);
    
    // Save pointer
    allocLocal(ptrVar);
    asm_.mov_mem_rbp_rax(locals[ptrVar]);
    
    // Load value back
    asm_.mov_rax_mem_rbp(locals[valVar]);
    // mov rcx, rax
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x89);
    asm_.code.push_back(0xC1);
    
    // Determine sign and absolute value
    // test rcx, rcx
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x85);
    asm_.code.push_back(0xC9);
    
    // mov rdx, 1 (assume positive)
    asm_.code.push_back(0x48);
    asm_.code.push_back(0xC7);
    asm_.code.push_back(0xC2);
    asm_.code.push_back(0x01);
    asm_.code.push_back(0x00);
    asm_.code.push_back(0x00);
    asm_.code.push_back(0x00);

    // jns skip_neg (jump if not sign = positive)
    asm_.code.push_back(0x79);  // JNS rel8
    size_t jnsOffset = asm_.code.size();
    asm_.code.push_back(0x00);  // placeholder
    
    // Negative: negate rcx, set sign to -1
    // neg rcx
    asm_.code.push_back(0x48);
    asm_.code.push_back(0xF7);
    asm_.code.push_back(0xD9);
    
    // mov rdx, -1
    asm_.code.push_back(0x48);
    asm_.code.push_back(0xC7);
    asm_.code.push_back(0xC2);
    asm_.code.push_back(0xFF);
    asm_.code.push_back(0xFF);
    asm_.code.push_back(0xFF);
    asm_.code.push_back(0xFF);
    
    // Patch jump
    asm_.code[jnsOffset] = (uint8_t)(asm_.code.size() - jnsOffset - 1);
    
    // Load pointer
    asm_.mov_rax_mem_rbp(locals[ptrVar]);
    
    // Store sign at [rax]: mov [rax], rdx
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x89);
    asm_.code.push_back(0x10);
    
    // Store length (1) at [rax+8]: mov qword [rax+8], 1
    asm_.code.push_back(0x48);
    asm_.code.push_back(0xC7);
    asm_.code.push_back(0x40);
    asm_.code.push_back(0x08);
    asm_.code.push_back(0x01);
    asm_.code.push_back(0x00);
    asm_.code.push_back(0x00);
    asm_.code.push_back(0x00);
    
    // Store value at [rax+16]: mov [rax+16], rcx
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x89);
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x10);
    
    lastExprWasFloat_ = false;
}

// bigint_add(a: BigInt, b: BigInt) -> BigInt
void NativeCodeGen::emitBigIntAdd(CallExpr& node) {
    int id = labelCounter++;
    std::string aVar = "$bigint_a_" + std::to_string(id);
    std::string bVar = "$bigint_b_" + std::to_string(id);
    std::string bvalVar = "$bigint_bval_" + std::to_string(id);
    std::string resultVar = "$bigint_result_" + std::to_string(id);
    std::string newVar = "$bigint_new_" + std::to_string(id);
    
    // Evaluate first BigInt
    node.args[0]->accept(*this);
    allocLocal(aVar);
    asm_.mov_mem_rbp_rax(locals[aVar]);
    
    // Evaluate second BigInt
    node.args[1]->accept(*this);
    allocLocal(bVar);
    asm_.mov_mem_rbp_rax(locals[bVar]);
    
    // Load b's value and sign, compute signed value
    // mov rcx, [rax+16] (value)
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x8B);
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x10);
    
    // mov rdx, [rax] (sign)
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x8B);
    asm_.code.push_back(0x10);
    
    // imul rcx, rdx (signed value of b)
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0xAF);
    asm_.code.push_back(0xCA);
    
    // Save b's signed value
    allocLocal(bvalVar);
    asm_.mov_mem_rbp_rcx(locals[bvalVar]);
    
    // Load a
    asm_.mov_rax_mem_rbp(locals[aVar]);
    
    // Load a's value: mov rcx, [rax+16]
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x8B);
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x10);
    
    // Load a's sign: mov rdx, [rax]
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x8B);
    asm_.code.push_back(0x10);
    
    // imul rcx, rdx (signed value of a)
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0xAF);
    asm_.code.push_back(0xCA);

    // Load b's signed value and add
    asm_.mov_rdx_mem_rbp(locals[bvalVar]);
    
    // add rcx, rdx (result in rcx)
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x01);
    asm_.code.push_back(0xD1);
    
    // Save result
    allocLocal(resultVar);
    asm_.mov_mem_rbp_rcx(locals[resultVar]);
    
    // Allocate new BigInt
    emitGCAllocRaw(24);
    allocLocal(newVar);
    asm_.mov_mem_rbp_rax(locals[newVar]);
    
    // Load result
    asm_.mov_rcx_mem_rbp(locals[resultVar]);
    
    // Determine sign
    // test rcx, rcx
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x85);
    asm_.code.push_back(0xC9);
    
    // mov rdx, 1 (assume positive)
    asm_.code.push_back(0x48);
    asm_.code.push_back(0xC7);
    asm_.code.push_back(0xC2);
    asm_.code.push_back(0x01);
    asm_.code.push_back(0x00);
    asm_.code.push_back(0x00);
    asm_.code.push_back(0x00);
    
    // jns skip
    asm_.code.push_back(0x79);
    size_t jnsOff = asm_.code.size();
    asm_.code.push_back(0x00);
    
    // neg rcx
    asm_.code.push_back(0x48);
    asm_.code.push_back(0xF7);
    asm_.code.push_back(0xD9);
    
    // mov rdx, -1
    asm_.code.push_back(0x48);
    asm_.code.push_back(0xC7);
    asm_.code.push_back(0xC2);
    asm_.code.push_back(0xFF);
    asm_.code.push_back(0xFF);
    asm_.code.push_back(0xFF);
    asm_.code.push_back(0xFF);
    
    asm_.code[jnsOff] = (uint8_t)(asm_.code.size() - jnsOff - 1);

    // Load pointer
    asm_.mov_rax_mem_rbp(locals[newVar]);
    
    // Store sign: mov [rax], rdx
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x89);
    asm_.code.push_back(0x10);
    
    // Store length: mov qword [rax+8], 1
    asm_.code.push_back(0x48);
    asm_.code.push_back(0xC7);
    asm_.code.push_back(0x40);
    asm_.code.push_back(0x08);
    asm_.code.push_back(0x01);
    asm_.code.push_back(0x00);
    asm_.code.push_back(0x00);
    asm_.code.push_back(0x00);
    
    // Store value: mov [rax+16], rcx
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x89);
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x10);
    
    lastExprWasFloat_ = false;
}

// bigint_to_int(b: BigInt) -> int
void NativeCodeGen::emitBigIntToInt(CallExpr& node) {
    node.args[0]->accept(*this);
    
    // Load sign: mov rcx, [rax]
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x8B);
    asm_.code.push_back(0x08);
    
    // Load value: mov rax, [rax+16]
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x8B);
    asm_.code.push_back(0x40);
    asm_.code.push_back(0x10);
    
    // Multiply by sign: imul rax, rcx
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0xAF);
    asm_.code.push_back(0xC1);
    
    lastExprWasFloat_ = false;
}

// ============================================================================
// Rational Operations
// Rational is stored as: [numerator: 8 bytes][denominator: 8 bytes]
// ============================================================================

// rational(num, denom) -> Rational
void NativeCodeGen::emitRationalNew(CallExpr& node) {
    int id = labelCounter++;
    std::string numVar = "$rat_num_" + std::to_string(id);
    std::string denomVar = "$rat_denom_" + std::to_string(id);
    std::string ptrVar = "$rat_ptr_" + std::to_string(id);
    
    // Evaluate numerator
    node.args[0]->accept(*this);
    allocLocal(numVar);
    asm_.mov_mem_rbp_rax(locals[numVar]);
    
    // Evaluate denominator
    node.args[1]->accept(*this);
    allocLocal(denomVar);
    asm_.mov_mem_rbp_rax(locals[denomVar]);
    
    // Allocate 16 bytes
    emitGCAllocRaw(16);
    allocLocal(ptrVar);
    asm_.mov_mem_rbp_rax(locals[ptrVar]);
    
    // Load numerator and store
    asm_.mov_rcx_mem_rbp(locals[numVar]);
    asm_.mov_rax_mem_rbp(locals[ptrVar]);
    // mov [rax], rcx
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x89);
    asm_.code.push_back(0x08);
    
    // Load denominator and store
    asm_.mov_rcx_mem_rbp(locals[denomVar]);
    // mov [rax+8], rcx
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x89);
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x08);
    
    lastExprWasFloat_ = false;
}

// rational_add(a, b) -> Rational: (a/b) + (c/d) = (ad + bc) / bd
void NativeCodeGen::emitRationalAdd(CallExpr& node) {
    int id = labelCounter++;
    std::string aVar = "$rat_a_" + std::to_string(id);
    std::string bVar = "$rat_b_" + std::to_string(id);
    std::string numaVar = "$rat_numa_" + std::to_string(id);
    std::string denomaVar = "$rat_denoma_" + std::to_string(id);
    std::string numbVar = "$rat_numb_" + std::to_string(id);
    std::string denombVar = "$rat_denomb_" + std::to_string(id);
    std::string adVar = "$rat_ad_" + std::to_string(id);
    std::string newnumVar = "$rat_newnum_" + std::to_string(id);
    std::string newdenomVar = "$rat_newdenom_" + std::to_string(id);
    std::string resultVar = "$rat_result_" + std::to_string(id);
    
    // Evaluate first rational
    node.args[0]->accept(*this);
    allocLocal(aVar);
    asm_.mov_mem_rbp_rax(locals[aVar]);
    
    // Evaluate second rational
    node.args[1]->accept(*this);
    allocLocal(bVar);
    asm_.mov_mem_rbp_rax(locals[bVar]);

    // Load a: num_a in rcx, denom_a in rdx
    asm_.mov_rax_mem_rbp(locals[aVar]);
    // mov rcx, [rax] (num_a)
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x8B);
    asm_.code.push_back(0x08);
    // mov rdx, [rax+8] (denom_a)
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x8B);
    asm_.code.push_back(0x50);
    asm_.code.push_back(0x08);
    
    allocLocal(numaVar);
    allocLocal(denomaVar);
    asm_.mov_mem_rbp_rcx(locals[numaVar]);
    asm_.mov_mem_rbp_rdx(locals[denomaVar]);
    
    // Load b: num_b in rcx, denom_b in rdx
    asm_.mov_rax_mem_rbp(locals[bVar]);
    // mov rcx, [rax]
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x8B);
    asm_.code.push_back(0x08);
    // mov rdx, [rax+8]
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x8B);
    asm_.code.push_back(0x50);
    asm_.code.push_back(0x08);
    
    allocLocal(numbVar);
    allocLocal(denombVar);
    asm_.mov_mem_rbp_rcx(locals[numbVar]);
    asm_.mov_mem_rbp_rdx(locals[denombVar]);
    
    // Calculate ad: num_a * denom_b
    asm_.mov_rax_mem_rbp(locals[numaVar]);
    asm_.mov_rcx_mem_rbp(locals[denombVar]);
    // imul rax, rcx
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0xAF);
    asm_.code.push_back(0xC1);
    allocLocal(adVar);
    asm_.mov_mem_rbp_rax(locals[adVar]);
    
    // Calculate bc: num_b * denom_a
    asm_.mov_rax_mem_rbp(locals[numbVar]);
    asm_.mov_rcx_mem_rbp(locals[denomaVar]);
    // imul rax, rcx
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0xAF);
    asm_.code.push_back(0xC1);

    // Add ad + bc
    asm_.mov_rcx_mem_rbp(locals[adVar]);
    // add rax, rcx
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x01);
    asm_.code.push_back(0xC8);
    allocLocal(newnumVar);
    asm_.mov_mem_rbp_rax(locals[newnumVar]);
    
    // Calculate bd: denom_a * denom_b
    asm_.mov_rax_mem_rbp(locals[denomaVar]);
    asm_.mov_rcx_mem_rbp(locals[denombVar]);
    // imul rax, rcx
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0xAF);
    asm_.code.push_back(0xC1);
    allocLocal(newdenomVar);
    asm_.mov_mem_rbp_rax(locals[newdenomVar]);
    
    // Allocate result
    emitGCAllocRaw(16);
    allocLocal(resultVar);
    asm_.mov_mem_rbp_rax(locals[resultVar]);
    
    // Store numerator
    asm_.mov_rcx_mem_rbp(locals[newnumVar]);
    // mov [rax], rcx
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x89);
    asm_.code.push_back(0x08);
    
    // Store denominator
    asm_.mov_rcx_mem_rbp(locals[newdenomVar]);
    // mov [rax+8], rcx
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x89);
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x08);
    
    lastExprWasFloat_ = false;
}

// rational_to_float(r) -> float
void NativeCodeGen::emitRationalToFloat(CallExpr& node) {
    node.args[0]->accept(*this);
    
    // Load numerator: mov rcx, [rax]
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x8B);
    asm_.code.push_back(0x08);
    
    // Load denominator: mov rdx, [rax+8]
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x8B);
    asm_.code.push_back(0x50);
    asm_.code.push_back(0x08);

    // Convert numerator to double: cvtsi2sd xmm0, rcx
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x2A);
    asm_.code.push_back(0xC1);
    
    // Convert denominator to double: cvtsi2sd xmm1, rdx
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x2A);
    asm_.code.push_back(0xCA);
    
    // Divide: divsd xmm0, xmm1
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x5E);
    asm_.code.push_back(0xC1);
    
    lastExprWasFloat_ = true;
}

// ============================================================================
// Fixed-Point Operations
// Fixed[64,32] stored as int64 with 32 fractional bits
// ============================================================================

// fixed(value) -> Fixed - Create from float or int
void NativeCodeGen::emitFixedNew(CallExpr& node) {
    node.args[0]->accept(*this);
    
    if (lastExprWasFloat_) {
        // Multiply by 2^32 to convert to fixed point
        uint64_t scale = 0x41F0000000000000ULL;  // 2^32 as double
        
        // mov rax, scale
        asm_.code.push_back(0x48);
        asm_.code.push_back(0xB8);
        for (int i = 0; i < 8; i++) {
            asm_.code.push_back((scale >> (i * 8)) & 0xFF);
        }
        
        // movq xmm1, rax
        asm_.code.push_back(0x66);
        asm_.code.push_back(0x48);
        asm_.code.push_back(0x0F);
        asm_.code.push_back(0x6E);
        asm_.code.push_back(0xC8);
        
        // mulsd xmm0, xmm1
        asm_.code.push_back(0xF2);
        asm_.code.push_back(0x0F);
        asm_.code.push_back(0x59);
        asm_.code.push_back(0xC1);

        // cvttsd2si rax, xmm0
        asm_.code.push_back(0xF2);
        asm_.code.push_back(0x48);
        asm_.code.push_back(0x0F);
        asm_.code.push_back(0x2C);
        asm_.code.push_back(0xC0);
    } else {
        // Integer: shift left by 32 bits
        // shl rax, 32
        asm_.code.push_back(0x48);
        asm_.code.push_back(0xC1);
        asm_.code.push_back(0xE0);
        asm_.code.push_back(0x20);
    }
    
    lastExprWasFloat_ = false;
}

// fixed_add(a, b) -> Fixed
void NativeCodeGen::emitFixedAdd(CallExpr& node) {
    node.args[0]->accept(*this);
    allocLocal("$fixed_a");
    asm_.mov_mem_rbp_rax(locals["$fixed_a"]);
    
    node.args[1]->accept(*this);
    asm_.mov_rcx_mem_rbp(locals["$fixed_a"]);
    
    // add rax, rcx
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x01);
    asm_.code.push_back(0xC8);
    
    lastExprWasFloat_ = false;
}

// fixed_sub(a, b) -> Fixed
void NativeCodeGen::emitFixedSub(CallExpr& node) {
    node.args[0]->accept(*this);
    allocLocal("$fixed_a");
    asm_.mov_mem_rbp_rax(locals["$fixed_a"]);
    
    node.args[1]->accept(*this);
    // mov rcx, rax (b)
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x89);
    asm_.code.push_back(0xC1);
    
    asm_.mov_rax_mem_rbp(locals["$fixed_a"]);
    
    // sub rax, rcx
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x29);
    asm_.code.push_back(0xC8);
    
    lastExprWasFloat_ = false;
}

// fixed_mul(a, b) -> Fixed
void NativeCodeGen::emitFixedMul(CallExpr& node) {
    node.args[0]->accept(*this);
    allocLocal("$fixed_a");
    asm_.mov_mem_rbp_rax(locals["$fixed_a"]);
    
    node.args[1]->accept(*this);
    // mov rcx, rax (b in rcx)
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x89);
    asm_.code.push_back(0xC1);
    
    asm_.mov_rax_mem_rbp(locals["$fixed_a"]);
    
    // imul rcx gives 128-bit result in rdx:rax
    // imul rcx
    asm_.code.push_back(0x48);
    asm_.code.push_back(0xF7);
    asm_.code.push_back(0xE9);
    
    // We need bits [95:32] of the 128-bit result
    // That's the high 32 bits of rax and low 32 bits of rdx
    // shrd rax, rdx, 32 - shift right double
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0xAC);
    asm_.code.push_back(0xD0);
    asm_.code.push_back(0x20);
    
    lastExprWasFloat_ = false;
}

// fixed_to_float(f) -> float
void NativeCodeGen::emitFixedToFloat(CallExpr& node) {
    node.args[0]->accept(*this);
    
    // cvtsi2sd xmm0, rax
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x2A);
    asm_.code.push_back(0xC0);
    
    // Divide by 2^32
    uint64_t scale = 0x41F0000000000000ULL;  // 2^32 as double
    
    // mov rax, scale
    asm_.code.push_back(0x48);
    asm_.code.push_back(0xB8);
    for (int i = 0; i < 8; i++) {
        asm_.code.push_back((scale >> (i * 8)) & 0xFF);
    }
    
    // movq xmm1, rax
    asm_.code.push_back(0x66);
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x6E);
    asm_.code.push_back(0xC8);
    
    // divsd xmm0, xmm1
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x5E);
    asm_.code.push_back(0xC1);
    
    lastExprWasFloat_ = true;
}

// ============================================================================
// Vec3 Operations
// Vec3 stored as 3 consecutive doubles (24 bytes)
// ============================================================================

// vec3(x, y, z) -> Vec3
void NativeCodeGen::emitVec3New(CallExpr& node) {
    // Allocate 24 bytes for 3 doubles
    emitGCAllocRaw(24);
    allocLocal("$vec3_ptr");
    asm_.mov_mem_rbp_rax(locals["$vec3_ptr"]);
    
    // Evaluate and store x
    node.args[0]->accept(*this);
    if (!lastExprWasFloat_) {
        // cvtsi2sd xmm0, rax
        asm_.code.push_back(0xF2);
        asm_.code.push_back(0x48);
        asm_.code.push_back(0x0F);
        asm_.code.push_back(0x2A);
        asm_.code.push_back(0xC0);
    }
    asm_.mov_rax_mem_rbp(locals["$vec3_ptr"]);
    // movsd [rax], xmm0
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x11);
    asm_.code.push_back(0x00);
    
    // Evaluate and store y
    node.args[1]->accept(*this);
    if (!lastExprWasFloat_) {
        asm_.code.push_back(0xF2);
        asm_.code.push_back(0x48);
        asm_.code.push_back(0x0F);
        asm_.code.push_back(0x2A);
        asm_.code.push_back(0xC0);
    }
    asm_.mov_rax_mem_rbp(locals["$vec3_ptr"]);
    // movsd [rax+8], xmm0
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x11);
    asm_.code.push_back(0x40);
    asm_.code.push_back(0x08);
    
    // Evaluate and store z
    node.args[2]->accept(*this);
    if (!lastExprWasFloat_) {
        asm_.code.push_back(0xF2);
        asm_.code.push_back(0x48);
        asm_.code.push_back(0x0F);
        asm_.code.push_back(0x2A);
        asm_.code.push_back(0xC0);
    }
    asm_.mov_rax_mem_rbp(locals["$vec3_ptr"]);
    // movsd [rax+16], xmm0
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x11);
    asm_.code.push_back(0x40);
    asm_.code.push_back(0x10);
    
    lastExprWasFloat_ = false;
}

// vec3_add(a, b) -> Vec3
void NativeCodeGen::emitVec3Add(CallExpr& node) {
    // Evaluate first vector
    node.args[0]->accept(*this);
    allocLocal("$vec3_a");
    asm_.mov_mem_rbp_rax(locals["$vec3_a"]);
    
    // Evaluate second vector
    node.args[1]->accept(*this);
    allocLocal("$vec3_b");
    asm_.mov_mem_rbp_rax(locals["$vec3_b"]);
    
    // Allocate result
    emitGCAllocRaw(24);
    allocLocal("$vec3_result");
    asm_.mov_mem_rbp_rax(locals["$vec3_result"]);
    
    // Load and add x components
    asm_.mov_rax_mem_rbp(locals["$vec3_a"]);
    // movsd xmm0, [rax]
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x10);
    asm_.code.push_back(0x00);
    
    asm_.mov_rax_mem_rbp(locals["$vec3_b"]);
    // movsd xmm1, [rax]
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x10);
    asm_.code.push_back(0x08);
    
    // addsd xmm0, xmm1
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x58);
    asm_.code.push_back(0xC1);
    
    asm_.mov_rax_mem_rbp(locals["$vec3_result"]);
    // movsd [rax], xmm0
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x11);
    asm_.code.push_back(0x00);
    
    // Load and add y components
    asm_.mov_rax_mem_rbp(locals["$vec3_a"]);
    // movsd xmm0, [rax+8]
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x10);
    asm_.code.push_back(0x40);
    asm_.code.push_back(0x08);
    
    asm_.mov_rax_mem_rbp(locals["$vec3_b"]);
    // movsd xmm1, [rax+8]
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x10);
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x08);

    // addsd xmm0, xmm1
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x58);
    asm_.code.push_back(0xC1);
    
    asm_.mov_rax_mem_rbp(locals["$vec3_result"]);
    // movsd [rax+8], xmm0
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x11);
    asm_.code.push_back(0x40);
    asm_.code.push_back(0x08);
    
    // Load and add z components
    asm_.mov_rax_mem_rbp(locals["$vec3_a"]);
    // movsd xmm0, [rax+16]
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x10);
    asm_.code.push_back(0x40);
    asm_.code.push_back(0x10);
    
    asm_.mov_rax_mem_rbp(locals["$vec3_b"]);
    // movsd xmm1, [rax+16]
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x10);
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x10);
    
    // addsd xmm0, xmm1
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x58);
    asm_.code.push_back(0xC1);
    
    asm_.mov_rax_mem_rbp(locals["$vec3_result"]);
    // movsd [rax+16], xmm0
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x11);
    asm_.code.push_back(0x40);
    asm_.code.push_back(0x10);
    
    lastExprWasFloat_ = false;
}

// vec3_dot(a, b) -> float
void NativeCodeGen::emitVec3Dot(CallExpr& node) {
    // Evaluate first vector
    node.args[0]->accept(*this);
    allocLocal("$vec3_a");
    asm_.mov_mem_rbp_rax(locals["$vec3_a"]);
    
    // Evaluate second vector
    node.args[1]->accept(*this);
    allocLocal("$vec3_b");
    asm_.mov_mem_rbp_rax(locals["$vec3_b"]);

    // x*x
    asm_.mov_rax_mem_rbp(locals["$vec3_a"]);
    // movsd xmm0, [rax]
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x10);
    asm_.code.push_back(0x00);
    
    asm_.mov_rax_mem_rbp(locals["$vec3_b"]);
    // movsd xmm1, [rax]
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x10);
    asm_.code.push_back(0x08);
    
    // mulsd xmm0, xmm1
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x59);
    asm_.code.push_back(0xC1);
    
    // Save x*x in xmm2
    // movsd xmm2, xmm0
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x10);
    asm_.code.push_back(0xD0);
    
    // y*y
    asm_.mov_rax_mem_rbp(locals["$vec3_a"]);
    // movsd xmm0, [rax+8]
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x10);
    asm_.code.push_back(0x40);
    asm_.code.push_back(0x08);
    
    asm_.mov_rax_mem_rbp(locals["$vec3_b"]);
    // movsd xmm1, [rax+8]
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x10);
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x08);
    
    // mulsd xmm0, xmm1
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x59);
    asm_.code.push_back(0xC1);
    
    // addsd xmm2, xmm0
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x58);
    asm_.code.push_back(0xD0);

    // z*z
    asm_.mov_rax_mem_rbp(locals["$vec3_a"]);
    // movsd xmm0, [rax+16]
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x10);
    asm_.code.push_back(0x40);
    asm_.code.push_back(0x10);
    
    asm_.mov_rax_mem_rbp(locals["$vec3_b"]);
    // movsd xmm1, [rax+16]
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x10);
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x10);
    
    // mulsd xmm0, xmm1
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x59);
    asm_.code.push_back(0xC1);
    
    // addsd xmm2, xmm0
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x58);
    asm_.code.push_back(0xD0);
    
    // movsd xmm0, xmm2 (result)
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x10);
    asm_.code.push_back(0xC2);
    
    lastExprWasFloat_ = true;
}

// vec3_length(v) -> float
void NativeCodeGen::emitVec3Length(CallExpr& node) {
    node.args[0]->accept(*this);
    
    // Load x: movsd xmm0, [rax]
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x10);
    asm_.code.push_back(0x00);
    
    // mulsd xmm0, xmm0 (x^2)
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x59);
    asm_.code.push_back(0xC0);

    // Save x^2 in xmm2
    // movsd xmm2, xmm0
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x10);
    asm_.code.push_back(0xD0);
    
    // Need to reload rax since it may have been clobbered
    node.args[0]->accept(*this);
    
    // Load y: movsd xmm0, [rax+8]
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x10);
    asm_.code.push_back(0x40);
    asm_.code.push_back(0x08);
    
    // mulsd xmm0, xmm0 (y^2)
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x59);
    asm_.code.push_back(0xC0);
    
    // addsd xmm2, xmm0
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x58);
    asm_.code.push_back(0xD0);
    
    // Reload rax
    node.args[0]->accept(*this);
    
    // Load z: movsd xmm0, [rax+16]
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x10);
    asm_.code.push_back(0x40);
    asm_.code.push_back(0x10);
    
    // mulsd xmm0, xmm0 (z^2)
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x59);
    asm_.code.push_back(0xC0);
    
    // addsd xmm2, xmm0
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x58);
    asm_.code.push_back(0xD0);
    
    // sqrtsd xmm0, xmm2
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x51);
    asm_.code.push_back(0xC2);
    
    lastExprWasFloat_ = true;
}

} // namespace tyl
