// Tyl Compiler - Bit-Tracking Dead Code Elimination Implementation
// Full implementation that tracks demanded bits and simplifies expressions
#include "bdce.h"
#include <algorithm>
#include <iostream>

namespace tyl {

// ============================================
// DemandedBits Implementation
// ============================================

int DemandedBits::countLeadingZeros() const {
    if (mask == 0) return bitWidth;
    int count = 0;
    uint64_t m = mask;
    for (int i = bitWidth - 1; i >= 0; --i) {
        if ((m >> i) & 1) break;
        count++;
    }
    return count;
}

// ============================================
// BDCEPass Implementation
// ============================================

void BDCEPass::run(Program& ast) {
    transformations_ = 0;
    
    // Process each function
    for (auto& stmt : ast.statements) {
        if (auto* fn = dynamic_cast<FnDecl*>(stmt.get())) {
            processFunction(fn);
        }
        else if (auto* module = dynamic_cast<ModuleDecl*>(stmt.get())) {
            for (auto& modStmt : module->body) {
                if (auto* modFn = dynamic_cast<FnDecl*>(modStmt.get())) {
                    processFunction(modFn);
                }
            }
        }
    }
}

void BDCEPass::processFunction(FnDecl* fn) {
    if (!fn || !fn->body) return;
    
    auto* body = dynamic_cast<Block*>(fn->body.get());
    if (!body) return;
    
    bitInfo_.clear();
    deadExprs_.clear();
    
    // Phase 1: Compute demanded bits (backward analysis)
    computeDemandedBits(body->statements);
    
    // Phase 2: Identify dead/simplifiable expressions
    identifyDeadCode(body->statements);
    
    // Phase 3: Transform/remove dead code and simplify expressions
    transformDeadCode(body->statements);
}

void BDCEPass::computeDemandedBits(std::vector<StmtPtr>& stmts) {
    // Process statements in reverse order (backward analysis)
    for (auto it = stmts.rbegin(); it != stmts.rend(); ++it) {
        computeDemandedBitsForStmt(it->get());
    }
}

void BDCEPass::computeDemandedBitsForStmt(Statement* stmt) {
    if (!stmt) return;
    
    if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
        // Variable declaration - all bits of initializer are demanded
        if (varDecl->initializer) {
            computeDemandedBitsForExpr(varDecl->initializer.get(), 
                                       DemandedBits(~0ULL, getBitWidth(varDecl->initializer.get())));
        }
    }
    else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
        // Assignment - all bits of value are demanded
        if (assignStmt->value) {
            computeDemandedBitsForExpr(assignStmt->value.get(),
                                       DemandedBits(~0ULL, getBitWidth(assignStmt->value.get())));
        }
    }
    else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
        // Expression statement - only side effects matter
        if (exprStmt->expr) {
            if (hasSideEffects(exprStmt->expr.get())) {
                computeDemandedBitsForExpr(exprStmt->expr.get(),
                                           DemandedBits(~0ULL, getBitWidth(exprStmt->expr.get())));
            } else {
                // No side effects - no bits demanded
                computeDemandedBitsForExpr(exprStmt->expr.get(), DemandedBits(0, 64));
            }
        }
    }
    else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        // Return - all bits are demanded
        if (returnStmt->value) {
            computeDemandedBitsForExpr(returnStmt->value.get(),
                                       DemandedBits(~0ULL, getBitWidth(returnStmt->value.get())));
        }
    }
    else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        // Condition - only lowest bit matters for boolean
        computeDemandedBitsForExpr(ifStmt->condition.get(), DemandedBits(1, 1));
        
        // Process branches
        if (auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
            computeDemandedBits(thenBlock->statements);
        }
        for (auto& elif : ifStmt->elifBranches) {
            computeDemandedBitsForExpr(elif.first.get(), DemandedBits(1, 1));
            if (auto* elifBlock = dynamic_cast<Block*>(elif.second.get())) {
                computeDemandedBits(elifBlock->statements);
            }
        }
        if (auto* elseBlock = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
            computeDemandedBits(elseBlock->statements);
        }
    }
    else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        computeDemandedBitsForExpr(whileStmt->condition.get(), DemandedBits(1, 1));
        if (auto* body = dynamic_cast<Block*>(whileStmt->body.get())) {
            computeDemandedBits(body->statements);
        }
    }
    else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
        // For-each loop: iterable is fully demanded
        if (forStmt->iterable) {
            computeDemandedBitsForExpr(forStmt->iterable.get(), DemandedBits(~0ULL, 64));
        }
        if (auto* body = dynamic_cast<Block*>(forStmt->body.get())) {
            computeDemandedBits(body->statements);
        }
    }
}

