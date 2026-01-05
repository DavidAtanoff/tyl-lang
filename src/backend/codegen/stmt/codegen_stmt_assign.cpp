// Tyl Compiler - Native Code Generator Assignment Statements
// Handles: AssignStmt, DestructuringDecl, ExprStmt

#include "backend/codegen/codegen_base.h"

namespace tyl {

void NativeCodeGen::visit(ExprStmt& node) {
    node.expr->accept(*this);
}

void NativeCodeGen::visit(DestructuringDecl& node) {
    if (node.kind == DestructuringDecl::Kind::TUPLE) {
        if (auto* list = dynamic_cast<ListExpr*>(node.initializer.get())) {
            for (size_t i = 0; i < node.names.size() && i < list->elements.size(); i++) {
                list->elements[i]->accept(*this);
                allocLocal(node.names[i]);
                asm_.mov_mem_rbp_rax(locals[node.names[i]]);
                
                int64_t val;
                if (tryEvalConstant(list->elements[i].get(), val)) {
                    constVars[node.names[i]] = val;
                }
            }
            return;
        }
    }
    
    if (node.kind == DestructuringDecl::Kind::RECORD) {
        if (auto* rec = dynamic_cast<RecordExpr*>(node.initializer.get())) {
            std::map<std::string, Expression*> fieldMap;
            for (auto& [name, expr] : rec->fields) {
                fieldMap[name] = expr.get();
            }
            
            for (const std::string& name : node.names) {
                auto it = fieldMap.find(name);
                if (it != fieldMap.end()) {
                    it->second->accept(*this);
                    
                    int64_t val;
                    if (tryEvalConstant(it->second, val)) {
                        constVars[name] = val;
                    }
                    std::string strVal;
                    if (tryEvalConstantString(it->second, strVal)) {
                        constStrVars[name] = strVal;
                    } else if (dynamic_cast<StringLiteral*>(it->second) ||
                               dynamic_cast<InterpolatedString*>(it->second)) {
                        constStrVars[name] = "";
                    }
                } else {
                    asm_.xor_rax_rax();
                }
                allocLocal(name);
                asm_.mov_mem_rbp_rax(locals[name]);
            }
            return;
        }
    }
    
    node.initializer->accept(*this);
    
    allocLocal("$destruct_base");
    asm_.mov_mem_rbp_rax(locals["$destruct_base"]);
    
    for (size_t i = 0; i < node.names.size(); i++) {
        asm_.mov_rax_mem_rbp(locals["$destruct_base"]);
        
        if (i > 0) {
            asm_.mov_rcx_imm64(i * 8);
            asm_.add_rax_rcx();
        }
        
        asm_.mov_rax_mem_rax();
        
        allocLocal(node.names[i]);
        asm_.mov_mem_rbp_rax(locals[node.names[i]]);
    }
}

void NativeCodeGen::visit(AssignStmt& node) {
    bool isFloat = false;
    
    if (auto* id = dynamic_cast<Identifier*>(node.target.get())) {
        isFloat = floatVars.count(id->name) > 0 || isFloatExpression(node.value.get());
        
        if (node.op == TokenType::ASSIGN) {
            if (isFloat) {
                double floatVal;
                if (tryEvalConstantFloat(node.value.get(), floatVal)) {
                    constFloatVars[id->name] = floatVal;
                } else {
                    constFloatVars.erase(id->name);
                }
                floatVars.insert(id->name);
            } else {
                int64_t intVal;
                if (tryEvalConstant(node.value.get(), intVal)) {
                    constVars[id->name] = intVal;
                } else {
                    constVars.erase(id->name);
                }
            }
            std::string strVal;
            if (tryEvalConstantString(node.value.get(), strVal)) {
                constStrVars[id->name] = strVal;
            } else if (isStringReturningExpr(node.value.get())) {
                constStrVars[id->name] = "";
            } else {
                constStrVars.erase(id->name);
            }
        } else {
            constVars.erase(id->name);
            constStrVars.erase(id->name);
            constFloatVars.erase(id->name);
        }
    }
    
    int64_t constVal;
    bool valueIsConst = tryEvalConstant(node.value.get(), constVal);
    bool valueIsSmall = valueIsConst && constVal >= INT32_MIN && constVal <= INT32_MAX;
    
    if (auto* id = dynamic_cast<Identifier*>(node.target.get())) {
        emitIdentifierAssign(id, node, isFloat, valueIsConst, valueIsSmall, constVal);
    } else if (auto* deref = dynamic_cast<DerefExpr*>(node.target.get())) {
        emitDerefAssign(deref, node);
    } else if (auto* indexExpr = dynamic_cast<IndexExpr*>(node.target.get())) {
        emitIndexAssign(indexExpr, node);
    } else if (auto* member = dynamic_cast<MemberExpr*>(node.target.get())) {
        emitMemberAssign(member, node);
    } else {
        node.target->accept(*this);
        asm_.push_rax();
        node.value->accept(*this);
        asm_.pop_rcx();
        asm_.mov_mem_rcx_rax();
    }
}

void NativeCodeGen::emitIdentifierAssign(Identifier* id, AssignStmt& node, bool isFloat, 
                                          bool valueIsConst, bool valueIsSmall, int64_t constVal) {
    auto regIt = varRegisters_.find(id->name);
    auto globalRegIt = globalVarRegisters_.find(id->name);
    
    VarRegister reg = VarRegister::NONE;
    if (regIt != varRegisters_.end() && regIt->second != VarRegister::NONE) {
        reg = regIt->second;
    } else if (globalRegIt != globalVarRegisters_.end() && globalRegIt->second != VarRegister::NONE) {
        reg = globalRegIt->second;
    }
    
    if (reg != VarRegister::NONE) {
        emitRegisterAssign(reg, node, isFloat, valueIsConst, valueIsSmall, constVal);
        return;
    }
    
    auto it = locals.find(id->name);
    
    if (it != locals.end() && !isFloat && valueIsSmall && 
        (node.op == TokenType::PLUS_ASSIGN || node.op == TokenType::MINUS_ASSIGN)) {
        asm_.mov_rax_mem_rbp(it->second);
        if (node.op == TokenType::PLUS_ASSIGN) {
            asm_.code.push_back(0x48); asm_.code.push_back(0x05);
        } else {
            asm_.code.push_back(0x48); asm_.code.push_back(0x2D);
        }
        asm_.code.push_back(constVal & 0xFF);
        asm_.code.push_back((constVal >> 8) & 0xFF);
        asm_.code.push_back((constVal >> 16) & 0xFF);
        asm_.code.push_back((constVal >> 24) & 0xFF);
        asm_.mov_mem_rbp_rax(it->second);
        return;
    }
    
    node.value->accept(*this);
    
    if (it != locals.end()) {
        if (isFloat && lastExprWasFloat_) {
            emitFloatCompoundAssign(it->second, node.op);
        } else {
            emitIntCompoundAssign(it->second, node.op);
        }
    } else {
        allocLocal(id->name);
        if (isFloat && lastExprWasFloat_) {
            asm_.movsd_mem_rbp_xmm0(locals[id->name]);
        } else {
            asm_.mov_mem_rbp_rax(locals[id->name]);
        }
    }
}

void NativeCodeGen::emitRegisterAssign(VarRegister reg, AssignStmt& node, bool isFloat,
                                        bool valueIsConst, bool valueIsSmall, int64_t constVal) {
    if (!isFloat) {
        if (valueIsSmall && (node.op == TokenType::PLUS_ASSIGN || node.op == TokenType::MINUS_ASSIGN)) {
            switch (reg) {
                case VarRegister::RBX: asm_.mov_rax_rbx(); break;
                case VarRegister::R12: asm_.mov_rax_r12(); break;
                case VarRegister::R13: asm_.mov_rax_r13(); break;
                case VarRegister::R14: asm_.mov_rax_r14(); break;
                case VarRegister::R15: asm_.mov_rax_r15(); break;
                default: break;
            }
            
            if (node.op == TokenType::PLUS_ASSIGN) {
                asm_.code.push_back(0x48); asm_.code.push_back(0x05);
            } else {
                asm_.code.push_back(0x48); asm_.code.push_back(0x2D);
            }
            asm_.code.push_back(constVal & 0xFF);
            asm_.code.push_back((constVal >> 8) & 0xFF);
            asm_.code.push_back((constVal >> 16) & 0xFF);
            asm_.code.push_back((constVal >> 24) & 0xFF);
            
            switch (reg) {
                case VarRegister::RBX: asm_.mov_rbx_rax(); break;
                case VarRegister::R12: asm_.mov_r12_rax(); break;
                case VarRegister::R13: asm_.mov_r13_rax(); break;
                case VarRegister::R14: asm_.mov_r14_rax(); break;
                case VarRegister::R15: asm_.mov_r15_rax(); break;
                default: break;
            }
            return;
        }
        
        node.value->accept(*this);
        
        if (node.op == TokenType::PLUS_ASSIGN || node.op == TokenType::MINUS_ASSIGN ||
            node.op == TokenType::STAR_ASSIGN) {
            asm_.push_rax();
            switch (reg) {
                case VarRegister::RBX: asm_.mov_rax_rbx(); break;
                case VarRegister::R12: asm_.mov_rax_r12(); break;
                case VarRegister::R13: asm_.mov_rax_r13(); break;
                case VarRegister::R14: asm_.mov_rax_r14(); break;
                case VarRegister::R15: asm_.mov_rax_r15(); break;
                default: break;
            }
            asm_.pop_rcx();
            
            if (node.op == TokenType::PLUS_ASSIGN) asm_.add_rax_rcx();
            else if (node.op == TokenType::MINUS_ASSIGN) asm_.sub_rax_rcx();
            else if (node.op == TokenType::STAR_ASSIGN) asm_.imul_rax_rcx();
        } else if (node.op == TokenType::SLASH_ASSIGN) {
            asm_.mov_rcx_rax();
            switch (reg) {
                case VarRegister::RBX: asm_.mov_rax_rbx(); break;
                case VarRegister::R12: asm_.mov_rax_r12(); break;
                case VarRegister::R13: asm_.mov_rax_r13(); break;
                case VarRegister::R14: asm_.mov_rax_r14(); break;
                case VarRegister::R15: asm_.mov_rax_r15(); break;
                default: break;
            }
            asm_.cqo();
            asm_.idiv_rcx();
        }
        
        switch (reg) {
            case VarRegister::RBX: asm_.mov_rbx_rax(); break;
            case VarRegister::R12: asm_.mov_r12_rax(); break;
            case VarRegister::R13: asm_.mov_r13_rax(); break;
            case VarRegister::R14: asm_.mov_r14_rax(); break;
            case VarRegister::R15: asm_.mov_r15_rax(); break;
            default: break;
        }
        return;
    }
    
    node.value->accept(*this);
    if (isFloat && lastExprWasFloat_) {
        asm_.movq_rax_xmm0();
        switch (reg) {
            case VarRegister::RBX: asm_.mov_rbx_rax(); break;
            case VarRegister::R12: asm_.mov_r12_rax(); break;
            case VarRegister::R13: asm_.mov_r13_rax(); break;
            case VarRegister::R14: asm_.mov_r14_rax(); break;
            case VarRegister::R15: asm_.mov_r15_rax(); break;
            default: break;
        }
    }
}

void NativeCodeGen::emitFloatCompoundAssign(int32_t offset, TokenType op) {
    if (op == TokenType::PLUS_ASSIGN) {
        asm_.movsd_xmm1_mem_rbp(offset);
        asm_.addsd_xmm0_xmm1();
    } else if (op == TokenType::MINUS_ASSIGN) {
        asm_.movsd_xmm1_xmm0();
        asm_.movsd_xmm0_mem_rbp(offset);
        asm_.subsd_xmm0_xmm1();
    } else if (op == TokenType::STAR_ASSIGN) {
        asm_.movsd_xmm1_mem_rbp(offset);
        asm_.mulsd_xmm0_xmm1();
    } else if (op == TokenType::SLASH_ASSIGN) {
        asm_.movsd_xmm1_xmm0();
        asm_.movsd_xmm0_mem_rbp(offset);
        asm_.divsd_xmm0_xmm1();
    }
    asm_.movsd_mem_rbp_xmm0(offset);
}

void NativeCodeGen::emitIntCompoundAssign(int32_t offset, TokenType op) {
    if (op == TokenType::PLUS_ASSIGN) {
        asm_.mov_rcx_mem_rbp(offset);
        asm_.add_rax_rcx();
    } else if (op == TokenType::MINUS_ASSIGN) {
        asm_.mov_rcx_rax();
        asm_.mov_rax_mem_rbp(offset);
        asm_.sub_rax_rcx();
    } else if (op == TokenType::STAR_ASSIGN) {
        asm_.mov_rcx_mem_rbp(offset);
        asm_.imul_rax_rcx();
    } else if (op == TokenType::SLASH_ASSIGN) {
        asm_.mov_rcx_rax();
        asm_.mov_rax_mem_rbp(offset);
        asm_.cqo();
        asm_.idiv_rcx();
    }
    asm_.mov_mem_rbp_rax(offset);
}

void NativeCodeGen::emitDerefAssign(DerefExpr* deref, AssignStmt& node) {
    node.value->accept(*this);
    asm_.push_rax();
    deref->operand->accept(*this);
    asm_.mov_rcx_rax();
    asm_.pop_rax();
    asm_.mov_mem_rcx_rax();
}

void NativeCodeGen::emitIndexAssign(IndexExpr* indexExpr, AssignStmt& node) {
    if (auto* objId = dynamic_cast<Identifier*>(indexExpr->object.get())) {
        auto fixedArrayIt = varFixedArrayTypes_.find(objId->name);
        if (fixedArrayIt != varFixedArrayTypes_.end()) {
            emitFixedArrayAssign(indexExpr, node, fixedArrayIt->second);
            return;
        }
    }
    
    // Regular list assignment (1-based)
    node.value->accept(*this);
    asm_.push_rax();
    
    indexExpr->index->accept(*this);
    asm_.dec_rax();
    asm_.push_rax();
    
    indexExpr->object->accept(*this);
    asm_.add_rax_imm32(16);
    
    asm_.pop_rcx();
    asm_.code.push_back(0x48); asm_.code.push_back(0xC1);
    asm_.code.push_back(0xE1); asm_.code.push_back(0x03);
    
    asm_.add_rax_rcx();
    asm_.mov_rcx_rax();
    
    asm_.pop_rax();
    asm_.mov_mem_rcx_rax();
}

void NativeCodeGen::emitFixedArrayAssign(IndexExpr* indexExpr, AssignStmt& node, const FixedArrayInfo& info) {
    node.value->accept(*this);
    asm_.push_rax();
    
    indexExpr->index->accept(*this);
    asm_.push_rax();
    
    indexExpr->object->accept(*this);
    asm_.pop_rcx();
    
    if (info.elementSize == 8) {
        asm_.code.push_back(0x48); asm_.code.push_back(0xC1);
        asm_.code.push_back(0xE1); asm_.code.push_back(0x03);
    } else if (info.elementSize == 4) {
        asm_.code.push_back(0x48); asm_.code.push_back(0xC1);
        asm_.code.push_back(0xE1); asm_.code.push_back(0x02);
    } else if (info.elementSize == 2) {
        asm_.code.push_back(0x48); asm_.code.push_back(0xD1);
        asm_.code.push_back(0xE1);
    } else if (info.elementSize != 1) {
        asm_.mov_rdx_imm64(info.elementSize);
        asm_.code.push_back(0x48); asm_.code.push_back(0x0F);
        asm_.code.push_back(0xAF); asm_.code.push_back(0xCA);
    }
    
    asm_.add_rax_rcx();
    asm_.mov_rcx_rax();
    asm_.pop_rax();
    
    if (info.elementSize == 1) {
        asm_.code.push_back(0x88);
        asm_.code.push_back(0x01);
    } else if (info.elementSize == 2) {
        asm_.code.push_back(0x66);
        asm_.code.push_back(0x89);
        asm_.code.push_back(0x01);
    } else if (info.elementSize == 4) {
        asm_.code.push_back(0x89);
        asm_.code.push_back(0x01);
    } else {
        asm_.mov_mem_rcx_rax();
    }
}

void NativeCodeGen::emitMemberAssign(MemberExpr* member, AssignStmt& node) {
    if (auto* objId = dynamic_cast<Identifier*>(member->object.get())) {
        auto varTypeIt = varRecordTypes_.find(objId->name);
        if (varTypeIt != varRecordTypes_.end()) {
            auto typeIt = recordTypes_.find(varTypeIt->second);
            if (typeIt != recordTypes_.end()) {
                int fieldIndex = -1;
                for (size_t i = 0; i < typeIt->second.fieldNames.size(); i++) {
                    if (typeIt->second.fieldNames[i] == member->member) {
                        fieldIndex = static_cast<int>(i);
                        break;
                    }
                }
                
                if (fieldIndex >= 0) {
                    bool isBitfield = false;
                    if (fieldIndex < static_cast<int>(typeIt->second.fieldBitWidths.size())) {
                        isBitfield = (typeIt->second.fieldBitWidths[fieldIndex] > 0);
                    }
                    
                    if (isBitfield) {
                        node.value->accept(*this);
                        asm_.mov_rcx_rax();
                        member->object->accept(*this);
                        emitBitfieldWrite(varTypeIt->second, fieldIndex);
                        return;
                    }
                    
                    node.value->accept(*this);
                    asm_.push_rax();
                    member->object->accept(*this);
                    
                    int32_t offset = getRecordFieldOffset(varTypeIt->second, fieldIndex);
                    if (offset > 0) {
                        asm_.add_rax_imm32(offset);
                    }
                    
                    const std::string& fieldType = typeIt->second.fieldTypes[fieldIndex];
                    int32_t fieldSize = getTypeSize(fieldType);
                    
                    asm_.mov_rcx_rax();
                    asm_.pop_rax();
                    
                    if (fieldSize == 1) {
                        asm_.code.push_back(0x88);
                        asm_.code.push_back(0x01);
                    } else if (fieldSize == 2) {
                        asm_.code.push_back(0x66);
                        asm_.code.push_back(0x89);
                        asm_.code.push_back(0x01);
                    } else if (fieldSize == 4) {
                        asm_.code.push_back(0x89);
                        asm_.code.push_back(0x01);
                    } else {
                        asm_.mov_mem_rcx_rax();
                    }
                    return;
                }
            }
        }
    }
    
    // Fallback
    node.value->accept(*this);
    asm_.push_rax();
    member->object->accept(*this);
    asm_.mov_rcx_rax();
    asm_.pop_rax();
    asm_.mov_mem_rcx_rax();
}

} // namespace tyl
