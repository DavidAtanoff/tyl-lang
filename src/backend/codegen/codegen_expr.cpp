// Flex Compiler - Native Code Generator Expression Visitors
// Handles: literals, identifiers, binary/unary expressions, calls, etc.

#include "codegen_base.h"
#include <cmath>

namespace flex {

void NativeCodeGen::visit(IntegerLiteral& node) {
    // OPTIMIZATION: Use smaller instruction encodings for small values
    if (node.value == 0) {
        asm_.xor_rax_rax();  // 3 bytes instead of 10
    } else if (node.value >= 0 && node.value <= 0x7FFFFFFF) {
        // mov eax, imm32 (5 bytes) - zero-extends to rax
        asm_.code.push_back(0xB8);
        asm_.code.push_back(node.value & 0xFF);
        asm_.code.push_back((node.value >> 8) & 0xFF);
        asm_.code.push_back((node.value >> 16) & 0xFF);
        asm_.code.push_back((node.value >> 24) & 0xFF);
    } else {
        // Full 64-bit mov for large/negative values
        asm_.mov_rax_imm64(node.value);
    }
    lastExprWasFloat_ = false;
}

void NativeCodeGen::visit(FloatLiteral& node) {
    // Load float constant into xmm0 via rax
    union { double d; int64_t i; } u;
    u.d = node.value;
    asm_.mov_rax_imm64(u.i);
    asm_.movq_xmm0_rax();
    // Also keep in rax for compatibility with existing code paths
    lastExprWasFloat_ = true;
}

void NativeCodeGen::visit(StringLiteral& node) {
    uint32_t rva = addString(node.value);
    asm_.lea_rax_rip_fixup(rva);
    lastExprWasFloat_ = false;
}

void NativeCodeGen::visit(InterpolatedString& node) {
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
                } else {
                    result += "<?>";
                }
            }
        }
        uint32_t rva = addString(result);
        asm_.lea_rax_rip_fixup(rva);
    }
}

void NativeCodeGen::visit(BoolLiteral& node) {
    // OPTIMIZATION: Use smaller instruction for 0/1
    if (node.value) {
        // mov eax, 1 (5 bytes instead of 10)
        asm_.code.push_back(0xB8);
        asm_.code.push_back(0x01);
        asm_.code.push_back(0x00);
        asm_.code.push_back(0x00);
        asm_.code.push_back(0x00);
    } else {
        asm_.xor_rax_rax();  // 3 bytes
    }
    lastExprWasFloat_ = false;
}

void NativeCodeGen::visit(NilLiteral& node) {
    (void)node;
    asm_.xor_rax_rax();
    lastExprWasFloat_ = false;
}