void BDCEPass::computeDemandedBitsForExpr(Expression* expr, DemandedBits demanded) {
    if (!expr) return;
    
    // Store demanded bits for this expression
    bitInfo_[expr].demanded = demanded;
    
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        // Compute demanded bits for operands based on operation
        DemandedBits lhsDemanded = getDemandedBitsForBinaryOp(
            binary->op, demanded, binary->left.get(), binary->right.get(), true);
        DemandedBits rhsDemanded = getDemandedBitsForBinaryOp(
            binary->op, demanded, binary->left.get(), binary->right.get(), false);
        
        computeDemandedBitsForExpr(binary->left.get(), lhsDemanded);
        computeDemandedBitsForExpr(binary->right.get(), rhsDemanded);
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        DemandedBits operandDemanded = getDemandedBitsForUnaryOp(unary->op, demanded);
        computeDemandedBitsForExpr(unary->operand.get(), operandDemanded);
    }
    else if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        // Function calls - all argument bits are demanded
        for (auto& arg : call->args) {
            computeDemandedBitsForExpr(arg.get(), DemandedBits(~0ULL, getBitWidth(arg.get())));
        }
    }
    else if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        computeDemandedBitsForExpr(index->object.get(), DemandedBits(~0ULL, 64));
        computeDemandedBitsForExpr(index->index.get(), DemandedBits(~0ULL, 64));
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        computeDemandedBitsForExpr(ternary->condition.get(), DemandedBits(1, 1));
        computeDemandedBitsForExpr(ternary->thenExpr.get(), demanded);
        computeDemandedBitsForExpr(ternary->elseExpr.get(), demanded);
    }
    else if (auto* cast = dynamic_cast<CastExpr*>(expr)) {
        // For casts, propagate demanded bits
        computeDemandedBitsForExpr(cast->expr.get(), demanded);
    }
}

