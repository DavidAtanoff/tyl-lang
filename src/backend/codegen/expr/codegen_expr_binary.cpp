// Tyl Compiler - Native Code Generator Binary Expressions
// Handles: BinaryExpr, UnaryExpr with optimizations

#include "backend/codegen/codegen_base.h"

namespace tyl {

void NativeCodeGen::visit(BinaryExpr& node) {
    // Check if this is a float operation
    bool isFloat = isFloatExpression(node.left.get()) || isFloatExpression(node.right.get());
    
    if (isFloat) {
        // Float binary operation using SSE
        node.right->accept(*this);
        if (!lastExprWasFloat_) {
            asm_.cvtsi2sd_xmm0_rax();
        }
        asm_.movsd_xmm1_xmm0();
        
        node.left->accept(*this);
        if (!lastExprWasFloat_) {
            asm_.cvtsi2sd_xmm0_rax();
        }
        
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
                asm_.code.push_back(0x0F); asm_.code.push_back(0x92); asm_.code.push_back(0xC0);
                asm_.movzx_rax_al();
                lastExprWasFloat_ = false;
                break;
            case TokenType::GT:
                asm_.ucomisd_xmm0_xmm1();
                asm_.code.push_back(0x0F); asm_.code.push_back(0x97); asm_.code.push_back(0xC0);
                asm_.movzx_rax_al();
                lastExprWasFloat_ = false;
                break;
            case TokenType::LE:
                asm_.ucomisd_xmm0_xmm1();
                asm_.code.push_back(0x0F); asm_.code.push_back(0x96); asm_.code.push_back(0xC0);
                asm_.movzx_rax_al();
                lastExprWasFloat_ = false;
                break;
            case TokenType::GE:
                asm_.ucomisd_xmm0_xmm1();
                asm_.code.push_back(0x0F); asm_.code.push_back(0x93); asm_.code.push_back(0xC0);
                asm_.movzx_rax_al();
                lastExprWasFloat_ = false;
                break;
            default:
                asm_.cvttsd2si_rax_xmm0();
                lastExprWasFloat_ = false;
                break;
        }
        
        if (lastExprWasFloat_) {
            asm_.movq_rax_xmm0();
        }
        return;
    }
    
    // OPTIMIZATION: Check if right operand is a small constant
    int64_t rightConst;
    bool rightIsConst = tryEvalConstant(node.right.get(), rightConst);
    bool rightIsSmall = rightIsConst && rightConst >= INT32_MIN && rightConst <= INT32_MAX;
    
    // OPTIMIZATION: Strength reduction for multiplication
    if (node.op == TokenType::STAR && rightIsConst) {
        node.left->accept(*this);
        
        switch (rightConst) {
            case 0:
                asm_.xor_rax_rax();
                lastExprWasFloat_ = false;
                return;
            case 1:
                lastExprWasFloat_ = false;
                return;
            case 2:
                asm_.shl_rax_imm8(1);
                lastExprWasFloat_ = false;
                return;
            case 3:
                asm_.mov_rcx_rax();
                asm_.lea_rax_rcx_rcx_2();
                lastExprWasFloat_ = false;
                return;
            case 4:
                asm_.shl_rax_imm8(2);
                lastExprWasFloat_ = false;
                return;
            case 8:
                asm_.shl_rax_imm8(3);
                lastExprWasFloat_ = false;
                return;
            case 16:
                asm_.shl_rax_imm8(4);
                lastExprWasFloat_ = false;
                return;
            default:
                if (rightConst > 0 && (rightConst & (rightConst - 1)) == 0) {
                    int shift = 0;
                    int64_t temp = rightConst;
                    while (temp > 1) { temp >>= 1; shift++; }
                    asm_.shl_rax_imm8((uint8_t)shift);
                    lastExprWasFloat_ = false;
                    return;
                }
                break;
        }
        
        if (rightIsSmall) {
            asm_.imul_rax_rax_imm32((int32_t)rightConst);
            lastExprWasFloat_ = false;
            return;
        }
    }
    
    // OPTIMIZATION: Division by power of 2
    if (node.op == TokenType::SLASH && rightIsConst && rightConst > 0 && (rightConst & (rightConst - 1)) == 0) {
        node.left->accept(*this);
        int shift = 0;
        int64_t temp = rightConst;
        while (temp > 1) { temp >>= 1; shift++; }
        asm_.sar_rax_imm8((uint8_t)shift);
        lastExprWasFloat_ = false;
        return;
    }
    
    // Comparisons with small constants
    if (rightIsSmall && (node.op == TokenType::LT || node.op == TokenType::GT ||
                         node.op == TokenType::LE || node.op == TokenType::GE ||
                         node.op == TokenType::EQ || node.op == TokenType::NE)) {
        node.left->accept(*this);
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
    
    // Add/sub with small constants
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
    
    // Default: use push/pop
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

void NativeCodeGen::visit(TernaryExpr& node) {
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

} // namespace tyl