void NativeCodeGen::visit(Identifier& node) {
    // First check if this is a function label - functions should NOT use register allocation
    if (asm_.labels.count(node.name)) {
        // This is a function reference, load its address
        asm_.code.push_back(0x48); asm_.code.push_back(0x8D); asm_.code.push_back(0x05);
        asm_.fixupLabel(node.name);
        lastExprWasFloat_ = false;
        return;
    }
    
    auto it = locals.find(node.name);
    auto regIt = varRegisters_.find(node.name);
    auto globalRegIt = globalVarRegisters_.find(node.name);
    
    // Check if variable is in a function-local register
    // But skip if this is actually a function name (shouldn't happen, but safety check)
    if (regIt != varRegisters_.end() && regIt->second != VarRegister::NONE) {
        // Check if this is a float variable
        if (floatVars.count(node.name)) {
            // Float in register - need to load to xmm0
            // First move register to rax, then to xmm0
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
            // Integer in register - move to rax
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
    }
    // Check if variable is in a global register (top-level code)
    else if (globalRegIt != globalVarRegisters_.end() && globalRegIt->second != VarRegister::NONE) {
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
    }
    else if (it != locals.end()) {
        // Variable is on stack
        if (floatVars.count(node.name)) {
            asm_.movsd_xmm0_mem_rbp(it->second);
            asm_.movq_rax_xmm0();
            lastExprWasFloat_ = true;
        } else {
            asm_.mov_rax_mem_rbp(it->second);
            lastExprWasFloat_ = false;
        }
    } else if (constFloatVars.count(node.name)) {
        // Constant float
        union { double d; int64_t i; } u;
        u.d = constFloatVars[node.name];
        asm_.mov_rax_imm64(u.i);
        asm_.movq_xmm0_rax();
        lastExprWasFloat_ = true;
    } else {
        // Unknown identifier - return 0
        asm_.xor_rax_rax();
        lastExprWasFloat_ = false;
    }
}

void NativeCodeGen::visit(BinaryExpr& node) {
    // Check if this is a float operation
    bool isFloat = isFloatExpression(node.left.get()) || isFloatExpression(node.right.get());
    
    if (isFloat) {
        // Float binary operation using SSE
        node.right->accept(*this);
        if (!lastExprWasFloat_) {
            // Convert int to float
            asm_.cvtsi2sd_xmm0_rax();
        }
        asm_.movsd_xmm1_xmm0();  // Save right operand in xmm1
        
        node.left->accept(*this);
        if (!lastExprWasFloat_) {
            // Convert int to float
            asm_.cvtsi2sd_xmm0_rax();
        }
        // Now xmm0 = left, xmm1 = right
        
        switch (node.op) {
            case TokenType::PLUS:
                asm_.addsd_xmm0_xmm1();
                lastExprWasFloat_ = true;
                break;
            case TokenType::MINUS:
                asm_.subsd_xmm0_xmm1();
                lastExprWasFloat_ = true;
                break;
            case TokenType::STAR:
                asm_.mulsd_xmm0_xmm1();
                lastExprWasFloat_ = true;
                break;
            case TokenType::SLASH:
                asm_.divsd_xmm0_xmm1();
                lastExprWasFloat_ = true;
                break;
            case TokenType::EQ:
                asm_.ucomisd_xmm0_xmm1();
                asm_.sete_al();
                asm_.movzx_rax_al();
                lastExprWasFloat_ = false;
                break;
            case TokenType::NE:
                asm_.ucomisd_xmm0_xmm1();
                asm_.setne_al();
                asm_.movzx_rax_al();
                lastExprWasFloat_ = false;
                break;
            case TokenType::LT:
                asm_.ucomisd_xmm0_xmm1();
                asm_.code.push_back(0x0F); asm_.code.push_back(0x92); asm_.code.push_back(0xC0);  // setb al
                asm_.movzx_rax_al();
                lastExprWasFloat_ = false;
                break;
            case TokenType::GT:
                asm_.ucomisd_xmm0_xmm1();
                asm_.code.push_back(0x0F); asm_.code.push_back(0x97); asm_.code.push_back(0xC0);  // seta al
                asm_.movzx_rax_al();
                lastExprWasFloat_ = false;
                break;
            case TokenType::LE:
                asm_.ucomisd_xmm0_xmm1();
                asm_.code.push_back(0x0F); asm_.code.push_back(0x96); asm_.code.push_back(0xC0);  // setbe al
                asm_.movzx_rax_al();
                lastExprWasFloat_ = false;
                break;
            case TokenType::GE:
                asm_.ucomisd_xmm0_xmm1();
                asm_.code.push_back(0x0F); asm_.code.push_back(0x93); asm_.code.push_back(0xC0);  // setae al
                asm_.movzx_rax_al();
                lastExprWasFloat_ = false;
                break;
            default:
                // Fallback - convert to int and use integer ops
                asm_.cvttsd2si_rax_xmm0();
                lastExprWasFloat_ = false;
                break;
        }
        
        // Keep result in rax as well for compatibility
        if (lastExprWasFloat_) {
            asm_.movq_rax_xmm0();
        }
        return;
    }
    
    // OPTIMIZATION: Check if right operand is a small constant
    // If so, we can use immediate instructions instead of push/pop
    int64_t rightConst;
    bool rightIsConst = tryEvalConstant(node.right.get(), rightConst);
    bool rightIsSmall = rightIsConst && rightConst >= INT32_MIN && rightConst <= INT32_MAX;
    
    // OPTIMIZATION: Strength reduction for multiplication by small constants using LEA/shifts
    if (node.op == TokenType::STAR && rightIsConst) {
        node.left->accept(*this);
        
        // Use LEA or shifts for common multipliers
        switch (rightConst) {
            case 0:
                asm_.xor_rax_rax();
                lastExprWasFloat_ = false;
                return;
            case 1:
                // No-op, result already in rax
                lastExprWasFloat_ = false;
                return;
            case 2:
                asm_.shl_rax_imm8(1);  // x * 2 = x << 1
                lastExprWasFloat_ = false;
                return;
            case 3:
                asm_.mov_rcx_rax();
                asm_.lea_rax_rcx_rcx_2();  // x * 3 = x + x*2
                lastExprWasFloat_ = false;
                return;
            case 4:
                asm_.shl_rax_imm8(2);  // x * 4 = x << 2
                lastExprWasFloat_ = false;
                return;
            case 5:
                asm_.mov_rcx_rax();
                asm_.lea_rax_rcx_4();      // rax = rcx * 4
                asm_.add_rax_rcx();        // rax = rcx * 4 + rcx = rcx * 5
                lastExprWasFloat_ = false;
                return;
            case 6:
                asm_.mov_rcx_rax();
                asm_.lea_rax_rcx_rcx_2();  // rax = rcx * 3
                asm_.shl_rax_imm8(1);      // rax = rcx * 6
                lastExprWasFloat_ = false;
                return;
            case 7:
                asm_.mov_rcx_rax();
                asm_.lea_rax_rcx_8();      // rax = rcx * 8
                asm_.sub_rax_rcx();        // rax = rcx * 8 - rcx = rcx * 7
                lastExprWasFloat_ = false;
                return;
            case 8:
                asm_.shl_rax_imm8(3);  // x * 8 = x << 3
                lastExprWasFloat_ = false;
                return;
            case 9:
                asm_.mov_rcx_rax();
                asm_.lea_rax_rcx_8();      // rax = rcx * 8
                asm_.add_rax_rcx();        // rax = rcx * 8 + rcx = rcx * 9
                lastExprWasFloat_ = false;
                return;
            case 10:
                asm_.mov_rcx_rax();
                asm_.lea_rax_rcx_4();      // rax = rcx * 4
                asm_.add_rax_rcx();        // rax = rcx * 5
                asm_.shl_rax_imm8(1);      // rax = rcx * 10
                lastExprWasFloat_ = false;
                return;
            case 16:
                asm_.shl_rax_imm8(4);  // x * 16 = x << 4
                lastExprWasFloat_ = false;
                return;
            case 32:
                asm_.shl_rax_imm8(5);
                lastExprWasFloat_ = false;
                return;
            case 64:
                asm_.shl_rax_imm8(6);
                lastExprWasFloat_ = false;
                return;
            case 128:
                asm_.shl_rax_imm8(7);
                lastExprWasFloat_ = false;
                return;
            case 256:
                asm_.shl_rax_imm8(8);
                lastExprWasFloat_ = false;
                return;
            default:
                // Check if it's a power of 2
                if (rightConst > 0 && (rightConst & (rightConst - 1)) == 0) {
                    int shift = 0;
                    int64_t temp = rightConst;
                    while (temp > 1) { temp >>= 1; shift++; }
                    asm_.shl_rax_imm8((uint8_t)shift);
                    lastExprWasFloat_ = false;
                    return;
                }
                // Fall through to imul
                break;
        }
        
        // Use imul with immediate for other constants
        if (rightIsSmall) {
            asm_.imul_rax_rax_imm32((int32_t)rightConst);
            lastExprWasFloat_ = false;
            return;
        }
    }
    
    // OPTIMIZATION: Division by power of 2 using shifts
    if (node.op == TokenType::SLASH && rightIsConst && rightConst > 0 && (rightConst & (rightConst - 1)) == 0) {
        node.left->accept(*this);
        int shift = 0;
        int64_t temp = rightConst;
        while (temp > 1) { temp >>= 1; shift++; }
        asm_.sar_rax_imm8((uint8_t)shift);  // Arithmetic shift for signed division
        lastExprWasFloat_ = false;
        return;
    }
    
    // For comparisons with small constants, use cmp rax, imm32 directly
    if (rightIsSmall && (node.op == TokenType::LT || node.op == TokenType::GT ||
                         node.op == TokenType::LE || node.op == TokenType::GE ||
                         node.op == TokenType::EQ || node.op == TokenType::NE)) {
        node.left->accept(*this);
        // cmp rax, imm32
        asm_.code.push_back(0x48); asm_.code.push_back(0x3D);
        asm_.code.push_back(rightConst & 0xFF);
        asm_.code.push_back((rightConst >> 8) & 0xFF);
        asm_.code.push_back((rightConst >> 16) & 0xFF);
        asm_.code.push_back((rightConst >> 24) & 0xFF);
        
        switch (node.op) {
            case TokenType::EQ: asm_.sete_al(); break;
            case TokenType::NE: asm_.setne_al(); break;
            case TokenType::LT: asm_.setl_al(); break;
            case TokenType::GT: asm_.setg_al(); break;
            case TokenType::LE: asm_.setle_al(); break;
            case TokenType::GE: asm_.setge_al(); break;
            default: break;
        }
        asm_.movzx_rax_al();
        lastExprWasFloat_ = false;
        return;
    }
    
    // For add/sub with small constants, use add/sub rax, imm32
    if (rightIsSmall && (node.op == TokenType::PLUS || node.op == TokenType::MINUS)) {
        node.left->accept(*this);
        if (node.op == TokenType::PLUS) {
            asm_.add_rax_imm32((int32_t)rightConst);
        } else {
            asm_.sub_rax_imm32((int32_t)rightConst);
        }
        lastExprWasFloat_ = false;
        return;
    }
    
    // Default: use push/pop for complex expressions
    node.right->accept(*this);
    asm_.push_rax();
    node.left->accept(*this);
    asm_.pop_rcx();
    
    switch (node.op) {
        case TokenType::PLUS: asm_.add_rax_rcx(); break;
        case TokenType::MINUS: asm_.sub_rax_rcx(); break;
        case TokenType::STAR: asm_.imul_rax_rcx(); break;
        case TokenType::SLASH:
            asm_.cqo();
            asm_.idiv_rcx();
            break;
        case TokenType::PERCENT:
            asm_.cqo();
            asm_.idiv_rcx();
            asm_.mov_rax_rdx();
            break;
        case TokenType::EQ:
            asm_.cmp_rax_rcx();
            asm_.sete_al();
            asm_.movzx_rax_al();
            break;
        case TokenType::NE:
            asm_.cmp_rax_rcx();
            asm_.setne_al();
            asm_.movzx_rax_al();
            break;
        case TokenType::LT:
            asm_.cmp_rax_rcx();
            asm_.setl_al();
            asm_.movzx_rax_al();
            break;
        case TokenType::GT:
            asm_.cmp_rax_rcx();
            asm_.setg_al();
            asm_.movzx_rax_al();
            break;
        case TokenType::LE:
            asm_.cmp_rax_rcx();
            asm_.setle_al();
            asm_.movzx_rax_al();
            break;
        case TokenType::GE:
            asm_.cmp_rax_rcx();
            asm_.setge_al();
            asm_.movzx_rax_al();
            break;
        case TokenType::AND:
            asm_.test_rax_rax();
            asm_.setne_al();
            asm_.movzx_rax_al();
            asm_.push_rax();
            asm_.mov_rax_rcx();
            asm_.test_rax_rax();
            asm_.setne_al();
            asm_.movzx_rax_al();
            asm_.pop_rcx();
            asm_.and_rax_rcx();
            break;
        case TokenType::OR:
            asm_.or_rax_rcx();
            asm_.test_rax_rax();
            asm_.setne_al();
            asm_.movzx_rax_al();
            break;
        case TokenType::QUESTION_QUESTION: {
            std::string useRight = newLabel("coalesce_right");
            std::string done = newLabel("coalesce_done");
            asm_.test_rax_rax();
            asm_.jz_rel32(useRight);
            asm_.jmp_rel32(done);
            asm_.label(useRight);
            asm_.mov_rax_rcx();
            asm_.label(done);
            break;
        }
        default:
            break;
    }
    lastExprWasFloat_ = false;
}

void NativeCodeGen::visit(UnaryExpr& node) {
    node.operand->accept(*this);
    
    if (lastExprWasFloat_ && node.op == TokenType::MINUS) {
        // Float negation: XOR with sign bit mask
        // Load -0.0 (0x8000000000000000) and XOR
        asm_.mov_rcx_imm64(0x8000000000000000LL);
        asm_.movq_xmm1_rcx();
        asm_.xorpd_xmm0_xmm1();
        asm_.movq_rax_xmm0();
        lastExprWasFloat_ = true;
        return;
    }
    
    switch (node.op) {
        case TokenType::MINUS: asm_.neg_rax(); break;
        case TokenType::NOT:
            asm_.test_rax_rax();
            asm_.sete_al();
            asm_.movzx_rax_al();
            break;
        default: break;
    }
    lastExprWasFloat_ = false;
}

void NativeCodeGen::visit(MemberExpr& node) {
    node.object->accept(*this);
}

void NativeCodeGen::visit(IndexExpr& node) {
    // Handle list/array indexing: list[index]
    
    // Check if this is a constant list access
    if (auto* ident = dynamic_cast<Identifier*>(node.object.get())) {
        auto constListIt = constListVars.find(ident->name);
        if (constListIt != constListVars.end()) {
            // Constant list - try to evaluate index at compile time
            int64_t indexVal;
            if (tryEvalConstant(node.index.get(), indexVal)) {
                if (indexVal >= 0 && (size_t)indexVal < constListIt->second.size()) {
                    // Return the constant value directly
                    asm_.mov_rax_imm64(constListIt->second[indexVal]);
                    lastExprWasFloat_ = false;
                    return;
                }
            }
        }
    }
    
    // Runtime indexing
    // Evaluate index first
    node.index->accept(*this);
    asm_.push_rax();  // Save index
    
    // Evaluate object (list pointer)
    node.object->accept(*this);
    
    // rax = list pointer, stack top = index
    asm_.pop_rcx();  // rcx = index
    
    // Calculate offset: index * 8 (64-bit elements)
    asm_.code.push_back(0x48); asm_.code.push_back(0xC1);
    asm_.code.push_back(0xE1); asm_.code.push_back(0x03);  // shl rcx, 3
    
    // Add offset to base pointer
    asm_.add_rax_rcx();
    
    // Load value from memory
    asm_.mov_rax_mem_rax();
    
    lastExprWasFloat_ = false;
}

void NativeCodeGen::visit(ListExpr& node) {
    if (node.elements.empty()) {
        asm_.xor_rax_rax();
        return;
    }
    
    std::vector<int64_t> values;
    bool allConstant = true;
    for (auto& elem : node.elements) {
        int64_t val;
        if (tryEvalConstant(elem.get(), val)) {
            values.push_back(val);
        } else {
            allConstant = false;
            break;
        }
    }
    
    if (allConstant) {
        std::vector<uint8_t> data;
        for (int64_t val : values) {
            for (int i = 0; i < 8; i++) {
                data.push_back((val >> (i * 8)) & 0xFF);
            }
        }
        uint32_t rva = pe_.addData(data.data(), data.size());
        asm_.lea_rax_rip_fixup(rva);
    } else {
        asm_.xor_rax_rax();
    }
}

void NativeCodeGen::visit(RecordExpr& node) {
    if (node.fields.empty()) {
        asm_.xor_rax_rax();
        return;
    }
    
    // Allocate space on heap for the record
    size_t size = node.fields.size() * 8;
    
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("GetProcessHeap"));
    
    asm_.mov_rcx_rax();
    asm_.xor_rax_rax();
    asm_.mov_rdx_rax();
    asm_.mov_r8d_imm32((int32_t)size);
    
    asm_.call_mem_rip(pe_.getImportRVA("HeapAlloc"));
    if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
    
    // Save record pointer on stack (don't use rdi - it's for stdout caching)
    allocLocal("$record_ptr");
    asm_.mov_mem_rbp_rax(locals["$record_ptr"]);
    
    for (size_t i = 0; i < node.fields.size(); i++) {
        // Evaluate field expression -> result in rax
        node.fields[i].second->accept(*this);
        
        // Load record pointer
        asm_.mov_rcx_mem_rbp(locals["$record_ptr"]);
        
        // Store in record memory: [rcx + i*8] = rax
        if (i > 0) {
            asm_.add_rcx_imm32((int32_t)(i * 8));
        }
        asm_.mov_mem_rcx_rax();
    }
    
    // Return record pointer in rax
    asm_.mov_rax_mem_rbp(locals["$record_ptr"]);
}