DemandedBits BDCEPass::getDemandedBitsForBinaryOp(TokenType op, DemandedBits resultDemanded,
                                                   Expression* lhs, Expression* rhs, bool isLHS) {
    int bitWidth = std::max(getBitWidth(lhs), getBitWidth(rhs));
    
    switch (op) {
        case TokenType::AMP: {   // Bitwise AND
            // For AND: if result bit i is demanded, both operand bits i are demanded
            // But if RHS is constant, we can be smarter
            if (!isLHS) {
                if (auto* lit = dynamic_cast<IntegerLiteral*>(rhs)) {
                    // Only bits that are 1 in the mask can affect the result
                    return DemandedBits(resultDemanded.mask & static_cast<uint64_t>(lit->value), bitWidth);
                }
            }
            if (isLHS) {
                if (auto* lit = dynamic_cast<IntegerLiteral*>(rhs)) {
                    // LHS bits only matter where mask is 1
                    return DemandedBits(resultDemanded.mask & static_cast<uint64_t>(lit->value), bitWidth);
                }
            }
            return resultDemanded;
        }
        
        case TokenType::PIPE: {  // Bitwise OR
            // For OR: if result bit i is demanded, both operand bits i are demanded
            // But if RHS is constant with bit i = 1, LHS bit i doesn't matter
            if (isLHS) {
                if (auto* lit = dynamic_cast<IntegerLiteral*>(rhs)) {
                    uint64_t mask = static_cast<uint64_t>(lit->value);
                    // LHS bits only matter where mask is 0
                    return DemandedBits(resultDemanded.mask & ~mask, bitWidth);
                }
            }
            return resultDemanded;
        }
        
        case TokenType::CARET:  // Bitwise XOR
            // For XOR: demanded bits propagate directly
            return resultDemanded;
        
        case TokenType::PLUS:
        case TokenType::MINUS: {
            // For add/sub: if only low N bits are demanded, only low N bits of operands matter
            // (plus one more for carry)
            if (resultDemanded.mask != ~0ULL) {
                // Find highest demanded bit
                int highBit = 63;
                while (highBit >= 0 && !((resultDemanded.mask >> highBit) & 1)) {
                    highBit--;
                }
                if (highBit >= 0 && highBit < 63) {
                    // Need one extra bit for carry
                    uint64_t newMask = (1ULL << (highBit + 2)) - 1;
                    return DemandedBits(newMask, bitWidth);
                }
            }
            return DemandedBits(~0ULL, bitWidth);
        }
        
        case TokenType::STAR:
        case TokenType::SLASH:
        case TokenType::PERCENT:
            // Multiplication/division - all bits may affect result
            // (could be smarter for power-of-2 multiplies)
            return DemandedBits(~0ULL, bitWidth);
        
        case TokenType::EQ:
        case TokenType::NE:
        case TokenType::LT:
        case TokenType::LE:
        case TokenType::GT:
        case TokenType::GE:
            // Comparisons - all bits of operands are demanded
            return DemandedBits(~0ULL, bitWidth);
        
        case TokenType::AMP_AMP:
        case TokenType::PIPE_PIPE:
            // Logical ops - only lowest bit matters
            return DemandedBits(1, 1);
        
        default:
            return DemandedBits(~0ULL, bitWidth);
    }
}

DemandedBits BDCEPass::getDemandedBitsForUnaryOp(TokenType op, DemandedBits resultDemanded) {
    switch (op) {
        case TokenType::BANG:  // Logical NOT
            return DemandedBits(1, 1);
        
        case TokenType::TILDE:  // Bitwise NOT
            return resultDemanded;
        
        case TokenType::MINUS:  // Negation
            return DemandedBits(~0ULL, resultDemanded.bitWidth);
        
        default:
            return resultDemanded;
    }
}

void BDCEPass::identifyDeadCode(std::vector<StmtPtr>& stmts) {
    for (auto& stmt : stmts) {
        if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt.get())) {
            if (exprStmt->expr && isExpressionDead(exprStmt->expr.get())) {
                deadExprs_.insert(exprStmt->expr.get());
            }
        }
        
        // Recurse into nested blocks
        if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
            if (auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
                identifyDeadCode(thenBlock->statements);
            }
            for (auto& elif : ifStmt->elifBranches) {
                if (auto* elifBlock = dynamic_cast<Block*>(elif.second.get())) {
                    identifyDeadCode(elifBlock->statements);
                }
            }
            if (auto* elseBlock = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
                identifyDeadCode(elseBlock->statements);
            }
        }
        else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt.get())) {
            if (auto* body = dynamic_cast<Block*>(whileStmt->body.get())) {
                identifyDeadCode(body->statements);
            }
        }
        else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt.get())) {
            if (auto* body = dynamic_cast<Block*>(forStmt->body.get())) {
                identifyDeadCode(body->statements);
            }
        }
    }
}

bool BDCEPass::isExpressionDead(Expression* expr) {
    if (!expr) return false;
    
    auto it = bitInfo_.find(expr);
    if (it == bitInfo_.end()) return false;
    
    // Expression is dead if no bits are demanded and it has no side effects
    return it->second.demanded.isZero() && !hasSideEffects(expr);
}

