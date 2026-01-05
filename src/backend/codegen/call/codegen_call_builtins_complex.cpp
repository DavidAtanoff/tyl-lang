// Tyl Compiler - Complex Number Builtins
// Implements: complex(), real(), imag()

#include "backend/codegen/codegen_base.h"

namespace tyl {

// complex(real, imag) - Create a complex number from real and imaginary parts
// Returns a pointer to a 16-byte structure containing two doubles
void NativeCodeGen::emitComplexCreate(CallExpr& node) {
    // Allocate 16 bytes on stack for the complex number (two doubles)
    // Stack layout: [real: 8 bytes][imag: 8 bytes]
    
    // Evaluate real part first
    node.args[0]->accept(*this);
    
    // If it's a float, it's in xmm0, otherwise convert from rax
    if (lastExprWasFloat_) {
        // movsd [rsp-16], xmm0  - store real part
        asm_.sub_rsp_imm32(16);
        asm_.code.push_back(0xF2);  // MOVSD prefix
        asm_.code.push_back(0x0F);
        asm_.code.push_back(0x11);
        asm_.code.push_back(0x04);  // [rsp]
        asm_.code.push_back(0x24);
    } else {
        // Convert integer to double
        asm_.sub_rsp_imm32(16);
        // cvtsi2sd xmm0, rax
        asm_.code.push_back(0xF2);
        asm_.code.push_back(0x48);
        asm_.code.push_back(0x0F);
        asm_.code.push_back(0x2A);
        asm_.code.push_back(0xC0);  // xmm0, rax
        // movsd [rsp], xmm0
        asm_.code.push_back(0xF2);
        asm_.code.push_back(0x0F);
        asm_.code.push_back(0x11);
        asm_.code.push_back(0x04);
        asm_.code.push_back(0x24);
    }
    
    // Evaluate imaginary part
    node.args[1]->accept(*this);
    
    if (lastExprWasFloat_) {
        // movsd [rsp+8], xmm0  - store imag part
        asm_.code.push_back(0xF2);
        asm_.code.push_back(0x0F);
        asm_.code.push_back(0x11);
        asm_.code.push_back(0x44);  // [rsp+disp8]
        asm_.code.push_back(0x24);
        asm_.code.push_back(0x08);  // +8
    } else {
        // Convert integer to double
        // cvtsi2sd xmm0, rax
        asm_.code.push_back(0xF2);
        asm_.code.push_back(0x48);
        asm_.code.push_back(0x0F);
        asm_.code.push_back(0x2A);
        asm_.code.push_back(0xC0);
        // movsd [rsp+8], xmm0
        asm_.code.push_back(0xF2);
        asm_.code.push_back(0x0F);
        asm_.code.push_back(0x11);
        asm_.code.push_back(0x44);
        asm_.code.push_back(0x24);
        asm_.code.push_back(0x08);
    }
    
    // Return pointer to the complex number (rsp)
    // lea rax, [rsp]
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x8D);
    asm_.code.push_back(0x04);
    asm_.code.push_back(0x24);
    
    lastExprWasFloat_ = false;
    lastExprWasComplex_ = true;
}

// real(complex) - Extract real part from complex number
void NativeCodeGen::emitComplexReal(CallExpr& node) {
    // Evaluate the complex number (returns pointer in rax)
    node.args[0]->accept(*this);
    
    // movsd xmm0, [rax]  - load real part (first 8 bytes)
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x10);
    asm_.code.push_back(0x00);  // xmm0, [rax]
    
    lastExprWasFloat_ = true;
    lastExprWasComplex_ = false;
}

// imag(complex) - Extract imaginary part from complex number
void NativeCodeGen::emitComplexImag(CallExpr& node) {
    // Evaluate the complex number (returns pointer in rax)
    node.args[0]->accept(*this);
    
    // movsd xmm0, [rax+8]  - load imag part (second 8 bytes)
    asm_.code.push_back(0xF2);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x10);
    asm_.code.push_back(0x40);  // xmm0, [rax+disp8]
    asm_.code.push_back(0x08);  // +8
    
    lastExprWasFloat_ = true;
    lastExprWasComplex_ = false;
}

} // namespace tyl