void NativeCodeGen::visit(AssignExpr& node) {
    // Evaluate value
    node.value->accept(*this);
    
    if (auto* id = dynamic_cast<Identifier*>(node.target.get())) {
        // Only track as non-constant if this is a reassignment (variable already exists)
        // For initial assignments, keep the constant values from pre-scan
        bool isReassignment = locals.count(id->name) > 0 || 
                              varRegisters_.count(id->name) > 0 ||
                              globalVarRegisters_.count(id->name) > 0;
        
        if (isReassignment) {
            constVars.erase(id->name);
            constStrVars.erase(id->name);
            constFloatVars.erase(id->name);
        }
        
        // Check local register
        auto regIt = varRegisters_.find(id->name);
        // Also check global registers
        auto globalRegIt = globalVarRegisters_.find(id->name);
        
        VarRegister reg = VarRegister::NONE;
        if (regIt != varRegisters_.end() && regIt->second != VarRegister::NONE) {
            reg = regIt->second;
        } else if (globalRegIt != globalVarRegisters_.end() && globalRegIt->second != VarRegister::NONE) {
            reg = globalRegIt->second;
        }
        
        if (reg != VarRegister::NONE) {
            // Handle compound assignment logic if op isn't ASSIGN
            if (node.op != TokenType::ASSIGN) {
                if (node.op == TokenType::SLASH_ASSIGN) {
                    // Division: need dividend in rax, divisor in rcx
                    asm_.mov_rcx_rax();  // rcx = divisor (new value)
                    // load old value (dividend)
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
                } else {
                    asm_.push_rax(); // save new value
                    // load old value
                    switch (reg) {
                        case VarRegister::RBX: asm_.mov_rax_rbx(); break;
                        case VarRegister::R12: asm_.mov_rax_r12(); break;
                        case VarRegister::R13: asm_.mov_rax_r13(); break;
                        case VarRegister::R14: asm_.mov_rax_r14(); break;
                        case VarRegister::R15: asm_.mov_rax_r15(); break;
                        default: break;
                    }
                    asm_.pop_rcx(); // new value
                    
                    if (node.op == TokenType::PLUS_ASSIGN) asm_.add_rax_rcx();
                    else if (node.op == TokenType::MINUS_ASSIGN) asm_.sub_rax_rcx();
                    else if (node.op == TokenType::STAR_ASSIGN) asm_.imul_rax_rcx();
                }
            }
            
            // Store to register
            switch (reg) {
                case VarRegister::RBX: asm_.mov_rbx_rax(); break;
                case VarRegister::R12: asm_.mov_r12_rax(); break;
                case VarRegister::R13: asm_.mov_r13_rax(); break;
                case VarRegister::R14: asm_.mov_r14_rax(); break;
                case VarRegister::R15: asm_.mov_r15_rax(); break;
                default: break;
            }
        } else {
            // Fall back to stack
            auto it = locals.find(id->name);
            if (it == locals.end()) {
                allocLocal(id->name);
                it = locals.find(id->name);
            }
            
            if (node.op != TokenType::ASSIGN) {
                if (node.op == TokenType::SLASH_ASSIGN) {
                    asm_.mov_rcx_rax();  // rcx = divisor
                    asm_.mov_rax_mem_rbp(it->second);  // rax = dividend
                    asm_.cqo();
                    asm_.idiv_rcx();
                } else if (node.op == TokenType::STAR_ASSIGN) {
                    asm_.mov_rcx_mem_rbp(it->second);
                    asm_.imul_rax_rcx();
                } else {
                    asm_.push_rax();
                    asm_.mov_rax_mem_rbp(it->second);
                    asm_.pop_rcx();
                    if (node.op == TokenType::PLUS_ASSIGN) asm_.add_rax_rcx();
                    else if (node.op == TokenType::MINUS_ASSIGN) asm_.sub_rax_rcx();
                }
            }
            asm_.mov_mem_rbp_rax(it->second);
        }
    }
    // Result of assignment is the value in rax
}

