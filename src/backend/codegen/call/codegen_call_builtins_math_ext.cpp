// Tyl Compiler - Extended Math Builtins for Native Code Generation
// Additional math functions ported from stdlib/math/math.cpp

#include "backend/codegen/codegen_base.h"
#include <cmath>

namespace tyl {

// sin(x) -> float - Sine
void NativeCodeGen::emitMathSin(CallExpr& node) {
    double floatVal;
    if (tryEvalConstantFloat(node.args[0].get(), floatVal)) {
        double result = std::sin(floatVal);
        uint32_t rva = addFloatConstant(result);
        asm_.movsd_xmm0_mem_rip(rva);
        lastExprWasFloat_ = true;
        return;
    }
    
    // Runtime - use x87 FPU for trig functions
    node.args[0]->accept(*this);
    if (!lastExprWasFloat_) {
        asm_.cvtsi2sd_xmm0_rax();
    }
    
    // Store xmm0 to stack, load to x87, compute sin, store back
    allocLocal("$sin_tmp");
    asm_.movsd_mem_rbp_xmm0(locals["$sin_tmp"]);
    asm_.code.push_back(0xDD); asm_.code.push_back(0x85); // fld qword [rbp+offset]
    int32_t offset = locals["$sin_tmp"];
    asm_.code.push_back(offset & 0xFF);
    asm_.code.push_back((offset >> 8) & 0xFF);
    asm_.code.push_back((offset >> 16) & 0xFF);
    asm_.code.push_back((offset >> 24) & 0xFF);
    asm_.code.push_back(0xD9); asm_.code.push_back(0xFE); // fsin
    asm_.code.push_back(0xDD); asm_.code.push_back(0x9D); // fstp qword [rbp+offset]
    asm_.code.push_back(offset & 0xFF);
    asm_.code.push_back((offset >> 8) & 0xFF);
    asm_.code.push_back((offset >> 16) & 0xFF);
    asm_.code.push_back((offset >> 24) & 0xFF);
    asm_.movsd_xmm0_mem_rbp(locals["$sin_tmp"]);
    lastExprWasFloat_ = true;
}

// cos(x) -> float - Cosine
void NativeCodeGen::emitMathCos(CallExpr& node) {
    double floatVal;
    if (tryEvalConstantFloat(node.args[0].get(), floatVal)) {
        double result = std::cos(floatVal);
        uint32_t rva = addFloatConstant(result);
        asm_.movsd_xmm0_mem_rip(rva);
        lastExprWasFloat_ = true;
        return;
    }
    
    node.args[0]->accept(*this);
    if (!lastExprWasFloat_) {
        asm_.cvtsi2sd_xmm0_rax();
    }
    
    allocLocal("$cos_tmp");
    asm_.movsd_mem_rbp_xmm0(locals["$cos_tmp"]);
    asm_.code.push_back(0xDD); asm_.code.push_back(0x85);
    int32_t offset = locals["$cos_tmp"];
    asm_.code.push_back(offset & 0xFF);
    asm_.code.push_back((offset >> 8) & 0xFF);
    asm_.code.push_back((offset >> 16) & 0xFF);
    asm_.code.push_back((offset >> 24) & 0xFF);
    asm_.code.push_back(0xD9); asm_.code.push_back(0xFF); // fcos
    asm_.code.push_back(0xDD); asm_.code.push_back(0x9D);
    asm_.code.push_back(offset & 0xFF);
    asm_.code.push_back((offset >> 8) & 0xFF);
    asm_.code.push_back((offset >> 16) & 0xFF);
    asm_.code.push_back((offset >> 24) & 0xFF);
    asm_.movsd_xmm0_mem_rbp(locals["$cos_tmp"]);
    lastExprWasFloat_ = true;
}

// tan(x) -> float - Tangent
void NativeCodeGen::emitMathTan(CallExpr& node) {
    double floatVal;
    if (tryEvalConstantFloat(node.args[0].get(), floatVal)) {
        double result = std::tan(floatVal);
        uint32_t rva = addFloatConstant(result);
        asm_.movsd_xmm0_mem_rip(rva);
        lastExprWasFloat_ = true;
        return;
    }
    
    node.args[0]->accept(*this);
    if (!lastExprWasFloat_) {
        asm_.cvtsi2sd_xmm0_rax();
    }
    
    allocLocal("$tan_tmp");
    asm_.movsd_mem_rbp_xmm0(locals["$tan_tmp"]);
    asm_.code.push_back(0xDD); asm_.code.push_back(0x85);
    int32_t offset = locals["$tan_tmp"];
    asm_.code.push_back(offset & 0xFF);
    asm_.code.push_back((offset >> 8) & 0xFF);
    asm_.code.push_back((offset >> 16) & 0xFF);
    asm_.code.push_back((offset >> 24) & 0xFF);
    asm_.code.push_back(0xD9); asm_.code.push_back(0xF2); // fptan
    asm_.code.push_back(0xDD); asm_.code.push_back(0xD8); // fstp st(0) - pop the 1.0
    asm_.code.push_back(0xDD); asm_.code.push_back(0x9D);
    asm_.code.push_back(offset & 0xFF);
    asm_.code.push_back((offset >> 8) & 0xFF);
    asm_.code.push_back((offset >> 16) & 0xFF);
    asm_.code.push_back((offset >> 24) & 0xFF);
    asm_.movsd_xmm0_mem_rbp(locals["$tan_tmp"]);
    lastExprWasFloat_ = true;
}

// exp(x) -> float - e^x
void NativeCodeGen::emitMathExp(CallExpr& node) {
    double floatVal;
    if (tryEvalConstantFloat(node.args[0].get(), floatVal)) {
        double result = std::exp(floatVal);
        uint32_t rva = addFloatConstant(result);
        asm_.movsd_xmm0_mem_rip(rva);
        lastExprWasFloat_ = true;
        return;
    }
    
    // Runtime - simplified, just return the input for now
    node.args[0]->accept(*this);
    if (!lastExprWasFloat_) {
        asm_.cvtsi2sd_xmm0_rax();
    }
    lastExprWasFloat_ = true;
}

// log(x) -> float - Natural logarithm
void NativeCodeGen::emitMathLog(CallExpr& node) {
    double floatVal;
    if (tryEvalConstantFloat(node.args[0].get(), floatVal)) {
        double result = std::log(floatVal);
        uint32_t rva = addFloatConstant(result);
        asm_.movsd_xmm0_mem_rip(rva);
        lastExprWasFloat_ = true;
        return;
    }
    
    node.args[0]->accept(*this);
    if (!lastExprWasFloat_) {
        asm_.cvtsi2sd_xmm0_rax();
    }
    lastExprWasFloat_ = true;
}

// trunc(x) -> int - Truncate towards zero
void NativeCodeGen::emitMathTrunc(CallExpr& node) {
    double floatVal;
    if (tryEvalConstantFloat(node.args[0].get(), floatVal)) {
        int64_t result = (int64_t)std::trunc(floatVal);
        asm_.mov_rax_imm64(result);
        lastExprWasFloat_ = false;
        return;
    }
    
    node.args[0]->accept(*this);
    if (lastExprWasFloat_) {
        // roundsd xmm0, xmm0, 3 (round toward zero)
        asm_.code.push_back(0x66); asm_.code.push_back(0x0F);
        asm_.code.push_back(0x3A); asm_.code.push_back(0x0B);
        asm_.code.push_back(0xC0); asm_.code.push_back(0x03);
        asm_.cvttsd2si_rax_xmm0();
    }
    lastExprWasFloat_ = false;
}

// sign(x) -> int - Returns -1, 0, or 1
void NativeCodeGen::emitMathSign(CallExpr& node) {
    int64_t intVal;
    if (tryEvalConstant(node.args[0].get(), intVal)) {
        int64_t result = intVal > 0 ? 1 : (intVal < 0 ? -1 : 0);
        asm_.mov_rax_imm64(result);
        lastExprWasFloat_ = false;
        return;
    }
    
    double floatVal;
    if (tryEvalConstantFloat(node.args[0].get(), floatVal)) {
        int64_t result = floatVal > 0 ? 1 : (floatVal < 0 ? -1 : 0);
        asm_.mov_rax_imm64(result);
        lastExprWasFloat_ = false;
        return;
    }
    
    node.args[0]->accept(*this);
    
    std::string negLabel = newLabel("sign_neg");
    std::string zeroLabel = newLabel("sign_zero");
    std::string doneLabel = newLabel("sign_done");
    
    if (lastExprWasFloat_) {
        asm_.cvttsd2si_rax_xmm0();
    }
    
    asm_.test_rax_rax();
    asm_.jz_rel32(zeroLabel);
    asm_.jl_rel32(negLabel);
    
    asm_.mov_rax_imm64(1);
    asm_.jmp_rel32(doneLabel);
    
    asm_.label(negLabel);
    asm_.mov_rax_imm64(-1);
    asm_.jmp_rel32(doneLabel);
    
    asm_.label(zeroLabel);
    asm_.xor_rax_rax();
    
    asm_.label(doneLabel);
    lastExprWasFloat_ = false;
}

// clamp(x, min, max) -> number - Clamp value to range
void NativeCodeGen::emitMathClamp(CallExpr& node) {
    int64_t x, lo, hi;
    if (tryEvalConstant(node.args[0].get(), x) &&
        tryEvalConstant(node.args[1].get(), lo) &&
        tryEvalConstant(node.args[2].get(), hi)) {
        int64_t result = x < lo ? lo : (x > hi ? hi : x);
        asm_.mov_rax_imm64(result);
        lastExprWasFloat_ = false;
        return;
    }
    
    // Runtime
    node.args[0]->accept(*this);
    asm_.push_rax();
    node.args[1]->accept(*this);
    asm_.push_rax();
    node.args[2]->accept(*this);
    asm_.mov_r8_rax();  // hi
    asm_.pop_rcx();     // lo
    asm_.pop_rax();     // x
    
    // if x < lo, x = lo
    asm_.cmp_rax_rcx();
    asm_.cmovl_rax_rcx();
    
    // if x > hi, x = hi
    asm_.code.push_back(0x4C); asm_.code.push_back(0x39); asm_.code.push_back(0xC0); // cmp rax, r8
    asm_.code.push_back(0x4C); asm_.code.push_back(0x0F); asm_.code.push_back(0x4F); asm_.code.push_back(0xC0); // cmovg rax, r8
    
    lastExprWasFloat_ = false;
}

// lerp(a, b, t) -> float - Linear interpolation
void NativeCodeGen::emitMathLerp(CallExpr& node) {
    double a, b, t;
    if (tryEvalConstantFloat(node.args[0].get(), a) &&
        tryEvalConstantFloat(node.args[1].get(), b) &&
        tryEvalConstantFloat(node.args[2].get(), t)) {
        double result = a + (b - a) * t;
        uint32_t rva = addFloatConstant(result);
        asm_.movsd_xmm0_mem_rip(rva);
        lastExprWasFloat_ = true;
        return;
    }
    
    // Runtime: a + (b - a) * t
    node.args[0]->accept(*this);
    if (!lastExprWasFloat_) asm_.cvtsi2sd_xmm0_rax();
    asm_.code.push_back(0xF2); asm_.code.push_back(0x0F); asm_.code.push_back(0x11);
    asm_.code.push_back(0x44); asm_.code.push_back(0x24); asm_.code.push_back(0xF8); // movsd [rsp-8], xmm0
    asm_.sub_rsp_imm32(8);
    
    node.args[1]->accept(*this);
    if (!lastExprWasFloat_) asm_.cvtsi2sd_xmm0_rax();
    asm_.code.push_back(0xF2); asm_.code.push_back(0x0F); asm_.code.push_back(0x11);
    asm_.code.push_back(0x44); asm_.code.push_back(0x24); asm_.code.push_back(0xF8);
    asm_.sub_rsp_imm32(8);
    
    node.args[2]->accept(*this);
    if (!lastExprWasFloat_) asm_.cvtsi2sd_xmm0_rax();
    // xmm0 = t
    
    asm_.add_rsp_imm32(8);
    asm_.code.push_back(0xF2); asm_.code.push_back(0x0F); asm_.code.push_back(0x10);
    asm_.code.push_back(0x0C); asm_.code.push_back(0x24); // movsd xmm1, [rsp] = b
    
    asm_.add_rsp_imm32(8);
    asm_.code.push_back(0xF2); asm_.code.push_back(0x0F); asm_.code.push_back(0x10);
    asm_.code.push_back(0x14); asm_.code.push_back(0x24); // movsd xmm2, [rsp] = a
    
    // xmm1 = b - a
    asm_.code.push_back(0xF2); asm_.code.push_back(0x0F); asm_.code.push_back(0x5C);
    asm_.code.push_back(0xCA); // subsd xmm1, xmm2
    
    // xmm0 = t * (b - a)
    asm_.code.push_back(0xF2); asm_.code.push_back(0x0F); asm_.code.push_back(0x59);
    asm_.code.push_back(0xC1); // mulsd xmm0, xmm1
    
    // xmm0 = a + t * (b - a)
    asm_.code.push_back(0xF2); asm_.code.push_back(0x0F); asm_.code.push_back(0x58);
    asm_.code.push_back(0xC2); // addsd xmm0, xmm2
    
    lastExprWasFloat_ = true;
}

// gcd(a, b) -> int - Greatest common divisor
void NativeCodeGen::emitMathGcd(CallExpr& node) {
    int64_t a, b;
    if (tryEvalConstant(node.args[0].get(), a) && tryEvalConstant(node.args[1].get(), b)) {
        a = std::abs(a);
        b = std::abs(b);
        while (b != 0) {
            int64_t t = b;
            b = a % b;
            a = t;
        }
        asm_.mov_rax_imm64(a);
        return;
    }
    
    // Runtime: Euclidean algorithm
    node.args[0]->accept(*this);
    asm_.push_rax();
    node.args[1]->accept(*this);
    asm_.mov_rcx_rax();
    asm_.pop_rax();
    
    // Make both positive
    asm_.code.push_back(0x48); asm_.code.push_back(0x99); // cqo
    asm_.code.push_back(0x48); asm_.code.push_back(0x31); asm_.code.push_back(0xD0); // xor rax, rdx
    asm_.code.push_back(0x48); asm_.code.push_back(0x29); asm_.code.push_back(0xD0); // sub rax, rdx
    
    asm_.push_rax();
    asm_.mov_rax_rcx();
    asm_.code.push_back(0x48); asm_.code.push_back(0x99);
    asm_.code.push_back(0x48); asm_.code.push_back(0x31); asm_.code.push_back(0xD0);
    asm_.code.push_back(0x48); asm_.code.push_back(0x29); asm_.code.push_back(0xD0);
    asm_.mov_rcx_rax();
    asm_.pop_rax();
    
    std::string loopLabel = newLabel("gcd_loop");
    std::string doneLabel = newLabel("gcd_done");
    
    asm_.label(loopLabel);
    // test rcx, rcx
    asm_.code.push_back(0x48); asm_.code.push_back(0x85); asm_.code.push_back(0xC9);
    asm_.jz_rel32(doneLabel);
    
    asm_.cqo();
    asm_.idiv_rcx();
    asm_.mov_rax_rcx();
    asm_.mov_rcx_rdx();
    asm_.jmp_rel32(loopLabel);
    
    asm_.label(doneLabel);
    lastExprWasFloat_ = false;
}

// lcm(a, b) -> int - Least common multiple
void NativeCodeGen::emitMathLcm(CallExpr& node) {
    int64_t a, b;
    if (tryEvalConstant(node.args[0].get(), a) && tryEvalConstant(node.args[1].get(), b)) {
        a = std::abs(a);
        b = std::abs(b);
        if (a == 0 || b == 0) {
            asm_.mov_rax_imm64(0);
            return;
        }
        int64_t ga = a, gb = b;
        while (gb != 0) {
            int64_t t = gb;
            gb = ga % gb;
            ga = t;
        }
        int64_t result = (a / ga) * b;
        asm_.mov_rax_imm64(result);
        return;
    }
    
    // Runtime - simplified, return a * b for now
    node.args[0]->accept(*this);
    asm_.push_rax();
    node.args[1]->accept(*this);
    asm_.mov_rcx_rax();
    asm_.pop_rax();
    asm_.imul_rax_rcx();
    lastExprWasFloat_ = false;
}

// factorial(n) -> int
void NativeCodeGen::emitMathFactorial(CallExpr& node) {
    int64_t n;
    if (tryEvalConstant(node.args[0].get(), n)) {
        if (n < 0) {
            asm_.mov_rax_imm64(0);
            return;
        }
        if (n > 20) {
            asm_.mov_rax_imm64(-1); // Overflow
            return;
        }
        int64_t result = 1;
        for (int64_t i = 2; i <= n; i++) result *= i;
        asm_.mov_rax_imm64(result);
        return;
    }
    
    // Runtime
    node.args[0]->accept(*this);
    asm_.mov_rcx_rax();
    asm_.mov_rax_imm64(1);
    
    std::string loopLabel = newLabel("fact_loop");
    std::string doneLabel = newLabel("fact_done");
    
    asm_.label(loopLabel);
    asm_.code.push_back(0x48); asm_.code.push_back(0x83); asm_.code.push_back(0xF9); asm_.code.push_back(0x01); // cmp rcx, 1
    asm_.jle_rel32(doneLabel);
    
    asm_.imul_rax_rcx();
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC9); // dec rcx
    asm_.jmp_rel32(loopLabel);
    
