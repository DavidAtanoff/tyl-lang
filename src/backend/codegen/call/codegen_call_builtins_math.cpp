// Tyl Compiler - Native Code Generator Math Builtin Calls
// Handles: abs, min, max, sqrt, sin, cos, tan, pow, floor, ceil, round

#include "backend/codegen/codegen_base.h"
#include <cmath>

namespace tyl {

// Math builtin implementations extracted from codegen_call_core.cpp

void NativeCodeGen::emitMathAbs(CallExpr& node) {
    // Check for constant evaluation
    int64_t intVal;
    if (tryEvalConstant(node.args[0].get(), intVal)) {
        asm_.mov_rax_imm64(intVal < 0 ? -intVal : intVal);
        lastExprWasFloat_ = false;
        return;
    }
    
    double floatVal;
    if (tryEvalConstantFloat(node.args[0].get(), floatVal)) {
        floatVal = std::abs(floatVal);
        uint32_t rva = addFloatConstant(floatVal);
        asm_.movsd_xmm0_mem_rip(rva);
        lastExprWasFloat_ = true;
        return;
    }
    
    // Runtime evaluation
    node.args[0]->accept(*this);
    
    if (isFloatExpression(node.args[0].get()) || lastExprWasFloat_) {
        // Float abs: clear sign bit
        // andpd xmm0, [abs_mask] where abs_mask = 0x7FFFFFFFFFFFFFFF
        asm_.code.push_back(0x48); asm_.code.push_back(0xB9);
        asm_.code.push_back(0xFF); asm_.code.push_back(0xFF);
        asm_.code.push_back(0xFF); asm_.code.push_back(0xFF);
        asm_.code.push_back(0xFF); asm_.code.push_back(0xFF);
        asm_.code.push_back(0xFF); asm_.code.push_back(0x7F);
        asm_.movq_xmm1_rcx();
        // andpd xmm0, xmm1
        asm_.code.push_back(0x66); asm_.code.push_back(0x0F);
        asm_.code.push_back(0x54); asm_.code.push_back(0xC1);
        lastExprWasFloat_ = true;
    } else {
        // Integer abs
        asm_.mov_rcx_rax();
        asm_.neg_rax();
        asm_.cmovl_rax_rcx();
        lastExprWasFloat_ = false;
    }
}

void NativeCodeGen::emitMathMin(CallExpr& node) {
    if (node.args.size() < 2) {
        node.args[0]->accept(*this);
        return;
    }
    
    // Check for constant evaluation
    int64_t val1, val2;
    if (tryEvalConstant(node.args[0].get(), val1) && tryEvalConstant(node.args[1].get(), val2)) {
        asm_.mov_rax_imm64(val1 < val2 ? val1 : val2);
        lastExprWasFloat_ = false;
        return;
    }
    
    double fval1, fval2;
    if (tryEvalConstantFloat(node.args[0].get(), fval1) && tryEvalConstantFloat(node.args[1].get(), fval2)) {
        double result = fval1 < fval2 ? fval1 : fval2;
        uint32_t rva = addFloatConstant(result);
        asm_.movsd_xmm0_mem_rip(rva);
        lastExprWasFloat_ = true;
        return;
    }
    
    // Runtime evaluation
    node.args[0]->accept(*this);
    asm_.push_rax();
    node.args[1]->accept(*this);
    asm_.mov_rcx_rax();
    asm_.pop_rax();
    
    // min: if rcx < rax, use rcx
    asm_.cmp_rax_rcx();
    asm_.cmovg_rax_rcx();
    lastExprWasFloat_ = false;
}

void NativeCodeGen::emitMathMax(CallExpr& node) {
    if (node.args.size() < 2) {
        node.args[0]->accept(*this);
        return;
    }
    
    // Check for constant evaluation
    int64_t val1, val2;
    if (tryEvalConstant(node.args[0].get(), val1) && tryEvalConstant(node.args[1].get(), val2)) {
        asm_.mov_rax_imm64(val1 > val2 ? val1 : val2);
        lastExprWasFloat_ = false;
        return;
    }
    
    double fval1, fval2;
    if (tryEvalConstantFloat(node.args[0].get(), fval1) && tryEvalConstantFloat(node.args[1].get(), fval2)) {
        double result = fval1 > fval2 ? fval1 : fval2;
        uint32_t rva = addFloatConstant(result);
        asm_.movsd_xmm0_mem_rip(rva);
        lastExprWasFloat_ = true;
        return;
    }
    
    // Runtime evaluation
    node.args[0]->accept(*this);
    asm_.push_rax();
    node.args[1]->accept(*this);
    asm_.mov_rcx_rax();
    asm_.pop_rax();
    
    // max: if rcx > rax, use rcx
    asm_.cmp_rax_rcx();
    asm_.cmovl_rax_rcx();
    lastExprWasFloat_ = false;
}

void NativeCodeGen::emitMathSqrt(CallExpr& node) {
    double floatVal;
    if (tryEvalConstantFloat(node.args[0].get(), floatVal)) {
        double result = std::sqrt(floatVal);
        uint32_t rva = addFloatConstant(result);
        asm_.movsd_xmm0_mem_rip(rva);
        lastExprWasFloat_ = true;
        return;
    }
    
    node.args[0]->accept(*this);
    
    if (!lastExprWasFloat_) {
        // Convert int to float
        asm_.cvtsi2sd_xmm0_rax();
    }
    
    asm_.sqrtsd_xmm0_xmm0();
    lastExprWasFloat_ = true;
}

void NativeCodeGen::emitMathFloor(CallExpr& node) {
    double floatVal;
    if (tryEvalConstantFloat(node.args[0].get(), floatVal)) {
        int64_t result = (int64_t)std::floor(floatVal);
        asm_.mov_rax_imm64(result);
        lastExprWasFloat_ = false;
        return;
    }
    
    node.args[0]->accept(*this);
    
    if (!lastExprWasFloat_) {
        asm_.cvtsi2sd_xmm0_rax();
    }
    
    // roundsd xmm0, xmm0, 1 (round toward -infinity)
    asm_.code.push_back(0x66); asm_.code.push_back(0x0F);
    asm_.code.push_back(0x3A); asm_.code.push_back(0x0B);
    asm_.code.push_back(0xC0); asm_.code.push_back(0x01);
    // Convert to int
    asm_.cvttsd2si_rax_xmm0();
    lastExprWasFloat_ = false;
}

void NativeCodeGen::emitMathCeil(CallExpr& node) {
    double floatVal;
    if (tryEvalConstantFloat(node.args[0].get(), floatVal)) {
        int64_t result = (int64_t)std::ceil(floatVal);
        asm_.mov_rax_imm64(result);
        lastExprWasFloat_ = false;
        return;
    }
    
    node.args[0]->accept(*this);
    
    if (!lastExprWasFloat_) {
        asm_.cvtsi2sd_xmm0_rax();
    }
    
    // roundsd xmm0, xmm0, 2 (round toward +infinity)
    asm_.code.push_back(0x66); asm_.code.push_back(0x0F);
    asm_.code.push_back(0x3A); asm_.code.push_back(0x0B);
    asm_.code.push_back(0xC0); asm_.code.push_back(0x02);
    // Convert to int
    asm_.cvttsd2si_rax_xmm0();
    lastExprWasFloat_ = false;
}

void NativeCodeGen::emitMathRound(CallExpr& node) {
    double floatVal;
    if (tryEvalConstantFloat(node.args[0].get(), floatVal)) {
        int64_t result = (int64_t)std::round(floatVal);
        asm_.mov_rax_imm64(result);
        lastExprWasFloat_ = false;
        return;
    }
    
    node.args[0]->accept(*this);
    
    if (!lastExprWasFloat_) {
        asm_.cvtsi2sd_xmm0_rax();
    }
    
    // roundsd xmm0, xmm0, 0 (round to nearest)
    asm_.code.push_back(0x66); asm_.code.push_back(0x0F);
    asm_.code.push_back(0x3A); asm_.code.push_back(0x0B);
    asm_.code.push_back(0xC0); asm_.code.push_back(0x00);
    // Convert to int
    asm_.cvttsd2si_rax_xmm0();
    lastExprWasFloat_ = false;
}

void NativeCodeGen::emitMathPow(CallExpr& node) {
    if (node.args.size() < 2) {
        node.args[0]->accept(*this);
        return;
    }
    
    // Check for constant evaluation
    double base, exp;
    if (tryEvalConstantFloat(node.args[0].get(), base) && 
        tryEvalConstantFloat(node.args[1].get(), exp)) {
        double result = std::pow(base, exp);
        uint32_t rva = addFloatConstant(result);
        asm_.movsd_xmm0_mem_rip(rva);
        lastExprWasFloat_ = true;
        return;
    }
    
    // Check for integer power with small constant exponent
    int64_t intExp;
    if (tryEvalConstant(node.args[1].get(), intExp) && intExp >= 0 && intExp <= 10) {
        node.args[0]->accept(*this);
        
        if (intExp == 0) {
            asm_.mov_rax_imm64(1);
            lastExprWasFloat_ = false;
            return;
        }
        if (intExp == 1) {
            // Result is already in rax
            return;
        }
        
        // Use repeated multiplication for small exponents
        asm_.mov_rcx_rax();
        for (int64_t i = 1; i < intExp; i++) {
            asm_.imul_rax_rcx();
        }
        lastExprWasFloat_ = false;
        return;
    }
    
    // General case - would need to call a runtime function
    // For now, just evaluate base
    node.args[0]->accept(*this);
}

} // namespace tyl