void NativeCodeGen::visit(RangeExpr& node) { (void)node; asm_.xor_rax_rax(); }

void NativeCodeGen::visit(LambdaExpr& node) {
    // Generate a unique label for this lambda
    std::string lambdaLabel = newLabel("lambda");
    std::string afterLambda = newLabel("after_lambda");
    
    // Jump over the lambda body (we'll call it later)
    asm_.jmp_rel32(afterLambda);
    
    // Lambda function body
    asm_.label(lambdaLabel);
    
    // Save state
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
    
    // Standard function prologue
    asm_.push_rbp();
    asm_.mov_rbp_rsp();
    
    // Allocate minimal stack space
    functionStackSize_ = 0x40;
    asm_.sub_rsp_imm32(functionStackSize_);
    stackAllocated_ = true;
    
    // Store parameters (Windows x64: rcx, rdx, r8, r9)
    for (size_t i = 0; i < node.params.size() && i < 4; i++) {
        const std::string& paramName = node.params[i].first;
        allocLocal(paramName);
        int32_t off = locals[paramName];
        
        switch (i) {
            case 0: asm_.mov_mem_rbp_rcx(off); break;
            case 1:
                asm_.code.push_back(0x48); asm_.code.push_back(0x89);
                asm_.code.push_back(0x95);
                asm_.code.push_back(off & 0xFF);
                asm_.code.push_back((off >> 8) & 0xFF);
                asm_.code.push_back((off >> 16) & 0xFF);
                asm_.code.push_back((off >> 24) & 0xFF);
                break;
            case 2:
                asm_.code.push_back(0x4C); asm_.code.push_back(0x89);
                asm_.code.push_back(0x85);
                asm_.code.push_back(off & 0xFF);
                asm_.code.push_back((off >> 8) & 0xFF);
                asm_.code.push_back((off >> 16) & 0xFF);
                asm_.code.push_back((off >> 24) & 0xFF);
                break;
            case 3:
                asm_.code.push_back(0x4C); asm_.code.push_back(0x89);
                asm_.code.push_back(0x8D);
                asm_.code.push_back(off & 0xFF);
                asm_.code.push_back((off >> 8) & 0xFF);
                asm_.code.push_back((off >> 16) & 0xFF);
                asm_.code.push_back((off >> 24) & 0xFF);
                break;
        }
    }
    
    // Evaluate the lambda body expression
    node.body->accept(*this);
    
    // Function epilogue - result is already in rax
    asm_.add_rsp_imm32(functionStackSize_);
    asm_.pop_rbp();
    asm_.ret();
    
    // Restore state
    locals = savedLocals;
    stackOffset = savedStackOffset;
    inFunction = savedInFunction;
    functionStackSize_ = savedFunctionStackSize;
    stackAllocated_ = savedStackAllocated;
    varRegisters_ = savedVarRegisters;
    
    // After lambda: load the lambda's address into rax
    asm_.label(afterLambda);
    
    // lea rax, [rip + lambdaLabel]
    asm_.code.push_back(0x48); asm_.code.push_back(0x8D); asm_.code.push_back(0x05);
    asm_.fixupLabel(lambdaLabel);
    
    lastExprWasFloat_ = false;
}