    asm_.label(doneLabel);
    lastExprWasFloat_ = false;
}

// fib(n) -> int - Fibonacci number
void NativeCodeGen::emitMathFib(CallExpr& node) {
    int64_t n;
    if (tryEvalConstant(node.args[0].get(), n)) {
        if (n <= 0) {
            asm_.mov_rax_imm64(0);
            return;
        }
        if (n == 1) {
            asm_.mov_rax_imm64(1);
            return;
        }
        int64_t a = 0, b = 1;
        for (int64_t i = 2; i <= n; i++) {
            int64_t t = a + b;
            a = b;
            b = t;
        }
        asm_.mov_rax_imm64(b);
        return;
    }
    
    // Runtime
    node.args[0]->accept(*this);
    asm_.mov_r8_rax();  // n
    
    std::string zeroLabel = newLabel("fib_zero");
    std::string oneLabel = newLabel("fib_one");
    std::string loopLabel = newLabel("fib_loop");
    std::string doneLabel = newLabel("fib_done");
    
    // test r8, r8
    asm_.code.push_back(0x4D); asm_.code.push_back(0x85); asm_.code.push_back(0xC0);
    asm_.jle_rel32(zeroLabel);
    
    asm_.code.push_back(0x49); asm_.code.push_back(0x83); asm_.code.push_back(0xF8); asm_.code.push_back(0x01); // cmp r8, 1
    asm_.code.push_back(0x74); asm_.code.push_back(0x00); // je oneLabel (will be patched)
    // Simplified - just compute iteratively
    asm_.xor_rax_rax();  // a = 0
    asm_.mov_rcx_imm64(1);  // b = 1
    asm_.mov_rdx_imm64(2);  // i = 2
    
    asm_.label(loopLabel);
    asm_.code.push_back(0x4C); asm_.code.push_back(0x39); asm_.code.push_back(0xC2); // cmp rdx, r8
    asm_.jg_rel32(doneLabel);
    
    // t = a + b
    asm_.code.push_back(0x4C); asm_.code.push_back(0x8D); asm_.code.push_back(0x04); asm_.code.push_back(0x08); // lea r8, [rax+rcx]
    asm_.mov_rax_rcx();  // a = b
    asm_.code.push_back(0x4C); asm_.code.push_back(0x89); asm_.code.push_back(0xC1); // mov rcx, r8 (b = t)
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC2); // inc rdx
    asm_.jmp_rel32(loopLabel);
    
    asm_.label(zeroLabel);
    asm_.xor_rax_rax();
    asm_.jmp_rel32(doneLabel);
    
    asm_.label(oneLabel);
    asm_.mov_rax_imm64(1);
    
    asm_.label(doneLabel);
    asm_.mov_rax_rcx();
    lastExprWasFloat_ = false;
}

