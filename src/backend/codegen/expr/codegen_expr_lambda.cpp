// Tyl Compiler - Native Code Generator Lambda Expressions
// Handles: LambdaExpr

#include "backend/codegen/codegen_base.h"

namespace tyl {

void NativeCodeGen::visit(LambdaExpr& node) {
    std::string lambdaLabel = newLabel("lambda");
    std::string afterLambda = newLabel("after_lambda");
    
    std::set<std::string> paramNames;
    for (const auto& param : node.params) {
        paramNames.insert(param.first);
    }
    
    std::vector<std::string> capturedVars;
    std::set<std::string> capturedSet;
    collectCapturedVariables(node.body.get(), paramNames, capturedSet);
    
    for (const auto& varName : capturedSet) {
        bool inOuterScope = locals.count(varName) > 0 ||
                           varRegisters_.count(varName) > 0 ||
                           globalVarRegisters_.count(varName) > 0 ||
                           constVars.count(varName) > 0 ||
                           constFloatVars.count(varName) > 0;
        if (inOuterScope) {
            capturedVars.push_back(varName);
        }
    }
    
    bool hasCaptures = !capturedVars.empty();
    
    asm_.jmp_rel32(afterLambda);
    asm_.label(lambdaLabel);
    
    // Save context
    std::map<std::string, int32_t> savedLocals = locals;
    int32_t savedStackOffset = stackOffset;
    bool savedInFunction = inFunction;
    int32_t savedFunctionStackSize = functionStackSize_;
    bool savedStackAllocated = stackAllocated_;
    std::map<std::string, VarRegister> savedVarRegisters = varRegisters_;
    
    inFunction = true;
    locals.clear();
    stackOffset = 0;
    varRegisters_.clear();
    
    asm_.push_rbp();
    asm_.mov_rbp_rsp();
    
    functionStackSize_ = 0x40 + (hasCaptures ? (int32_t)(capturedVars.size() * 8 + 8) : 0);
    asm_.sub_rsp_imm32(functionStackSize_);
    stackAllocated_ = true;
    
    if (hasCaptures) {
        allocLocal("$closure_ptr");
        asm_.mov_mem_rbp_rcx(locals["$closure_ptr"]);
        
        for (size_t i = 0; i < capturedVars.size(); i++) {
            const std::string& varName = capturedVars[i];
            allocLocal(varName);
            int32_t off = locals[varName];
            
            asm_.mov_rax_mem_rbp(locals["$closure_ptr"]);
            int32_t captureOffset = 16 + static_cast<int32_t>(i * 8);
            asm_.add_rax_imm32(captureOffset);
            asm_.mov_rax_mem_rax();
            asm_.mov_mem_rbp_rax(off);
        }
    }
    
    // Store parameters
    for (size_t i = 0; i < node.params.size() && i < 3; i++) {
        const std::string& paramName = node.params[i].first;
        allocLocal(paramName);
        int32_t off = locals[paramName];
        
        switch (i) {
            case 0:
                asm_.code.push_back(0x48); asm_.code.push_back(0x89);
                asm_.code.push_back(0x95);
                asm_.code.push_back(off & 0xFF);
                asm_.code.push_back((off >> 8) & 0xFF);
                asm_.code.push_back((off >> 16) & 0xFF);
                asm_.code.push_back((off >> 24) & 0xFF);
                break;
            case 1:
                asm_.code.push_back(0x4C); asm_.code.push_back(0x89);
                asm_.code.push_back(0x85);
                asm_.code.push_back(off & 0xFF);
                asm_.code.push_back((off >> 8) & 0xFF);
                asm_.code.push_back((off >> 16) & 0xFF);
                asm_.code.push_back((off >> 24) & 0xFF);
                break;
            case 2:
                asm_.code.push_back(0x4C); asm_.code.push_back(0x89);
                asm_.code.push_back(0x8D);
                asm_.code.push_back(off & 0xFF);
                asm_.code.push_back((off >> 8) & 0xFF);
                asm_.code.push_back((off >> 16) & 0xFF);
                asm_.code.push_back((off >> 24) & 0xFF);
                break;
        }
    }
    
    node.body->accept(*this);
    
    asm_.add_rsp_imm32(functionStackSize_);
    asm_.pop_rbp();
    asm_.ret();
    
    // Restore context
    locals = savedLocals;
    stackOffset = savedStackOffset;
    inFunction = savedInFunction;
    functionStackSize_ = savedFunctionStackSize;
    stackAllocated_ = savedStackAllocated;
    varRegisters_ = savedVarRegisters;
    
    asm_.label(afterLambda);
    
    emitGCAllocClosure(capturedVars.size());
    asm_.push_rax();
    
    // Store function pointer
    asm_.code.push_back(0x48); asm_.code.push_back(0x8D); asm_.code.push_back(0x0D);
    asm_.fixupLabel(lambdaLabel);
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x89);
    asm_.code.push_back(0x08);
    
    // Store captured variables
    for (size_t i = 0; i < capturedVars.size(); i++) {
        const std::string& varName = capturedVars[i];
        
        auto regIt = varRegisters_.find(varName);
        auto globalRegIt = globalVarRegisters_.find(varName);
        auto localIt = locals.find(varName);
        auto constIt = constVars.find(varName);
        auto constFloatIt = constFloatVars.find(varName);
        
        if (constIt != constVars.end()) {
            asm_.mov_rcx_imm64(constIt->second);
        } else if (constFloatIt != constFloatVars.end()) {
            union { double d; int64_t i; } u;
            u.d = constFloatIt->second;
            asm_.mov_rcx_imm64(u.i);
        } else if (regIt != varRegisters_.end() && regIt->second != VarRegister::NONE) {
            switch (regIt->second) {
                case VarRegister::RBX: asm_.mov_rcx_rbx(); break;
                case VarRegister::R12: asm_.mov_rcx_r12(); break;
                case VarRegister::R13: asm_.mov_rcx_r13(); break;
                case VarRegister::R14: asm_.mov_rcx_r14(); break;
                case VarRegister::R15: asm_.mov_rcx_r15(); break;
                default: asm_.xor_ecx_ecx(); break;
            }
        } else if (globalRegIt != globalVarRegisters_.end() && globalRegIt->second != VarRegister::NONE) {
            switch (globalRegIt->second) {
                case VarRegister::RBX: asm_.mov_rcx_rbx(); break;
                case VarRegister::R12: asm_.mov_rcx_r12(); break;
                case VarRegister::R13: asm_.mov_rcx_r13(); break;
                case VarRegister::R14: asm_.mov_rcx_r14(); break;
                case VarRegister::R15: asm_.mov_rcx_r15(); break;
                default: asm_.xor_ecx_ecx(); break;
            }
        } else if (localIt != locals.end()) {
            asm_.mov_rcx_mem_rbp(localIt->second);
        } else {
            asm_.xor_ecx_ecx();
        }
        
        asm_.code.push_back(0x48); asm_.code.push_back(0x8B);
        asm_.code.push_back(0x04); asm_.code.push_back(0x24);
        
        int32_t captureOffset = 16 + static_cast<int32_t>(i * 8);
        asm_.code.push_back(0x48);
        asm_.code.push_back(0x89);
        asm_.code.push_back(0x48);
        asm_.code.push_back(static_cast<uint8_t>(captureOffset));
    }
    
    asm_.pop_rax();
    lastExprWasFloat_ = false;
}

} // namespace tyl