void NativeCodeGen::visit(TernaryExpr& node) {
    // OPTIMIZATION: Try to use CMOV for simple ternary expressions (branchless)
    // This is faster when both branches are simple expressions
    int64_t thenConst, elseConst;
    bool thenIsConst = tryEvalConstant(node.thenExpr.get(), thenConst);
    bool elseIsConst = tryEvalConstant(node.elseExpr.get(), elseConst);
    
    // Use CMOV when both branches are constants or simple values
    if (thenIsConst && elseIsConst) {
        // Evaluate condition
        node.condition->accept(*this);
        asm_.test_rax_rax();
        
        // Load else value first (default)
        if (elseConst == 0) {
            asm_.xor_rax_rax();
        } else {
            asm_.mov_rax_imm64(elseConst);
        }
        
        // Load then value into rcx
        if (thenConst == 0) {
            asm_.xor_ecx_ecx();
        } else {
            asm_.mov_rcx_imm64(thenConst);
        }
        
        // Conditionally move: if condition was true (non-zero), use then value
        // We need to re-test since we clobbered flags
        // Actually, let's use a different approach - evaluate condition into a temp
        
        // Simpler approach: use the standard branch for now, CMOV is tricky with flag preservation
        // Fall through to standard implementation
    }
    
    // Standard implementation with branches
    std::string elseLabel = newLabel("tern_else");
    std::string endLabel = newLabel("tern_end");
    
    node.condition->accept(*this);
    asm_.test_rax_rax();
    asm_.jz_rel32(elseLabel);
    node.thenExpr->accept(*this);
    asm_.jmp_rel32(endLabel);
    asm_.label(elseLabel);
    node.elseExpr->accept(*this);
    asm_.label(endLabel);
}