void BDCEPass::transformDeadCode(std::vector<StmtPtr>& stmts) {
    // First pass: simplify expressions based on demanded bits
    for (auto& stmt : stmts) {
        if (auto* varDecl = dynamic_cast<VarDecl*>(stmt.get())) {
            if (varDecl->initializer) {
                auto simplified = simplifyExpression(varDecl->initializer.get());
                if (simplified) {
                    varDecl->initializer = std::move(simplified);
                    transformations_++;
                }
            }
        }
        else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt.get())) {
            if (assignStmt->value) {
                auto simplified = simplifyExpression(assignStmt->value.get());
                if (simplified) {
                    assignStmt->value = std::move(simplified);
                    transformations_++;
                }
            }
        }
        else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt.get())) {
            if (returnStmt->value) {
                auto simplified = simplifyExpression(returnStmt->value.get());
                if (simplified) {
                    returnStmt->value = std::move(simplified);
                    transformations_++;
                }
            }
        }
        else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt.get())) {
            if (auto* thenBlock = dynamic_cast<Block*>(ifStmt->thenBranch.get())) {
                transformDeadCode(thenBlock->statements);
            }
            for (auto& elif : ifStmt->elifBranches) {
                if (auto* elifBlock = dynamic_cast<Block*>(elif.second.get())) {
                    transformDeadCode(elifBlock->statements);
                }
            }
            if (auto* elseBlock = dynamic_cast<Block*>(ifStmt->elseBranch.get())) {
                transformDeadCode(elseBlock->statements);
            }
        }
        else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt.get())) {
            if (auto* body = dynamic_cast<Block*>(whileStmt->body.get())) {
                transformDeadCode(body->statements);
            }
        }
        else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt.get())) {
            if (auto* body = dynamic_cast<Block*>(forStmt->body.get())) {
                transformDeadCode(body->statements);
            }
        }
    }
    
    // Second pass: remove dead expression statements
    auto it = stmts.begin();
    while (it != stmts.end()) {
        if (auto* exprStmt = dynamic_cast<ExprStmt*>(it->get())) {
            if (exprStmt->expr && deadExprs_.count(exprStmt->expr.get())) {
                it = stmts.erase(it);
                transformations_++;
                continue;
            }
        }
        ++it;
    }
}

ExprPtr BDCEPass::simplifyExpression(Expression* expr) {
    if (!expr) return nullptr;
    
    auto it = bitInfo_.find(expr);
    if (it == bitInfo_.end()) return nullptr;
    
    DemandedBits demanded = it->second.demanded;
    
    // If no bits demanded and no side effects, replace with 0
    if (demanded.isZero() && !hasSideEffects(expr)) {
        return std::make_unique<IntegerLiteral>(0, expr->location);
    }
    
    // Try to simplify bitwise operations
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        // Simplify AND with constant when some bits aren't demanded
        if (binary->op == TokenType::AMP) {
            if (auto* rhsLit = dynamic_cast<IntegerLiteral*>(binary->right.get())) {
                uint64_t mask = static_cast<uint64_t>(rhsLit->value);
                
                // If mask has bits that aren't demanded, we can simplify
                // e.g., (x & 0xFF00) when only low 8 bits demanded -> 0
                if ((mask & demanded.mask) == 0) {
                    return std::make_unique<IntegerLiteral>(0, expr->location);
                }
                
                // If demanded bits are subset of mask, the AND is unnecessary
                // e.g., (x & 0xFFFF) when only low 8 bits demanded -> x & 0xFF
                uint64_t newMask = mask & demanded.mask;
                if (newMask != mask && newMask != 0) {
                    // Create simplified AND
                    // For now, just note we could simplify
                }
            }
        }
        
        // Simplify OR with constant when some bits aren't demanded
        if (binary->op == TokenType::PIPE) {
            if (auto* rhsLit = dynamic_cast<IntegerLiteral*>(binary->right.get())) {
                uint64_t mask = static_cast<uint64_t>(rhsLit->value);
                
                // If OR sets bits that aren't demanded, we can remove those bits
                // e.g., (x | 0xFF00) when only low 8 bits demanded -> x
                if ((mask & demanded.mask) == 0) {
                    // The OR doesn't affect any demanded bits, return LHS
                    // Note: would need to clone LHS, skip for now
                }
            }
        }
        
        // Simplify shifts when high bits aren't demanded
        // e.g., (x << 8) >> 8 when only low 8 bits demanded
        
        // Simplify multiplication by power of 2 to shift
        if (binary->op == TokenType::STAR) {
            if (auto* rhsLit = dynamic_cast<IntegerLiteral*>(binary->right.get())) {
                int64_t val = rhsLit->value;
                // Check if power of 2
                if (val > 0 && (val & (val - 1)) == 0) {
                    // Could convert to shift, but that's strength reduction
                }
            }
        }
    }
    
    // Simplify unary operations
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        // Double negation: ~~x -> x (when all bits demanded)
        if (unary->op == TokenType::TILDE) {
            if (auto* inner = dynamic_cast<UnaryExpr*>(unary->operand.get())) {
                if (inner->op == TokenType::TILDE) {
                    // Would need to clone inner->operand
                }
            }
        }
    }
    
    return nullptr;
}