// random() -> float - Random float between 0 and 1
void NativeCodeGen::emitMathRandom(CallExpr& node) {
    (void)node;
    // Use rdtsc for simple pseudo-random
    asm_.code.push_back(0x0F); asm_.code.push_back(0x31); // rdtsc
    asm_.code.push_back(0x48); asm_.code.push_back(0xC1); asm_.code.push_back(0xE2); asm_.code.push_back(0x20); // shl rdx, 32
    asm_.code.push_back(0x48); asm_.code.push_back(0x09); asm_.code.push_back(0xD0); // or rax, rdx
    
    // Convert to 0-1 range
    asm_.code.push_back(0x48); asm_.code.push_back(0x25); // and rax, 0x7FFFFFFF
    asm_.code.push_back(0xFF); asm_.code.push_back(0xFF); asm_.code.push_back(0xFF); asm_.code.push_back(0x7F);
    
    asm_.cvtsi2sd_xmm0_rax();
    
    // Divide by 2^31
    uint32_t divisorRva = addFloatConstant(2147483647.0);
    asm_.code.push_back(0xF2); asm_.code.push_back(0x0F); asm_.code.push_back(0x5E);
    asm_.code.push_back(0x05); // divsd xmm0, [rip+offset]
    asm_.ripFixups.push_back({asm_.code.size(), divisorRva});
    asm_.code.push_back(0x00); asm_.code.push_back(0x00);
    asm_.code.push_back(0x00); asm_.code.push_back(0x00);
    
    lastExprWasFloat_ = true;
}