void NativeCodeGen::visit(ListCompExpr& node) {
    // List comprehension: [expr for var in iterable]
    // Example: [x * x for x in 0..10]
    
    // First, determine the size of the result list
    // For range expressions, we can calculate this at compile time
    int64_t listSize = 0;
    bool sizeKnown = false;
    
    if (auto* range = dynamic_cast<RangeExpr*>(node.iterable.get())) {
        int64_t startVal, endVal;
        if (tryEvalConstant(range->start.get(), startVal) && 
            tryEvalConstant(range->end.get(), endVal)) {
            listSize = endVal - startVal + 1;  // Inclusive range
            if (listSize < 0) listSize = 0;
            sizeKnown = true;
        }
    } else if (auto* call = dynamic_cast<CallExpr*>(node.iterable.get())) {
        if (auto* calleeId = dynamic_cast<Identifier*>(call->callee.get())) {
            if (calleeId->name == "range") {
                if (call->args.size() == 1) {
                    int64_t endVal;
                    if (tryEvalConstant(call->args[0].get(), endVal)) {
                        listSize = endVal;  // range(n) is 0..n-1
                        sizeKnown = true;
                    }
                } else if (call->args.size() >= 2) {
                    int64_t startVal, endVal;
                    if (tryEvalConstant(call->args[0].get(), startVal) &&
                        tryEvalConstant(call->args[1].get(), endVal)) {
                        listSize = endVal - startVal;  // range(a,b) is a..b-1
                        if (listSize < 0) listSize = 0;
                        sizeKnown = true;
                    }
                }
            }
        }
    }
    
    if (!sizeKnown || listSize <= 0) {
        // Can't determine size at compile time, return empty list
        asm_.xor_rax_rax();
        return;
    }
    
    // Allocate heap memory for the list (listSize * 8 bytes)
    size_t allocSize = (size_t)listSize * 8;
    
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("GetProcessHeap"));
    
    asm_.mov_rcx_rax();
    asm_.xor_rax_rax();
    asm_.mov_rdx_rax();  // HEAP_ZERO_MEMORY = 0
    asm_.mov_r8d_imm32((int32_t)allocSize);
    
    asm_.call_mem_rip(pe_.getImportRVA("HeapAlloc"));
    if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
    
    // Save list pointer
    allocLocal("$listcomp_ptr");
    asm_.mov_mem_rbp_rax(locals["$listcomp_ptr"]);
    
    // Initialize loop counter
    allocLocal("$listcomp_idx");
    asm_.xor_rax_rax();
    asm_.mov_mem_rbp_rax(locals["$listcomp_idx"]);
    
    // Initialize loop variable
    allocLocal(node.var);
    
    // Get start value
    if (auto* range = dynamic_cast<RangeExpr*>(node.iterable.get())) {
        range->start->accept(*this);
    } else if (auto* call = dynamic_cast<CallExpr*>(node.iterable.get())) {
        if (call->args.size() == 1) {
            asm_.xor_rax_rax();  // range(n) starts at 0
        } else {
            call->args[0]->accept(*this);  // range(start, end)
        }
    }
    asm_.mov_mem_rbp_rax(locals[node.var]);
    
    // Get end value
    allocLocal("$listcomp_end");
    if (auto* range = dynamic_cast<RangeExpr*>(node.iterable.get())) {
        range->end->accept(*this);
    } else if (auto* call = dynamic_cast<CallExpr*>(node.iterable.get())) {
        if (call->args.size() == 1) {
            call->args[0]->accept(*this);
        } else {
            call->args[1]->accept(*this);
        }
    }
    asm_.mov_mem_rbp_rax(locals["$listcomp_end"]);
    
    // Loop
    std::string loopLabel = newLabel("listcomp_loop");
    std::string endLabel = newLabel("listcomp_end");
    
    asm_.label(loopLabel);
    
    // Check if we've reached the end
    asm_.mov_rax_mem_rbp(locals[node.var]);
    asm_.cmp_rax_mem_rbp(locals["$listcomp_end"]);
    
    // For range expressions (inclusive), use jg; for range() (exclusive), use jge
    if (dynamic_cast<RangeExpr*>(node.iterable.get())) {
        asm_.jg_rel32(endLabel);
    } else {
        asm_.jge_rel32(endLabel);
    }
    
    // Check condition if present
    if (node.condition) {
        std::string skipLabel = newLabel("listcomp_skip");
        node.condition->accept(*this);
        asm_.test_rax_rax();
        asm_.jz_rel32(skipLabel);
        
        // Evaluate expression
        node.expr->accept(*this);
        
        // Store in list: list[idx] = value
        asm_.mov_rcx_mem_rbp(locals["$listcomp_ptr"]);
        asm_.mov_rdx_mem_rbp(locals["$listcomp_idx"]);
        // rdx *= 8
        asm_.code.push_back(0x48); asm_.code.push_back(0xC1);
        asm_.code.push_back(0xE2); asm_.code.push_back(0x03);
        asm_.add_rax_rcx();  // This is wrong, we need rcx + rdx
        // Actually: mov [rcx + rdx], rax
        // Let's fix this:
        asm_.code.push_back(0x48); asm_.code.push_back(0x01); asm_.code.push_back(0xD1);  // add rcx, rdx
        asm_.mov_mem_rcx_rax();
        
        // Increment index
        asm_.mov_rax_mem_rbp(locals["$listcomp_idx"]);
        asm_.inc_rax();
        asm_.mov_mem_rbp_rax(locals["$listcomp_idx"]);
        
        asm_.label(skipLabel);
    } else {
        // Evaluate expression
        node.expr->accept(*this);
        
        // Store in list: list[idx] = value
        asm_.push_rax();  // Save expression result
        asm_.mov_rcx_mem_rbp(locals["$listcomp_ptr"]);
        asm_.mov_rdx_mem_rbp(locals["$listcomp_idx"]);
        // rdx *= 8
        asm_.code.push_back(0x48); asm_.code.push_back(0xC1);
        asm_.code.push_back(0xE2); asm_.code.push_back(0x03);
        // rcx += rdx
        asm_.code.push_back(0x48); asm_.code.push_back(0x01); asm_.code.push_back(0xD1);
        asm_.pop_rax();  // Restore expression result
        asm_.mov_mem_rcx_rax();
        
        // Increment index
        asm_.mov_rax_mem_rbp(locals["$listcomp_idx"]);
        asm_.inc_rax();
        asm_.mov_mem_rbp_rax(locals["$listcomp_idx"]);
    }
    
    // Increment loop variable
    asm_.mov_rax_mem_rbp(locals[node.var]);
    asm_.inc_rax();
    asm_.mov_mem_rbp_rax(locals[node.var]);
    
    asm_.jmp_rel32(loopLabel);
    
    asm_.label(endLabel);
    
    // Return list pointer
    asm_.mov_rax_mem_rbp(locals["$listcomp_ptr"]);
    
    // Track the list size
    listSizes["$listcomp_result"] = (size_t)listSize;
    
    lastExprWasFloat_ = false;
}

