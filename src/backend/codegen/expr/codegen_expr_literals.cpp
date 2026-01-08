// Tyl Compiler - Native Code Generator Expression Literals
// Handles: IntegerLiteral, FloatLiteral, StringLiteral, BoolLiteral, NilLiteral, Identifier

#include "backend/codegen/codegen_base.h"

namespace tyl {

void NativeCodeGen::visit(IntegerLiteral& node) {
    if (node.value == 0) {
        asm_.xor_rax_rax();
    } else if (node.value >= 0 && node.value <= 0x7FFFFFFF) {
        asm_.code.push_back(0xB8);
        asm_.code.push_back(node.value & 0xFF);
        asm_.code.push_back((node.value >> 8) & 0xFF);
        asm_.code.push_back((node.value >> 16) & 0xFF);
        asm_.code.push_back((node.value >> 24) & 0xFF);
    } else {
        asm_.mov_rax_imm64(node.value);
    }
    lastExprWasFloat_ = false;
}

void NativeCodeGen::visit(FloatLiteral& node) {
    union { double d; int64_t i; } u;
    u.d = node.value;
    asm_.mov_rax_imm64(u.i);
    asm_.movq_xmm0_rax();
    lastExprWasFloat_ = true;
}

void NativeCodeGen::visit(StringLiteral& node) {
    uint32_t rva = addString(node.value);
    asm_.lea_rax_rip_fixup(rva);
    lastExprWasFloat_ = false;
}

void NativeCodeGen::visit(CharLiteral& node) {
    // Character is stored as a 32-bit Unicode code point
    if (node.value == 0) {
        asm_.xor_rax_rax();
    } else if (node.value <= 0x7FFFFFFF) {
        asm_.code.push_back(0xB8);
        asm_.code.push_back(node.value & 0xFF);
        asm_.code.push_back((node.value >> 8) & 0xFF);
        asm_.code.push_back((node.value >> 16) & 0xFF);
        asm_.code.push_back((node.value >> 24) & 0xFF);
    } else {
        asm_.mov_rax_imm64(node.value);
    }
    lastExprWasFloat_ = false;
}

void NativeCodeGen::visit(ByteStringLiteral& node) {
    // Byte string is stored as a pointer to the byte array in .rdata
    // The array is null-terminated for convenience
    std::string byteStr(node.value.begin(), node.value.end());
    uint32_t rva = addString(byteStr);
    asm_.lea_rax_rip_fixup(rva);
    lastExprWasFloat_ = false;
}

void NativeCodeGen::visit(InterpolatedString& node) {
    // Try to evaluate as a constant string first
    std::string result;
    bool allConstant = true;
    
    for (auto& part : node.parts) {
        if (auto* str = std::get_if<std::string>(&part)) {
            result += *str;
        } else if (auto* exprPtr = std::get_if<ExprPtr>(&part)) {
            std::string strVal;
            int64_t intVal;
            if (tryEvalConstantString(exprPtr->get(), strVal)) {
                result += strVal;
            } else if (tryEvalConstant(exprPtr->get(), intVal)) {
                result += std::to_string(intVal);
            } else {
                allConstant = false;
                break;
            }
        }
    }
    
    if (allConstant) {
        uint32_t rva = addString(result);
        asm_.lea_rax_rip_fixup(rva);
    } else {
        // For runtime interpolated strings used as expressions (not in print),
        // we need to build the string at runtime. For now, just return the
        // constant parts concatenated - runtime values will need proper string
        // building support.
        // 
        // Note: This is a limitation. Full runtime string interpolation would
        // require allocating a buffer and concatenating parts at runtime.
        // For now, we evaluate what we can and return that.
        result.clear();
        for (auto& part : node.parts) {
            if (auto* str = std::get_if<std::string>(&part)) {
                result += *str;
            } else if (auto* exprPtr = std::get_if<ExprPtr>(&part)) {
                std::string strVal;
                int64_t intVal;
                if (tryEvalConstantString(exprPtr->get(), strVal)) {
                    result += strVal;
                } else if (tryEvalConstant(exprPtr->get(), intVal)) {
                    result += std::to_string(intVal);
                }
                // Skip runtime values - they can't be included in a static string
            }
        }
        uint32_t rva = addString(result);
        asm_.lea_rax_rip_fixup(rva);
    }
}

void NativeCodeGen::visit(BoolLiteral& node) {
    if (node.value) {
        asm_.code.push_back(0xB8);
        asm_.code.push_back(0x01);
        asm_.code.push_back(0x00);
        asm_.code.push_back(0x00);
        asm_.code.push_back(0x00);
    } else {
        asm_.xor_rax_rax();
    }
    lastExprWasFloat_ = false;
}

void NativeCodeGen::visit(NilLiteral& node) {
    (void)node;
    asm_.xor_rax_rax();
    lastExprWasFloat_ = false;
}

void NativeCodeGen::visit(Identifier& node) {
    // FIRST: Check if this is a function label (for function pointers)
    // This must be checked before register lookup because function names
    // might accidentally be in varRegisters_ due to register allocation
    bool inLabels = asm_.labels.count(node.name) > 0;
    bool inAllFn = allFunctionNames_.count(node.name) > 0;
    
    if (inLabels || inAllFn) {
        // Register the label if not already present
        if (asm_.labels.find(node.name) == asm_.labels.end()) {
            asm_.labels[node.name] = 0;
        }
        
        // Create a closure wrapper for the function pointer with a thunk
        // This allows uniform calling convention for both lambdas and function references
        // 
        // Problem: Lambdas use calling convention: RCX=closure, RDX=arg0, R8=arg1, R9=arg2
        // But regular functions use: RCX=arg0, RDX=arg1, R8=arg2, R9=arg3
        // 
        // Solution: Generate a thunk that shifts arguments:
        //   thunk: mov rcx, rdx    ; arg0 -> rcx
        //          mov rdx, r8     ; arg1 -> rdx  
        //          mov r8, r9      ; arg2 -> r8
        //          jmp target_fn
        
        // Check if we already have a thunk for this function
        std::string thunkLabel = "__thunk_" + node.name;
        if (asm_.labels.find(thunkLabel) == asm_.labels.end()) {
            // Generate the thunk
            std::string afterThunk = newLabel("after_thunk");
            asm_.jmp_rel32(afterThunk);
            
            asm_.label(thunkLabel);
            // mov rcx, rdx (shift arg0)
            asm_.code.push_back(0x48); asm_.code.push_back(0x89); asm_.code.push_back(0xD1);
            // mov rdx, r8 (shift arg1)
            asm_.code.push_back(0x4C); asm_.code.push_back(0x89); asm_.code.push_back(0xC2);
            // mov r8, r9 (shift arg2)
            asm_.code.push_back(0x4D); asm_.code.push_back(0x89); asm_.code.push_back(0xC8);
            // jmp to actual function
            asm_.jmp_rel32(node.name);
            
            asm_.label(afterThunk);
        }
        
        // Allocate closure (16 bytes minimum: fn_ptr + metadata)
        emitGCAllocClosure(0);  // 0 captures
        asm_.push_rax();  // Save closure pointer
        
        // Store thunk pointer at offset 0 (not the original function)
        asm_.code.push_back(0x48); asm_.code.push_back(0x8D); asm_.code.push_back(0x0D);
        asm_.fixupLabel(thunkLabel);  // lea rcx, [thunk_label]
        
        // mov [rax], rcx - store thunk ptr in closure
        asm_.code.push_back(0x48); asm_.code.push_back(0x8B);
        asm_.code.push_back(0x04); asm_.code.push_back(0x24);  // mov rax, [rsp]
        asm_.code.push_back(0x48); asm_.code.push_back(0x89);
        asm_.code.push_back(0x08);  // mov [rax], rcx
        
        asm_.pop_rax();  // Restore closure pointer as result
        lastExprWasFloat_ = false;
        return;
    }
    
    auto it = locals.find(node.name);
    auto regIt = varRegisters_.find(node.name);
    auto globalRegIt = globalVarRegisters_.find(node.name);
    
    // Check if variable is in a function-local register
    if (regIt != varRegisters_.end() && regIt->second != VarRegister::NONE) {
        if (floatVars.count(node.name)) {
            switch (regIt->second) {
                case VarRegister::RBX: asm_.mov_rax_rbx(); break;
                case VarRegister::R12: asm_.mov_rax_r12(); break;
                case VarRegister::R13: asm_.mov_rax_r13(); break;
                case VarRegister::R14: asm_.mov_rax_r14(); break;
                case VarRegister::R15: asm_.mov_rax_r15(); break;
                default: break;
            }
            asm_.movq_xmm0_rax();
            lastExprWasFloat_ = true;
        } else {
            switch (regIt->second) {
                case VarRegister::RBX: asm_.mov_rax_rbx(); break;
                case VarRegister::R12: asm_.mov_rax_r12(); break;
                case VarRegister::R13: asm_.mov_rax_r13(); break;
                case VarRegister::R14: asm_.mov_rax_r14(); break;
                case VarRegister::R15: asm_.mov_rax_r15(); break;
                default: break;
            }
            lastExprWasFloat_ = false;
        }
        return;
    }
    
    // Check if variable is in a global register
    if (globalRegIt != globalVarRegisters_.end() && globalRegIt->second != VarRegister::NONE) {
        if (floatVars.count(node.name)) {
            switch (globalRegIt->second) {
                case VarRegister::RBX: asm_.mov_rax_rbx(); break;
                case VarRegister::R12: asm_.mov_rax_r12(); break;
                case VarRegister::R13: asm_.mov_rax_r13(); break;
                case VarRegister::R14: asm_.mov_rax_r14(); break;
                case VarRegister::R15: asm_.mov_rax_r15(); break;
                default: break;
            }
            asm_.movq_xmm0_rax();
            lastExprWasFloat_ = true;
        } else {
            switch (globalRegIt->second) {
                case VarRegister::RBX: asm_.mov_rax_rbx(); break;
                case VarRegister::R12: asm_.mov_rax_r12(); break;
                case VarRegister::R13: asm_.mov_rax_r13(); break;
                case VarRegister::R14: asm_.mov_rax_r14(); break;
                case VarRegister::R15: asm_.mov_rax_r15(); break;
                default: break;
            }
            lastExprWasFloat_ = false;
        }
        return;
    }
    
    // Check if variable is on the stack
    if (it != locals.end()) {
        if (floatVars.count(node.name)) {
            asm_.movsd_xmm0_mem_rbp(it->second);
            asm_.movq_rax_xmm0();
            lastExprWasFloat_ = true;
        } else {
            asm_.mov_rax_mem_rbp(it->second);
            lastExprWasFloat_ = false;
        }
        return;
    }
    
    // Check for compile-time integer constants
    auto constIt = constVars.find(node.name);
    if (constIt != constVars.end()) {
        if (constIt->second == 0) {
            asm_.xor_rax_rax();
        } else if (constIt->second >= 0 && constIt->second <= 0x7FFFFFFF) {
            asm_.code.push_back(0xB8);
            asm_.code.push_back(constIt->second & 0xFF);
            asm_.code.push_back((constIt->second >> 8) & 0xFF);
            asm_.code.push_back((constIt->second >> 16) & 0xFF);
            asm_.code.push_back((constIt->second >> 24) & 0xFF);
        } else {
            asm_.mov_rax_imm64(constIt->second);
        }
        lastExprWasFloat_ = false;
        return;
    }
    
    // Check for compile-time float constants
    if (constFloatVars.count(node.name)) {
        union { double d; int64_t i; } u;
        u.d = constFloatVars[node.name];
        asm_.mov_rax_imm64(u.i);
        asm_.movq_xmm0_rax();
        lastExprWasFloat_ = true;
        return;
    }
    
    // Unknown identifier - return 0
    asm_.xor_rax_rax();
    lastExprWasFloat_ = false;
}

} // namespace tyl
