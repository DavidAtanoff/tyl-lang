// Tyl Compiler - Native Code Generator Result Type Builtin Calls
// Handles: Ok, Err, is_ok, is_err, unwrap, unwrap_or

#include "backend/codegen/codegen_base.h"

namespace tyl {

void NativeCodeGen::emitResultOk(CallExpr& node) {
    node.args[0]->accept(*this);
    // Encode as (value << 1) | 1 to mark as Ok
    asm_.code.push_back(0x48); asm_.code.push_back(0xD1); asm_.code.push_back(0xE0); // shl rax, 1
    asm_.code.push_back(0x48); asm_.code.push_back(0x83); asm_.code.push_back(0xC8); asm_.code.push_back(0x01); // or rax, 1
}

void NativeCodeGen::emitResultErr(CallExpr& node) {
    node.args[0]->accept(*this);
    // Encode as (value << 1) | 0 to mark as Err
    asm_.code.push_back(0x48); asm_.code.push_back(0xD1); asm_.code.push_back(0xE0); // shl rax, 1
}

void NativeCodeGen::emitResultIsOk(CallExpr& node) {
    node.args[0]->accept(*this);
    // Check if lowest bit is 1 (Ok)
    asm_.code.push_back(0x48); asm_.code.push_back(0x83); asm_.code.push_back(0xE0); asm_.code.push_back(0x01); // and rax, 1
}

void NativeCodeGen::emitResultIsErr(CallExpr& node) {
    node.args[0]->accept(*this);
    // Check if lowest bit is 0 (Err), return inverted
    asm_.code.push_back(0x48); asm_.code.push_back(0x83); asm_.code.push_back(0xE0); asm_.code.push_back(0x01); // and rax, 1
    asm_.code.push_back(0x48); asm_.code.push_back(0x83); asm_.code.push_back(0xF0); asm_.code.push_back(0x01); // xor rax, 1
}

void NativeCodeGen::emitResultUnwrap(CallExpr& node) {
    node.args[0]->accept(*this);
    // Decode by shifting right
    asm_.code.push_back(0x48); asm_.code.push_back(0xD1); asm_.code.push_back(0xE8); // shr rax, 1
}

void NativeCodeGen::emitResultUnwrapOr(CallExpr& node) {
    node.args[0]->accept(*this);
    asm_.push_rax();
    
    // Check if Ok (lowest bit is 1)
    asm_.code.push_back(0x48); asm_.code.push_back(0x83); asm_.code.push_back(0xE0); asm_.code.push_back(0x01); // and rax, 1
    
    std::string okLabel = newLabel("unwrap_ok");
    std::string endLabel = newLabel("unwrap_end");
    
    asm_.test_rax_rax();
    asm_.jnz_rel32(okLabel);
    
    // Is Err - return default value
    asm_.pop_rax();
    node.args[1]->accept(*this);
    asm_.jmp_rel32(endLabel);
    
    asm_.label(okLabel);
    // Is Ok - unwrap the value
    asm_.pop_rax();
    asm_.code.push_back(0x48); asm_.code.push_back(0xD1); asm_.code.push_back(0xE8); // shr rax, 1
    
    asm_.label(endLabel);
}

} // namespace tyl