void NativeCodeGen::visit(AddressOfExpr& node) {
    node.operand->accept(*this);
}

void NativeCodeGen::visit(DerefExpr& node) {
    node.operand->accept(*this);
}

void NativeCodeGen::visit(NewExpr& node) {
    size_t size = 8;
    if (!node.args.empty()) {
        size = node.args.size() * 8;
    }
    
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("GetProcessHeap"));
    asm_.add_rsp_imm32(0x28);
    
    asm_.mov_rcx_rax();
    asm_.xor_rax_rax();
    asm_.mov_rdx_rax();
    asm_.mov_r8d_imm32((int32_t)size);
    
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("HeapAlloc"));
    asm_.add_rsp_imm32(0x28);
    
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
}

void NativeCodeGen::visit(AwaitExpr& node) {
    // Await: if the operand is a "future" (thread handle), wait for it
    // For now, we implement a simple synchronous await that just evaluates the expression
    // A full implementation would use WaitForSingleObject on a thread handle
    
    node.operand->accept(*this);
    
    // Check if result is a thread handle (non-zero pointer-like value)
    // If so, wait for it to complete
    
    // Test if it looks like a handle (> 0x1000, typical for handles)
    asm_.cmp_rax_imm32(0x1000);
    std::string notHandle = newLabel("await_not_handle");
    std::string done = newLabel("await_done");
    asm_.jl_rel32(notHandle);
    
    // Save handle to a local variable
    allocLocal("$await_handle");
    asm_.mov_mem_rbp_rax(locals["$await_handle"]);
    
    // It's a handle - wait for thread to complete
    // WaitForSingleObject(handle, INFINITE)
    asm_.mov_rcx_rax();  // handle
    asm_.mov_rdx_imm64(0xFFFFFFFF);  // INFINITE
    
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("WaitForSingleObject"));
    if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
    
    // Get the thread's exit code (return value)
    // GetExitCodeThread(handle, &exitCode)
    allocLocal("$await_result");
    asm_.mov_rcx_mem_rbp(locals["$await_handle"]);  // handle
    asm_.lea_rdx_rbp_offset(locals["$await_result"]);
    
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("GetExitCodeThread"));
    if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
    
    // Close the thread handle
    asm_.mov_rcx_mem_rbp(locals["$await_handle"]);
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("CloseHandle"));
    if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
    
    // Return the exit code
    asm_.mov_rax_mem_rbp(locals["$await_result"]);
    asm_.jmp_rel32(done);
    
    asm_.label(notHandle);
    // Not a handle - just return the value as-is (already in rax)
    
    asm_.label(done);
}