// is_nan(x) -> bool
void NativeCodeGen::emitMathIsNan(CallExpr& node) {
    double floatVal;
    if (tryEvalConstantFloat(node.args[0].get(), floatVal)) {
        asm_.mov_rax_imm64(std::isnan(floatVal) ? 1 : 0);
        lastExprWasFloat_ = false;
        return;
    }
    
    node.args[0]->accept(*this);
    if (!lastExprWasFloat_) {
        asm_.xor_rax_rax(); // Integers are never NaN
        lastExprWasFloat_ = false;
        return;
    }
    
    // ucomisd xmm0, xmm0 - NaN != NaN
    asm_.code.push_back(0x66); asm_.code.push_back(0x0F); asm_.code.push_back(0x2E); asm_.code.push_back(0xC0);
    asm_.code.push_back(0x0F); asm_.code.push_back(0x9A); asm_.code.push_back(0xC0); // setp al
    asm_.movzx_rax_al();
    lastExprWasFloat_ = false;
}

// is_inf(x) -> bool
void NativeCodeGen::emitMathIsInf(CallExpr& node) {
    double floatVal;
    if (tryEvalConstantFloat(node.args[0].get(), floatVal)) {
        asm_.mov_rax_imm64(std::isinf(floatVal) ? 1 : 0);
        lastExprWasFloat_ = false;
        return;
    }
    
    node.args[0]->accept(*this);
    if (!lastExprWasFloat_) {
        asm_.xor_rax_rax(); // Integers are never infinite
        lastExprWasFloat_ = false;
        return;
    }
    
    // Check for infinity by comparing abs value with infinity
    asm_.movq_rax_xmm0();
    asm_.code.push_back(0x48); asm_.code.push_back(0x25); // and rax, 0x7FFFFFFFFFFFFFFF
    asm_.code.push_back(0xFF); asm_.code.push_back(0xFF); asm_.code.push_back(0xFF); asm_.code.push_back(0xFF);
    asm_.code.push_back(0xFF); asm_.code.push_back(0xFF); asm_.code.push_back(0xFF); asm_.code.push_back(0x7F);
    
    asm_.mov_rcx_imm64(0x7FF0000000000000LL); // Infinity bit pattern
    asm_.cmp_rax_rcx();
    asm_.code.push_back(0x0F); asm_.code.push_back(0x94); asm_.code.push_back(0xC0); // sete al
    asm_.movzx_rax_al();
    lastExprWasFloat_ = false;
}

} // namespace tyl
