// Tyl Compiler - Native Code Generator Helpers
// Stack calculations and utility functions
// Note: Constant evaluation moved to codegen_const_eval.cpp
// Note: Type utilities moved to codegen_type_utils.cpp
// Note: Record layout moved to codegen_record_layout.cpp

#include "backend/codegen/codegen_base.h"
#include <cmath>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstdlib>

namespace tyl {

std::string NativeCodeGen::newLabel(const std::string& prefix) {
    return prefix + std::to_string(labelCounter++);
}

uint32_t NativeCodeGen::addString(const std::string& str) {
    auto it = stringOffsets.find(str);
    if (it != stringOffsets.end()) return it->second;
    
    uint32_t rva = pe_.addString(str);
    stringOffsets[str] = rva;
    return rva;
}

uint32_t NativeCodeGen::addFloatConstant(double value) {
    // Store float as 8 bytes in data section
    union { double d; uint8_t bytes[8]; } u;
    u.d = value;
    return pe_.addData(u.bytes, 8);
}

void NativeCodeGen::allocLocal(const std::string& name) {
    stackOffset -= 8;
    locals[name] = stackOffset;
}

// Calculate the maximum stack space needed for a function body
int32_t NativeCodeGen::calculateFunctionStackSize(Statement* body) {
    if (!body) return 0;
    
    int32_t maxStack = 0;
    
    std::function<void(Statement*)> scanStmt = [&](Statement* stmt) {
        if (!stmt) return;
        
        if (auto* block = dynamic_cast<Block*>(stmt)) {
            for (auto& s : block->statements) {
                scanStmt(s.get());
            }
        }
        else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt)) {
            maxStack = std::max(maxStack, calculateExprStackSize(exprStmt->expr.get()));
        }
        else if (auto* varDecl = dynamic_cast<VarDecl*>(stmt)) {
            maxStack = std::max(maxStack, calculateExprStackSize(varDecl->initializer.get()));
        }
        else if (auto* assignStmt = dynamic_cast<AssignStmt*>(stmt)) {
            maxStack = std::max(maxStack, calculateExprStackSize(assignStmt->value.get()));
        }
        else if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
            maxStack = std::max(maxStack, calculateExprStackSize(ifStmt->condition.get()));
            scanStmt(ifStmt->thenBranch.get());
            for (auto& elif : ifStmt->elifBranches) {
                maxStack = std::max(maxStack, calculateExprStackSize(elif.first.get()));
                scanStmt(elif.second.get());
            }
            scanStmt(ifStmt->elseBranch.get());
        }
        else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
            maxStack = std::max(maxStack, calculateExprStackSize(whileStmt->condition.get()));
            scanStmt(whileStmt->body.get());
        }
        else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
            maxStack = std::max(maxStack, calculateExprStackSize(forStmt->iterable.get()));
            scanStmt(forStmt->body.get());
        }
        else if (auto* returnStmt = dynamic_cast<ReturnStmt*>(stmt)) {
            maxStack = std::max(maxStack, calculateExprStackSize(returnStmt->value.get()));
        }
    };
    
    scanStmt(body);
    return maxStack;
}

// Calculate stack space needed for an expression (mainly for calls)
int32_t NativeCodeGen::calculateExprStackSize(Expression* expr) {
    if (!expr) return 0;
    
    int32_t maxStack = 0;
    
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        if (auto* id = dynamic_cast<Identifier*>(call->callee.get())) {
            if (id->name == "print" || id->name == "println") {
                maxStack = std::max(maxStack, (int32_t)0x38);
            } else if (id->name == "hostname" || id->name == "username" || 
                       id->name == "cpu_count" || id->name == "year" ||
                       id->name == "month" || id->name == "day" ||
                       id->name == "hour" || id->name == "minute" ||
                       id->name == "second" || id->name == "now" ||
                       id->name == "now_ms" || id->name == "sleep") {
                maxStack = std::max(maxStack, (int32_t)0x28);
            } else {
                maxStack = std::max(maxStack, (int32_t)0x20);
            }
        } else {
            maxStack = std::max(maxStack, (int32_t)0x20);
        }
        
        for (auto& arg : call->args) {
            maxStack = std::max(maxStack, calculateExprStackSize(arg.get()));
        }
    }
    else if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        maxStack = std::max(maxStack, calculateExprStackSize(binary->left.get()));
        maxStack = std::max(maxStack, calculateExprStackSize(binary->right.get()));
    }
    else if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        maxStack = std::max(maxStack, calculateExprStackSize(unary->operand.get()));
    }
    else if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        maxStack = std::max(maxStack, calculateExprStackSize(ternary->condition.get()));
        maxStack = std::max(maxStack, calculateExprStackSize(ternary->thenExpr.get()));
        maxStack = std::max(maxStack, calculateExprStackSize(ternary->elseExpr.get()));
    }
    else if (auto* walrus = dynamic_cast<WalrusExpr*>(expr)) {
        // Walrus expression allocates a local variable (8 bytes)
        maxStack = std::max(maxStack, (int32_t)8);
        maxStack = std::max(maxStack, calculateExprStackSize(walrus->value.get()));
    }
    
    return maxStack;
}

void NativeCodeGen::emitCallWithOptimizedStack(uint32_t importRVA) {
    asm_.call_mem_rip(importRVA);
}

void NativeCodeGen::emitCallRelWithOptimizedStack(const std::string& label) {
    asm_.call_rel32(label);
}

// Check if a statement ends with a terminator (return, break, continue)
bool NativeCodeGen::endsWithTerminator(Statement* stmt) {
    if (!stmt) return false;
    
    if (dynamic_cast<ReturnStmt*>(stmt)) return true;
    if (dynamic_cast<BreakStmt*>(stmt)) return true;
    if (dynamic_cast<ContinueStmt*>(stmt)) return true;
    
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        if (block->statements.empty()) return false;
        return endsWithTerminator(block->statements.back().get());
    }
    
    if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        if (!ifStmt->elseBranch) return false;
        if (!endsWithTerminator(ifStmt->thenBranch.get())) return false;
        for (auto& elif : ifStmt->elifBranches) {
            if (!endsWithTerminator(elif.second.get())) return false;
        }
        return endsWithTerminator(ifStmt->elseBranch.get());
    }
    
    return false;
}

} // namespace tyl
