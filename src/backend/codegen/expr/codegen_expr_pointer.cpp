// Tyl Compiler - Native Code Generator Pointer Expressions
// Handles: AddressOfExpr, DerefExpr, NewExpr, CastExpr

#include "backend/codegen/codegen_base.h"

namespace tyl {

void NativeCodeGen::visit(AddressOfExpr& node) {
    if (auto* id = dynamic_cast<Identifier*>(node.operand.get())) {
        // Check if this is a function name
        if (asm_.labels.find(id->name) != asm_.labels.end()) {
            auto callbackIt = callbacks_.find(id->name);
            if (callbackIt != callbacks_.end()) {
                asm_.code.push_back(0x48); asm_.code.push_back(0x8D); asm_.code.push_back(0x05);
                asm_.fixupLabel(callbackIt->second.trampolineLabel);
                lastExprWasFloat_ = false;
                return;
            }
            
            asm_.code.push_back(0x48); asm_.code.push_back(0x8D); asm_.code.push_back(0x05);
            asm_.fixupLabel(id->name);
            lastExprWasFloat_ = false;
            return;
        }
        
        constVars.erase(id->name);
        constFloatVars.erase(id->name);
        
        auto regIt = varRegisters_.find(id->name);
        auto globalRegIt = globalVarRegisters_.find(id->name);
        auto localIt = locals.find(id->name);
        
        if (regIt != varRegisters_.end() && regIt->second != VarRegister::NONE) {
            if (localIt == locals.end()) {
                allocLocal(id->name);
                localIt = locals.find(id->name);
            }
            int32_t off = localIt->second;
            
            switch (regIt->second) {
                case VarRegister::RBX: asm_.mov_rax_rbx(); break;
                case VarRegister::R12: asm_.mov_rax_r12(); break;
                case VarRegister::R13: asm_.mov_rax_r13(); break;
                case VarRegister::R14: asm_.mov_rax_r14(); break;
                case VarRegister::R15: asm_.mov_rax_r15(); break;
                default: break;
            }
            asm_.mov_mem_rbp_rax(off);
            varRegisters_[id->name] = VarRegister::NONE;
            asm_.lea_rax_rbp(off);
        } else if (globalRegIt != globalVarRegisters_.end() && globalRegIt->second != VarRegister::NONE) {
            if (localIt == locals.end()) {
                allocLocal(id->name);
                localIt = locals.find(id->name);
            }
            int32_t off = localIt->second;
            
            switch (globalRegIt->second) {
                case VarRegister::RBX: asm_.mov_rax_rbx(); break;
                case VarRegister::R12: asm_.mov_rax_r12(); break;
                case VarRegister::R13: asm_.mov_rax_r13(); break;
                case VarRegister::R14: asm_.mov_rax_r14(); break;
                case VarRegister::R15: asm_.mov_rax_r15(); break;
                default: break;
            }
            asm_.mov_mem_rbp_rax(off);
            globalVarRegisters_[id->name] = VarRegister::NONE;
            asm_.lea_rax_rbp(off);
        } else if (localIt != locals.end()) {
            asm_.lea_rax_rbp(localIt->second);
        } else {
            allocLocal(id->name);
            asm_.lea_rax_rbp(locals[id->name]);
        }
    } else if (auto* indexExpr = dynamic_cast<IndexExpr*>(node.operand.get())) {
        indexExpr->index->accept(*this);
        asm_.dec_rax();
        asm_.push_rax();
        indexExpr->object->accept(*this);
        asm_.add_rax_imm32(16);
        asm_.pop_rcx();
        asm_.code.push_back(0x48); asm_.code.push_back(0xC1);
        asm_.code.push_back(0xE1); asm_.code.push_back(0x03);
        asm_.add_rax_rcx();
    } else if (auto* memberExpr = dynamic_cast<MemberExpr*>(node.operand.get())) {
        memberExpr->object->accept(*this);
    } else {
        node.operand->accept(*this);
    }
    
    lastExprWasFloat_ = false;
}

void NativeCodeGen::visit(BorrowExpr& node) {
    // BorrowExpr generates the same code as AddressOfExpr - get address of operand
    // The difference is in type checking (BorrowExpr is safe, AddressOfExpr requires unsafe)
    if (auto* id = dynamic_cast<Identifier*>(node.operand.get())) {
        auto localIt = locals.find(id->name);
        auto regIt = varRegisters_.find(id->name);
        auto globalRegIt = globalVarRegisters_.find(id->name);
        
        // If variable is in a register, spill it to stack first
        if (regIt != varRegisters_.end() && regIt->second != VarRegister::NONE) {
            if (localIt == locals.end()) {
                allocLocal(id->name);
                localIt = locals.find(id->name);
            }
            int32_t off = localIt->second;
            
            switch (regIt->second) {
                case VarRegister::RBX: asm_.mov_rax_rbx(); break;
                case VarRegister::R12: asm_.mov_rax_r12(); break;
                case VarRegister::R13: asm_.mov_rax_r13(); break;
                case VarRegister::R14: asm_.mov_rax_r14(); break;
                case VarRegister::R15: asm_.mov_rax_r15(); break;
                default: break;
            }
            asm_.mov_mem_rbp_rax(off);
            varRegisters_[id->name] = VarRegister::NONE;
            asm_.lea_rax_rbp(off);
        } else if (globalRegIt != globalVarRegisters_.end() && globalRegIt->second != VarRegister::NONE) {
            if (localIt == locals.end()) {
                allocLocal(id->name);
                localIt = locals.find(id->name);
            }
            int32_t off = localIt->second;
            
            switch (globalRegIt->second) {
                case VarRegister::RBX: asm_.mov_rax_rbx(); break;
                case VarRegister::R12: asm_.mov_rax_r12(); break;
                case VarRegister::R13: asm_.mov_rax_r13(); break;
                case VarRegister::R14: asm_.mov_rax_r14(); break;
                case VarRegister::R15: asm_.mov_rax_r15(); break;
                default: break;
            }
            asm_.mov_mem_rbp_rax(off);
            globalVarRegisters_[id->name] = VarRegister::NONE;
            asm_.lea_rax_rbp(off);
        } else if (localIt != locals.end()) {
            asm_.lea_rax_rbp(localIt->second);
        } else {
            allocLocal(id->name);
            asm_.lea_rax_rbp(locals[id->name]);
        }
    } else if (auto* indexExpr = dynamic_cast<IndexExpr*>(node.operand.get())) {
        // Borrow of list element
        indexExpr->index->accept(*this);
        asm_.dec_rax();
        asm_.push_rax();
        indexExpr->object->accept(*this);
        asm_.add_rax_imm32(16);
        asm_.pop_rcx();
        asm_.code.push_back(0x48); asm_.code.push_back(0xC1);
        asm_.code.push_back(0xE1); asm_.code.push_back(0x03);
        asm_.add_rax_rcx();
    } else {
        node.operand->accept(*this);
    }
    
    lastExprWasFloat_ = false;
}

void NativeCodeGen::visit(DerefExpr& node) {
    node.operand->accept(*this);
    asm_.mov_rax_mem_rax();
    lastExprWasFloat_ = false;
}

void NativeCodeGen::visit(NewExpr& node) {
    size_t size = 8;
    if (!node.args.empty()) {
        size = node.args.size() * 8;
    }
    
    emitGCAllocRaw(size);
    
    if (!node.args.empty()) {
        asm_.push_rax();
        for (size_t i = 0; i < node.args.size(); i++) {
            node.args[i]->accept(*this);
            asm_.push_rax();
            asm_.code.push_back(0x48); asm_.code.push_back(0x8B);
            asm_.code.push_back(0x4C); asm_.code.push_back(0x24);
            asm_.code.push_back((uint8_t)((node.args.size() - i) * 8));
            if (i > 0) {
                asm_.mov_rax_imm64(i * 8);
                asm_.add_rax_rcx();
                asm_.mov_rcx_rax();
            }
            asm_.pop_rax();
            asm_.mov_mem_rcx_rax();
        }
        asm_.pop_rax();
    }
}

void NativeCodeGen::visit(CastExpr& node) {
    node.expr->accept(*this);
    
    bool sourceIsFloat = lastExprWasFloat_;
    bool targetIsFloat = isFloatTypeName(node.targetType);
    bool targetIsInt = (node.targetType == "int" || node.targetType == "i8" || 
                        node.targetType == "i16" || node.targetType == "i32" || 
                        node.targetType == "i64" || node.targetType == "u8" ||
                        node.targetType == "u16" || node.targetType == "u32" ||
                        node.targetType == "u64");
    
    if (sourceIsFloat && targetIsInt) {
        asm_.cvttsd2si_rax_xmm0();
        lastExprWasFloat_ = false;
    } else if (!sourceIsFloat && targetIsFloat) {
        asm_.cvtsi2sd_xmm0_rax();
        lastExprWasFloat_ = true;
    } else if (targetIsFloat) {
        lastExprWasFloat_ = true;
    } else {
        lastExprWasFloat_ = false;
    }
}

} // namespace tyl
