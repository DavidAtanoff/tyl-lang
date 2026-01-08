// Tyl Compiler - Native Code Generator Call Dispatch Helpers
// Handles: standard function calls, float function calls, function pointer calls, closure calls

#include "backend/codegen/codegen_base.h"

namespace tyl {

void NativeCodeGen::emitStandardFunctionCall(CallExpr& node, const std::string& callTarget) {
    for (int i = (int)node.args.size() - 1; i >= 0; i--) {
        node.args[i]->accept(*this);
        asm_.push_rax();
    }
    
    if (node.args.size() >= 1) asm_.pop_rcx();
    if (node.args.size() >= 2) asm_.pop_rdx();
    if (node.args.size() >= 3) {
        asm_.code.push_back(0x41); asm_.code.push_back(0x58); // pop r8
    }
    if (node.args.size() >= 4) {
        asm_.code.push_back(0x41); asm_.code.push_back(0x59); // pop r9
    }
    
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x20);
    asm_.call_rel32(callTarget);
    if (!stackAllocated_) asm_.add_rsp_imm32(0x20);
}

void NativeCodeGen::emitFloatFunctionCall(CallExpr& node, const std::string& callTarget) {
    // Push arguments in reverse order
    for (int i = (int)node.args.size() - 1; i >= 0; i--) {
        if (isFloatExpression(node.args[i].get())) {
            node.args[i]->accept(*this);
            asm_.movq_rax_xmm0();
            asm_.push_rax();
        } else {
            node.args[i]->accept(*this);
            asm_.push_rax();
        }
    }
    
    // Pop into correct registers (XMM for floats, GP for others)
    for (size_t i = 0; i < node.args.size() && i < 4; i++) {
        bool isFloat = isFloatExpression(node.args[i].get());
        
        if (isFloat) {
            asm_.pop_rax();
            switch (i) {
                case 0: 
                    asm_.movq_xmm0_rax(); 
                    break;
                case 1:
                    // movq xmm1, rax
                    asm_.code.push_back(0x66); asm_.code.push_back(0x48);
                    asm_.code.push_back(0x0F); asm_.code.push_back(0x6E); asm_.code.push_back(0xC8);
                    break;
                case 2:
                    // movq xmm2, rax
                    asm_.code.push_back(0x66); asm_.code.push_back(0x48);
                    asm_.code.push_back(0x0F); asm_.code.push_back(0x6E); asm_.code.push_back(0xD0);
                    break;
                case 3:
                    // movq xmm3, rax
                    asm_.code.push_back(0x66); asm_.code.push_back(0x48);
                    asm_.code.push_back(0x0F); asm_.code.push_back(0x6E); asm_.code.push_back(0xD8);
                    break;
            }
        } else {
            switch (i) {
                case 0: asm_.pop_rcx(); break;
                case 1: asm_.pop_rdx(); break;
                case 2: asm_.code.push_back(0x41); asm_.code.push_back(0x58); break; // pop r8
                case 3: asm_.code.push_back(0x41); asm_.code.push_back(0x59); break; // pop r9
            }
        }
    }
    
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x20);
    asm_.call_rel32(callTarget);
    if (!stackAllocated_) asm_.add_rsp_imm32(0x20);
    
    // Result is in xmm0, move to rax as bit pattern
    asm_.code.push_back(0x66); asm_.code.push_back(0x48);
    asm_.code.push_back(0x0F); asm_.code.push_back(0x7E); asm_.code.push_back(0xC0); // movq rax, xmm0
    lastExprWasFloat_ = true;
}