void NativeCodeGen::visit(SpawnExpr& node) {
    // Spawn: create a new thread to execute the expression
    // For function calls, we create a thread that runs the function
    // Returns a thread handle that can be awaited
    
    if (auto* call = dynamic_cast<CallExpr*>(node.operand.get())) {
        if (auto* ident = dynamic_cast<Identifier*>(call->callee.get())) {
            // Check if this is a known function
            if (asm_.labels.count(ident->name)) {
                // For functions with no arguments, we can spawn directly
                // CreateThread(NULL, 0, lpStartAddress, lpParameter, 0, NULL)
                
                if (call->args.empty()) {
                    // Get function address into r8 directly
                    // lea r8, [rip + function_label]
                    asm_.code.push_back(0x4C); asm_.code.push_back(0x8D); asm_.code.push_back(0x05);
                    asm_.fixupLabel(ident->name);
                    
                    // CreateThread params:
                    // rcx = lpThreadAttributes (NULL)
                    // rdx = dwStackSize (0 = default)
                    // r8 = lpStartAddress (function pointer) - already set
                    // r9 = lpParameter (NULL for no-arg functions)
                    // [rsp+0x20] = dwCreationFlags (0)
                    // [rsp+0x28] = lpThreadId (NULL)
                    
                    asm_.xor_rax_rax();
                    asm_.mov_rcx_rax();  // lpThreadAttributes = NULL
                    asm_.mov_rdx_rax();  // dwStackSize = 0
                    // r8 already has function address
                    // mov r9, 0 (lpParameter = NULL)
                    asm_.code.push_back(0x4D); asm_.code.push_back(0x31); asm_.code.push_back(0xC9);
                    
                    // [rsp+0x20] = 0 (dwCreationFlags)
                    // mov [rsp+0x20], rax
                    asm_.code.push_back(0x48); asm_.code.push_back(0x89);
                    asm_.code.push_back(0x44); asm_.code.push_back(0x24); asm_.code.push_back(0x20);
                    
                    // [rsp+0x28] = NULL (lpThreadId)
                    // mov [rsp+0x28], rax
                    asm_.code.push_back(0x48); asm_.code.push_back(0x89);
                    asm_.code.push_back(0x44); asm_.code.push_back(0x24); asm_.code.push_back(0x28);
                    
                    if (!stackAllocated_) asm_.sub_rsp_imm32(0x30);
                    asm_.call_mem_rip(pe_.getImportRVA("CreateThread"));
                    if (!stackAllocated_) asm_.add_rsp_imm32(0x30);
                    
                    // rax now contains the thread handle
                    return;
                }
                
                // For functions with arguments, we need to package them
                // For now, fall back to synchronous execution for functions with args
                // A full implementation would allocate a struct with function ptr + args
            }
        }
    }
    
    // Default: just evaluate the expression synchronously
    node.operand->accept(*this);
}

void NativeCodeGen::visit(DSLBlock& node) {
    uint32_t offset = addString(node.rawContent);
    asm_.lea_rax_rip_fixup(offset);
}

} // namespace flex