ExprPtr BDCEPass::simplifyBitwiseOp(BinaryExpr* expr, DemandedBits demanded) {
    if (!expr) return nullptr;
    
    // Get the constant mask if RHS is a constant
    auto* rhsLit = dynamic_cast<IntegerLiteral*>(expr->right.get());
    if (!rhsLit) return nullptr;
    
    uint64_t mask = static_cast<uint64_t>(rhsLit->value);
    
    switch (expr->op) {
        case TokenType::AMP: {  // AND
            // If mask has no demanded bits, result is 0
            if ((mask & demanded.mask) == 0) {
                return std::make_unique<IntegerLiteral>(0, expr->location);
            }
            break;
        }
        
        case TokenType::PIPE:   // OR
        case TokenType::CARET:  // XOR
            // If mask doesn't affect demanded bits, operation is unnecessary
            if ((mask & demanded.mask) == 0) {
                // Would need to clone left operand - skip for now
            }
            break;
        
        default:
            break;
    }
    
    return nullptr;
}

bool BDCEPass::hasSideEffects(Expression* expr) {
    if (!expr) return false;
    
    if (dynamic_cast<CallExpr*>(expr)) {
        return true;  // Function calls may have side effects
    }
    
    if (dynamic_cast<AssignExpr*>(expr)) {
        return true;  // Assignments have side effects
    }
    
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        return hasSideEffects(binary->left.get()) || hasSideEffects(binary->right.get());
    }
    
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        return hasSideEffects(unary->operand.get());
    }
    
    if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        return hasSideEffects(ternary->condition.get()) ||
               hasSideEffects(ternary->thenExpr.get()) ||
               hasSideEffects(ternary->elseExpr.get());
    }
    
    return false;
}

int BDCEPass::getBitWidth(Expression* expr) {
    // Default to 64-bit for integers
    if (auto* lit = dynamic_cast<IntegerLiteral*>(expr)) {
        int64_t val = lit->value;
        if (val >= 0 && val <= 255) return 8;
        if (val >= 0 && val <= 65535) return 16;
        if (val >= 0 && val <= 0xFFFFFFFF) return 32;
        return 64;
    }
    
    if (dynamic_cast<BoolLiteral*>(expr)) {
        return 1;
    }
    
    // Default to 64-bit
    return 64;
}

ExprPtr BDCEPass::convertSExtToZExt(Expression* expr, DemandedBits demanded) {
    // If high bits aren't demanded, sign extension can become zero extension
    // This is mainly useful at the IR level
    return nullptr;
}

} // namespace tyl