void NativeCodeGen::emitFunctionPointerCall(CallExpr& node, const std::string& varName) {
    // Load function pointer/closure from variable
    auto regIt = varRegisters_.find(varName);
    auto globalRegIt = globalVarRegisters_.find(varName);
    auto localIt = locals.find(varName);
    
    if (regIt != varRegisters_.end() && regIt->second != VarRegister::NONE) {
        switch (regIt->second) {
            case VarRegister::RBX: asm_.mov_rax_rbx(); break;
            case VarRegister::R12: asm_.mov_rax_r12(); break;
            case VarRegister::R13: asm_.mov_rax_r13(); break;
            case VarRegister::R14: asm_.mov_rax_r14(); break;
            case VarRegister::R15: asm_.mov_rax_r15(); break;
            default: break;
        }
    } else if (globalRegIt != globalVarRegisters_.end() && globalRegIt->second != VarRegister::NONE) {
        switch (globalRegIt->second) {
            case VarRegister::RBX: asm_.mov_rax_rbx(); break;
            case VarRegister::R12: asm_.mov_rax_r12(); break;
            case VarRegister::R13: asm_.mov_rax_r13(); break;
            case VarRegister::R14: asm_.mov_rax_r14(); break;
            case VarRegister::R15: asm_.mov_rax_r15(); break;
            default: break;
        }
    } else if (localIt != locals.end()) {
        asm_.mov_rax_mem_rbp(localIt->second);
    } else {
        asm_.xor_rax_rax();
    }
    
    // RAX now contains either:
    // 1. A closure pointer (heap object with fn ptr at offset 0)
    // 2. A raw function pointer (code address)
    // 
    // We need to handle both cases. Closures are heap-allocated and have
    // the function pointer at [rax]. Raw function pointers are just addresses.
    // 
    // Strategy: Load [rax] into a temp register. If it looks like a valid
    // code address (in the code section), use it. Otherwise, use rax directly.
    // 
    // Simpler approach: All function pointer parameters are now treated as
    // closures. When passing a raw function pointer, the caller wraps it.
    // The lambda calling convention is: RCX = closure ptr, RDX = arg0, R8 = arg1, R9 = arg2
    
    // Save closure pointer
    asm_.push_rax();
    
    // Push arguments in reverse order
    for (int i = (int)node.args.size() - 1; i >= 0; i--) {
        node.args[i]->accept(*this);
        asm_.push_rax();
    }
    
    // Pop arguments into registers (closure calling convention)
    // RCX = closure, RDX = arg0, R8 = arg1, R9 = arg2
    if (node.args.size() >= 1) asm_.pop_rdx();
    if (node.args.size() >= 2) {
        asm_.code.push_back(0x41); asm_.code.push_back(0x58); // pop r8
    }
    if (node.args.size() >= 3) {
        asm_.code.push_back(0x41); asm_.code.push_back(0x59); // pop r9
    }
    if (node.args.size() >= 4) {
        // Extra args would need stack passing
        asm_.pop_rax();
    }
    
    // Pop closure pointer into RCX
    asm_.pop_rcx();
    
    // Load function pointer from closure: mov rax, [rcx]
    asm_.mov_rax_mem_rcx();
    
    // Call through function pointer
    asm_.sub_rsp_imm32(0x20);
    asm_.call_rax();
    asm_.add_rsp_imm32(0x20);
}

void NativeCodeGen::emitClosureCall(CallExpr& node) {
    // Evaluate closure expression
    node.callee->accept(*this);
    asm_.push_rax();
    
    // Push arguments in reverse order
    for (int i = (int)node.args.size() - 1; i >= 0; i--) {
        node.args[i]->accept(*this);
        asm_.push_rax();
    }
    
    // Pop into argument registers (closure in RCX, args start at RDX)
    if (node.args.size() >= 1) asm_.pop_rdx();
    if (node.args.size() >= 2) {
        asm_.code.push_back(0x41); asm_.code.push_back(0x58); // pop r8
    }
    if (node.args.size() >= 3) {
        asm_.code.push_back(0x41); asm_.code.push_back(0x59); // pop r9
    }
    // Extra args are discarded (would need stack passing for more)
    for (size_t i = 3; i < node.args.size(); i++) {
        asm_.pop_rax();
    }
    
    // Pop closure pointer into RCX
    asm_.pop_rcx();
    
    // Load function pointer from closure (first field)
    asm_.mov_rax_mem_rcx();
    
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x20);
    asm_.call_rax();
    if (!stackAllocated_) asm_.add_rsp_imm32(0x20);
}

} // namespace tyl
