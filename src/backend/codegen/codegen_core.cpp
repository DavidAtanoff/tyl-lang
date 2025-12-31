// Flex Compiler - Native Code Generator Core
// Main compile entry point, helpers, and constant evaluation

#include "codegen_base.h"
#include "backend/x64/peephole.h"
#include <cmath>
#include <sstream>
#include <iomanip>
#include <iostream>

namespace flex {

NativeCodeGen::NativeCodeGen() : lastExprWasFloat_(false), useOptimizedStackFrame_(true), 
    functionStackSize_(0), stackAllocated_(false), stdoutHandleCached_(false), useStdoutCaching_(true),
    optLevel_(CodeGenOptLevel::O2), runtimeRoutinesEmitted_(false) {
    // Initialize runtime routine labels
    itoaRoutineLabel_ = "__flex_itoa";
    ftoaRoutineLabel_ = "__flex_ftoa";
    printIntRoutineLabel_ = "__flex_print_int";
}

// Dump generated assembly as hex bytes with labels
void NativeCodeGen::dumpAssembly(std::ostream& out) const {
    out << "=== Generated Assembly (" << asm_.code.size() << " bytes) ===\n";
    
    // Create reverse label map (offset -> label name)
    std::map<size_t, std::string> offsetLabels;
    for (const auto& [name, offset] : asm_.labels) {
        offsetLabels[offset] = name;
    }
    
    // Dump bytes with labels
    for (size_t i = 0; i < asm_.code.size(); ) {
        // Check for label at this offset
        auto labelIt = offsetLabels.find(i);
        if (labelIt != offsetLabels.end()) {
            out << "\n" << labelIt->second << ":\n";
        }
        
        // Print offset
        out << std::hex << std::setw(8) << std::setfill('0') << i << ": ";
        
        // Print up to 8 bytes per line
        size_t lineStart = i;
        size_t lineEnd = std::min(i + 8, asm_.code.size());
        
        // Check if there's a label in the middle of this line
        for (size_t j = i + 1; j < lineEnd; j++) {
            if (offsetLabels.count(j)) {
                lineEnd = j;
                break;
            }
        }
        
        // Print hex bytes
        for (size_t j = lineStart; j < lineEnd; j++) {
            out << std::hex << std::setw(2) << std::setfill('0') << (int)asm_.code[j] << " ";
        }
        
        // Pad if less than 8 bytes
        for (size_t j = lineEnd - lineStart; j < 8; j++) {
            out << "   ";
        }
        
        // Try to decode common instructions
        out << " ; ";
        uint8_t b0 = asm_.code[lineStart];
        if (lineEnd - lineStart >= 1) {
            if (b0 == 0xC3) out << "ret";
            else if (b0 == 0x55) out << "push rbp";
            else if (b0 == 0x5D) out << "pop rbp";
            else if (b0 == 0x50) out << "push rax";
            else if (b0 == 0x58) out << "pop rax";
            else if (b0 == 0x51) out << "push rcx";
            else if (b0 == 0x59) out << "pop rcx";
            else if (b0 == 0x52) out << "push rdx";
            else if (b0 == 0x5A) out << "pop rdx";
            else if (b0 == 0x53) out << "push rbx";
            else if (b0 == 0x5B) out << "pop rbx";
            else if (b0 == 0x90) out << "nop";
            else if (b0 == 0xE8) out << "call rel32";
            else if (b0 == 0xE9) out << "jmp rel32";
            else if (b0 == 0xEB) out << "jmp rel8";
            else if (b0 == 0x74) out << "je rel8";
            else if (b0 == 0x75) out << "jne rel8";
            else if (b0 == 0x0F && lineEnd - lineStart >= 2) {
                uint8_t b1 = asm_.code[lineStart + 1];
                if (b1 == 0x84) out << "je rel32";
                else if (b1 == 0x85) out << "jne rel32";
                else if (b1 == 0x8C) out << "jl rel32";
                else if (b1 == 0x8D) out << "jge rel32";
                else if (b1 == 0x8E) out << "jle rel32";
                else if (b1 == 0x8F) out << "jg rel32";
                else if (b1 == 0x94) out << "sete al";
                else if (b1 == 0x95) out << "setne al";
                else if (b1 == 0x9C) out << "setl al";
                else if (b1 == 0x9D) out << "setge al";
                else if (b1 == 0x9E) out << "setle al";
                else if (b1 == 0x9F) out << "setg al";
            }
            else if (b0 == 0x48 && lineEnd - lineStart >= 2) {
                uint8_t b1 = asm_.code[lineStart + 1];
                if (b1 == 0x89 && lineEnd - lineStart >= 3) {
                    uint8_t b2 = asm_.code[lineStart + 2];
                    if (b2 == 0xE5) out << "mov rbp, rsp";
                    else if (b2 == 0xEC) out << "mov rsp, rbp";
                    else if (b2 == 0xC1) out << "mov rcx, rax";
                    else if (b2 == 0xC2) out << "mov rdx, rax";
                    else out << "mov r64, r64";
                }
                else if (b1 == 0x8B) out << "mov r64, [mem]";
                else if (b1 == 0x83) out << "add/sub r64, imm8";
                else if (b1 == 0x81) out << "add/sub r64, imm32";
                else if (b1 == 0x01) out << "add [mem], r64";
                else if (b1 == 0x29) out << "sub [mem], r64";
                else if (b1 == 0x0F && lineEnd - lineStart >= 3) {
                    uint8_t b2 = asm_.code[lineStart + 2];
                    if (b2 == 0xAF) out << "imul r64, r64";
                }
                else if (b1 == 0xF7) out << "idiv/neg r64";
                else if (b1 == 0x99) out << "cqo";
                else if (b1 == 0x85) out << "test r64, r64";
                else if (b1 == 0x3B) out << "cmp r64, r64";
                else if (b1 == 0x3D) out << "cmp rax, imm32";
                else if (b1 == 0xB8) out << "mov rax, imm64";
                else if (b1 == 0x8D) out << "lea r64, [mem]";
            }
            else if (b0 == 0x49 || b0 == 0x4C || b0 == 0x4D) out << "r8-r15 op";
            else if (b0 == 0x41) out << "r8-r15 op";
            else if (b0 == 0xFF) out << "call/jmp [mem]";
            else if (b0 == 0xB8) out << "mov eax, imm32";
            else if (b0 == 0xB9) out << "mov ecx, imm32";
            else if (b0 == 0xBA) out << "mov edx, imm32";
            else if (b0 == 0x31 || b0 == 0x33) out << "xor r32, r32";
        }
        
        out << "\n";
        i = lineEnd;
    }
    
    out << std::dec;  // Reset to decimal
    out << "\n=== End Assembly ===\n";
}

// Emit WriteConsoleA call with cached stdout handle (in RDI)
// This avoids redundant GetStdHandle calls
void NativeCodeGen::emitWriteConsole(uint32_t strRVA, size_t len) {
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x38);
    
    if (useStdoutCaching_ && stdoutHandleCached_) {
        // Use cached handle from RDI
        // mov rcx, rdi
        asm_.mov_rcx_rdi();
    } else {
        // Get stdout handle (first time or caching disabled)
        asm_.mov_ecx_imm32(-11);
        asm_.call_mem_rip(pe_.getImportRVA("GetStdHandle"));
        
        // Cache the handle in RDI if caching is enabled
        if (useStdoutCaching_ && !stdoutHandleCached_) {
            // mov rdi, rax
            asm_.code.push_back(0x48); asm_.code.push_back(0x89); asm_.code.push_back(0xC7);
            stdoutHandleCached_ = true;
        }
        
        asm_.mov_rcx_rax();
    }
    
    asm_.lea_rax_rip_fixup(strRVA);
    asm_.mov_rdx_rax();
    asm_.mov_r8d_imm32((int32_t)len);
    // lea r9, [rsp+0x20]
    asm_.code.push_back(0x4C); asm_.code.push_back(0x8D); asm_.code.push_back(0x4C);
    asm_.code.push_back(0x24); asm_.code.push_back(0x20);
    asm_.xor_rax_rax();
    // mov [rsp+0x28], rax
    asm_.code.push_back(0x48); asm_.code.push_back(0x89); asm_.code.push_back(0x44);
    asm_.code.push_back(0x24); asm_.code.push_back(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("WriteConsoleA"));
    
    if (!stackAllocated_) asm_.add_rsp_imm32(0x38);
}

// Emit WriteConsoleA for buffer in rdx with length in r8
// Uses cached stdout handle in RDI
void NativeCodeGen::emitWriteConsoleBuffer() {
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x38);
    
    if (useStdoutCaching_ && stdoutHandleCached_) {
        // Use cached handle from RDI
        // mov rcx, rdi
        asm_.mov_rcx_rdi();
    } else {
        // Save rdx and r8 before GetStdHandle call
        asm_.push_rdx();
        // push r8
        asm_.code.push_back(0x41); asm_.code.push_back(0x50);
        
        // Get stdout handle
        asm_.mov_ecx_imm32(-11);
        asm_.call_mem_rip(pe_.getImportRVA("GetStdHandle"));
        
        // Cache the handle in RDI if caching is enabled
        if (useStdoutCaching_ && !stdoutHandleCached_) {
            // mov rdi, rax
            asm_.code.push_back(0x48); asm_.code.push_back(0x89); asm_.code.push_back(0xC7);
            stdoutHandleCached_ = true;
        }
        
        asm_.mov_rcx_rax();
        
        // Restore r8 and rdx
        // pop r8
        asm_.code.push_back(0x41); asm_.code.push_back(0x58);
        asm_.pop_rdx();
    }
    
    // lea r9, [rsp+0x20]
    asm_.code.push_back(0x4C); asm_.code.push_back(0x8D); asm_.code.push_back(0x4C);
    asm_.code.push_back(0x24); asm_.code.push_back(0x20);
    asm_.xor_rax_rax();
    // mov [rsp+0x28], rax
    asm_.code.push_back(0x48); asm_.code.push_back(0x89); asm_.code.push_back(0x44);
    asm_.code.push_back(0x24); asm_.code.push_back(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("WriteConsoleA"));
    
    if (!stackAllocated_) asm_.add_rsp_imm32(0x38);
}

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
// This includes shadow space for all calls and local variables
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
        // Each call needs shadow space (0x20) + potential spill space
        // For print/println, we need more (0x38) for WriteConsoleA params
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
        
        // Check arguments
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
    
    return maxStack;
}

// Emit a call to an imported function without stack adjustment (stack already allocated)
void NativeCodeGen::emitCallWithOptimizedStack(uint32_t importRVA) {
    // Stack is already allocated in function prologue, just call
    asm_.call_mem_rip(importRVA);
}

// Emit a relative call without stack adjustment
void NativeCodeGen::emitCallRelWithOptimizedStack(const std::string& label) {
    asm_.call_rel32(label);
}

// Check if a statement ends with a terminator (return, break, continue)
// This is used to avoid emitting dead jumps after terminating statements
bool NativeCodeGen::endsWithTerminator(Statement* stmt) {
    if (!stmt) return false;
    
    // Direct terminators
    if (dynamic_cast<ReturnStmt*>(stmt)) return true;
    if (dynamic_cast<BreakStmt*>(stmt)) return true;
    if (dynamic_cast<ContinueStmt*>(stmt)) return true;
    
    // Block: check last statement
    if (auto* block = dynamic_cast<Block*>(stmt)) {
        if (block->statements.empty()) return false;
        return endsWithTerminator(block->statements.back().get());
    }
    
    // If statement: terminates only if ALL branches terminate
    if (auto* ifStmt = dynamic_cast<IfStmt*>(stmt)) {
        // Must have an else branch for all paths to terminate
        if (!ifStmt->elseBranch) return false;
        
        // Check then branch
        if (!endsWithTerminator(ifStmt->thenBranch.get())) return false;
        
        // Check all elif branches
        for (auto& elif : ifStmt->elifBranches) {
            if (!endsWithTerminator(elif.second.get())) return false;
        }
        
        // Check else branch
        return endsWithTerminator(ifStmt->elseBranch.get());
    }
    
    return false;
}

bool NativeCodeGen::tryEvalConstant(Expression* expr, int64_t& outValue) {
    if (auto* intLit = dynamic_cast<IntegerLiteral*>(expr)) {
        outValue = intLit->value;
        return true;
    }
    if (auto* boolLit = dynamic_cast<BoolLiteral*>(expr)) {
        outValue = boolLit->value ? 1 : 0;
        return true;
    }
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        auto it = constVars.find(ident->name);
        if (it != constVars.end()) {
            outValue = it->second;
            return true;
        }
        return false;
    }
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        int64_t left, right;
        if (tryEvalConstant(binary->left.get(), left) && tryEvalConstant(binary->right.get(), right)) {
            switch (binary->op) {
                case TokenType::PLUS: outValue = left + right; return true;
                case TokenType::MINUS: outValue = left - right; return true;
                case TokenType::STAR: outValue = left * right; return true;
                case TokenType::SLASH: if (right != 0) { outValue = left / right; return true; } break;
                case TokenType::PERCENT: if (right != 0) { outValue = left % right; return true; } break;
                case TokenType::LT: outValue = left < right ? 1 : 0; return true;
                case TokenType::GT: outValue = left > right ? 1 : 0; return true;
                case TokenType::LE: outValue = left <= right ? 1 : 0; return true;
                case TokenType::GE: outValue = left >= right ? 1 : 0; return true;
                case TokenType::EQ: outValue = left == right ? 1 : 0; return true;
                case TokenType::NE: outValue = left != right ? 1 : 0; return true;
                default: break;
            }
        }
    }
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        int64_t val;
        if (tryEvalConstant(unary->operand.get(), val)) {
            switch (unary->op) {
                case TokenType::MINUS: outValue = -val; return true;
                case TokenType::NOT: outValue = !val ? 1 : 0; return true;
                default: break;
            }
        }
    }
    return false;
}

bool NativeCodeGen::tryEvalConstantFloat(Expression* expr, double& outValue) {
    if (auto* floatLit = dynamic_cast<FloatLiteral*>(expr)) {
        outValue = floatLit->value;
        return true;
    }
    if (auto* intLit = dynamic_cast<IntegerLiteral*>(expr)) {
        outValue = static_cast<double>(intLit->value);
        return true;
    }
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        auto it = constFloatVars.find(ident->name);
        if (it != constFloatVars.end()) {
            outValue = it->second;
            return true;
        }
        // Check int constants too
        auto intIt = constVars.find(ident->name);
        if (intIt != constVars.end()) {
            outValue = static_cast<double>(intIt->second);
            return true;
        }
        return false;
    }
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        double left, right;
        if (tryEvalConstantFloat(binary->left.get(), left) && tryEvalConstantFloat(binary->right.get(), right)) {
            switch (binary->op) {
                case TokenType::PLUS: outValue = left + right; return true;
                case TokenType::MINUS: outValue = left - right; return true;
                case TokenType::STAR: outValue = left * right; return true;
                case TokenType::SLASH: if (right != 0.0) { outValue = left / right; return true; } break;
                default: break;
            }
        }
    }
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        double val;
        if (tryEvalConstantFloat(unary->operand.get(), val)) {
            switch (unary->op) {
                case TokenType::MINUS: outValue = -val; return true;
                default: break;
            }
        }
    }
    return false;
}

bool NativeCodeGen::isFloatExpression(Expression* expr) {
    if (dynamic_cast<FloatLiteral*>(expr)) return true;
    
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        if (floatVars.count(ident->name)) return true;
        if (constFloatVars.count(ident->name)) return true;
    }
    
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        return isFloatExpression(binary->left.get()) || isFloatExpression(binary->right.get());
    }
    
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        return isFloatExpression(unary->operand.get());
    }
    
    if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        return isFloatExpression(ternary->thenExpr.get()) || isFloatExpression(ternary->elseExpr.get());
    }
    
    return false;
}

// Check if an expression returns a string pointer at runtime
// This includes: platform(), arch(), upper(), ternary with strings, string variables
bool NativeCodeGen::isStringReturningExpr(Expression* expr) {
    if (!expr) return false;
    
    // String literals are handled separately
    if (dynamic_cast<StringLiteral*>(expr)) return true;
    if (dynamic_cast<InterpolatedString*>(expr)) return true;
    
    // Check for built-in functions that return strings
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        if (auto* id = dynamic_cast<Identifier*>(call->callee.get())) {
            // These built-ins return string pointers
            if (id->name == "platform" || id->name == "arch" ||
                id->name == "upper" || id->name == "hostname" ||
                id->name == "username" || id->name == "str") {
                return true;
            }
        }
    }
    
    // Ternary with string branches
    if (auto* ternary = dynamic_cast<TernaryExpr*>(expr)) {
        return isStringReturningExpr(ternary->thenExpr.get()) || 
               isStringReturningExpr(ternary->elseExpr.get());
    }
    
    // String variable
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        if (constStrVars.count(ident->name)) return true;
    }
    
    return false;
}

// Print a string from pointer in rax - calculates length at runtime using strlen
void NativeCodeGen::emitPrintStringPtr() {
    // rax contains string pointer
    // We need to calculate strlen and then call WriteConsoleA
    
    // Save string pointer
    asm_.push_rax();
    
    // Calculate strlen: loop until we find null terminator
    // mov rcx, rax (string pointer)
    asm_.mov_rcx_rax();
    // xor rax, rax (length counter)
    asm_.xor_rax_rax();
    
    std::string loopLabel = newLabel("strlen_loop");
    std::string doneLabel = newLabel("strlen_done");
    
    asm_.label(loopLabel);
    // movzx rdx, byte [rcx + rax]
    asm_.code.push_back(0x48); asm_.code.push_back(0x0F); asm_.code.push_back(0xB6);
    asm_.code.push_back(0x14); asm_.code.push_back(0x01);  // movzx rdx, byte [rcx + rax]
    // test dl, dl
    asm_.code.push_back(0x84); asm_.code.push_back(0xD2);
    // jz done
    asm_.jz_rel32(doneLabel);
    // inc rax
    asm_.inc_rax();
    // jmp loop
    asm_.jmp_rel32(loopLabel);
    
    asm_.label(doneLabel);
    // rax = length
    // mov r8, rax (length for WriteConsoleA)
    asm_.mov_r8_rax();
    
    // Pop string pointer into rdx
    asm_.pop_rdx();
    
    // Call WriteConsoleA with cached handle
    emitWriteConsoleBuffer();
}

bool NativeCodeGen::tryEvalConstantString(Expression* expr, std::string& outValue) {
    if (auto* strLit = dynamic_cast<StringLiteral*>(expr)) {
        outValue = strLit->value;
        return true;
    }
    if (auto* interp = dynamic_cast<InterpolatedString*>(expr)) {
        std::string result;
        for (auto& part : interp->parts) {
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
                    return false;
                }
            }
        }
        outValue = result;
        return true;
    }
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        auto it = constStrVars.find(ident->name);
        if (it != constStrVars.end() && !it->second.empty()) {
            outValue = it->second;
            return true;
        }
        return false;
    }
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        if (binary->op == TokenType::PLUS) {
            std::string left, right;
            if (tryEvalConstantString(binary->left.get(), left) && tryEvalConstantString(binary->right.get(), right)) {
                outValue = left + right;
                return true;
            }
            int64_t intVal;
            if (dynamic_cast<StringLiteral*>(binary->left.get()) || 
                (dynamic_cast<Identifier*>(binary->left.get()) && 
                 constStrVars.count(dynamic_cast<Identifier*>(binary->left.get())->name))) {
                if (tryEvalConstantString(binary->left.get(), left) && tryEvalConstant(binary->right.get(), intVal)) {
                    outValue = left + std::to_string(intVal);
                    return true;
                }
            }
            if (dynamic_cast<StringLiteral*>(binary->right.get()) || 
                (dynamic_cast<Identifier*>(binary->right.get()) && 
                 constStrVars.count(dynamic_cast<Identifier*>(binary->right.get())->name))) {
                if (tryEvalConstant(binary->left.get(), intVal) && tryEvalConstantString(binary->right.get(), right)) {
                    outValue = std::to_string(intVal) + right;
                    return true;
                }
            }
        }
    }
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        if (auto* id = dynamic_cast<Identifier*>(call->callee.get())) {
            if (id->name == "str" && call->args.size() == 1) {
                int64_t intVal;
                if (tryEvalConstant(call->args[0].get(), intVal)) {
                    outValue = std::to_string(intVal);
                    return true;
                }
                std::string strVal;
                if (tryEvalConstantString(call->args[0].get(), strVal)) {
                    outValue = strVal;
                    return true;
                }
            }
        }
    }
    return false;
}

bool NativeCodeGen::compile(Program& program, const std::string& outputFile) {
    pe_.addImport("kernel32.dll", "GetStdHandle");
    pe_.addImport("kernel32.dll", "WriteConsoleA");
    pe_.addImport("kernel32.dll", "ExitProcess");
    pe_.addImport("kernel32.dll", "GetProcessHeap");
    pe_.addImport("kernel32.dll", "HeapAlloc");
    pe_.addImport("kernel32.dll", "HeapFree");
    pe_.addImport("kernel32.dll", "GetComputerNameA");
    pe_.addImport("kernel32.dll", "GetSystemInfo");
    pe_.addImport("kernel32.dll", "Sleep");
    pe_.addImport("kernel32.dll", "GetLocalTime");
    pe_.addImport("kernel32.dll", "GetTickCount64");
    pe_.addImport("kernel32.dll", "GetEnvironmentVariableA");
    // Async/threading support
    pe_.addImport("kernel32.dll", "CreateThread");
    pe_.addImport("kernel32.dll", "WaitForSingleObject");
    pe_.addImport("kernel32.dll", "GetExitCodeThread");
    pe_.addImport("kernel32.dll", "CloseHandle");
    
    pe_.finalizeImports();
    
    addString("%d");
    addString("\r\n");
    
    std::vector<uint8_t> itoaBuf(32, 0);
    itoaBufferRVA_ = pe_.addData(itoaBuf.data(), itoaBuf.size());
    
    // Pre-scan for constants (both int and float) and lists
    for (auto& stmt : program.statements) {
        if (auto* varDecl = dynamic_cast<VarDecl*>(stmt.get())) {
            if (varDecl->initializer) {
                // Check if it's a list expression
                if (auto* list = dynamic_cast<ListExpr*>(varDecl->initializer.get())) {
                    listSizes[varDecl->name] = list->elements.size();
                    std::vector<int64_t> values;
                    bool allConst = true;
                    for (auto& elem : list->elements) {
                        int64_t val;
                        if (tryEvalConstant(elem.get(), val)) {
                            values.push_back(val);
                        } else {
                            allConst = false;
                            break;
                        }
                    }
                    if (allConst) {
                        constListVars[varDecl->name] = values;
                    }
                    continue;
                }
                
                // Check if it's a float expression
                if (isFloatExpression(varDecl->initializer.get())) {
                    floatVars.insert(varDecl->name);
                    double floatVal;
                    if (tryEvalConstantFloat(varDecl->initializer.get(), floatVal)) {
                        if (!varDecl->isMutable) {
                            constFloatVars[varDecl->name] = floatVal;
                        }
                    }
                    continue;
                }
                
                // Only track as compile-time constant if isConst (::)
                if (varDecl->isConst) {
                    // Try int
                    int64_t intVal;
                    if (tryEvalConstant(varDecl->initializer.get(), intVal)) {
                        constVars[varDecl->name] = intVal;
                    }
                    
                    // Try string
                    std::string strVal;
                    if (tryEvalConstantString(varDecl->initializer.get(), strVal)) {
                        constStrVars[varDecl->name] = strVal;
                    }
                }
            }
        }
        // Also handle AssignExpr wrapped in ExprStmt (for x = value syntax)
        else if (auto* exprStmt = dynamic_cast<ExprStmt*>(stmt.get())) {
            if (auto* assignExpr = dynamic_cast<AssignExpr*>(exprStmt->expr.get())) {
                if (auto* ident = dynamic_cast<Identifier*>(assignExpr->target.get())) {
                    if (assignExpr->op == TokenType::ASSIGN) {
                        // Check if it's a list expression
                        if (auto* list = dynamic_cast<ListExpr*>(assignExpr->value.get())) {
                            listSizes[ident->name] = list->elements.size();
                            std::vector<int64_t> values;
                            bool allConst = true;
                            for (auto& elem : list->elements) {
                                int64_t val;
                                if (tryEvalConstant(elem.get(), val)) {
                                    values.push_back(val);
                                } else {
                                    allConst = false;
                                    break;
                                }
                            }
                            if (allConst) {
                                constListVars[ident->name] = values;
                            }
                        }
                        // Check if it's a float expression
                        else if (isFloatExpression(assignExpr->value.get())) {
                            floatVars.insert(ident->name);
                            double floatVal;
                            if (tryEvalConstantFloat(assignExpr->value.get(), floatVal)) {
                                constFloatVars[ident->name] = floatVal;
                            }
                        }
                        // Check for string (including ternary with strings)
                        else if (isStringReturningExpr(assignExpr->value.get())) {
                            std::string strVal;
                            if (tryEvalConstantString(assignExpr->value.get(), strVal)) {
                                constStrVars[ident->name] = strVal;
                            } else {
                                constStrVars[ident->name] = "";  // Mark as string variable
                            }
                        }
                        // Check for int constant
                        else {
                            int64_t intVal;
                            if (tryEvalConstant(assignExpr->value.get(), intVal)) {
                                constVars[ident->name] = intVal;
                            }
                        }
                    }
                }
            }
        }
    }
    
    program.accept(*this);
    
    // Emit shared runtime routines at the end (for O0/O1/O2)
    emitRuntimeRoutines();
    
    asm_.resolve(PEGenerator::CODE_RVA);
    
    // Apply peephole optimizations to the generated machine code
    PeepholeOptimizer peephole;
    peephole.optimize(asm_.code);
    
    pe_.addCodeWithFixups(asm_.code, asm_.ripFixups);
    
    return pe_.write(outputFile);
}

// Helper to emit code that prints a single expression
// Handles string concatenation by printing parts separately
void NativeCodeGen::emitPrintExpr(Expression* expr) {
    // First, try to evaluate the entire expression as a constant string
    // This handles cases like: "x = " + str(x) + " (expected 4)" when x is known
    std::string constStr;
    if (tryEvalConstantString(expr, constStr)) {
        uint32_t strRVA = addString(constStr);
        size_t len = constStr.length();
        emitWriteConsole(strRVA, len);
        return;
    }
    
    // Handle InterpolatedString with runtime variables
    // Print each part separately - string literals directly, expressions via runtime conversion
    if (auto* interp = dynamic_cast<InterpolatedString*>(expr)) {
        for (auto& part : interp->parts) {
            if (auto* str = std::get_if<std::string>(&part)) {
                // Static string part - print directly
                if (!str->empty()) {
                    uint32_t strRVA = addString(*str);
                    size_t len = str->length();
                    emitWriteConsole(strRVA, len);
                }
            } else if (auto* exprPtr = std::get_if<ExprPtr>(&part)) {
                // Expression part - try constant eval first, then runtime
                std::string strVal;
                int64_t intVal;
                double floatVal;
                
                if (tryEvalConstantString(exprPtr->get(), strVal)) {
                    // Constant string
                    uint32_t strRVA = addString(strVal);
                    size_t len = strVal.length();
                    emitWriteConsole(strRVA, len);
                } else if (tryEvalConstantFloat(exprPtr->get(), floatVal) && isFloatExpression(exprPtr->get())) {
                    // Constant float - check isFloatExpression to avoid treating ints as floats
                    std::ostringstream oss;
                    oss << std::fixed << std::setprecision(6) << floatVal;
                    std::string numStr = oss.str();
                    uint32_t strRVA = addString(numStr);
                    size_t len = numStr.length();
                    emitWriteConsole(strRVA, len);
                } else if (tryEvalConstant(exprPtr->get(), intVal)) {
                    // Constant int
                    std::string numStr = std::to_string(intVal);
                    uint32_t strRVA = addString(numStr);
                    size_t len = numStr.length();
                    emitWriteConsole(strRVA, len);
                } else if (isFloatExpression(exprPtr->get())) {
                    // Runtime float
                    (*exprPtr)->accept(*this);
                    emitFtoaCall();
                    asm_.mov_rdx_rax();
                    asm_.mov_r8_rcx();
                    emitWriteConsoleBuffer();
                } else if (isStringReturningExpr(exprPtr->get())) {
                    // Expression returns a string pointer (e.g., platform(), arch(), ternary with strings)
                    // Evaluate to get string pointer, then print using strlen
                    (*exprPtr)->accept(*this);
                    emitPrintStringPtr();
                } else {
                    // Runtime int/other - evaluate and print
                    (*exprPtr)->accept(*this);
                    emitPrintIntCall();
                }
            }
        }
        return;
    }
    
    // Check for string concatenation (BinaryExpr with PLUS)
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        if (binary->op == TokenType::PLUS) {
            // Check if either side involves strings
            bool leftIsString = dynamic_cast<StringLiteral*>(binary->left.get()) ||
                               dynamic_cast<InterpolatedString*>(binary->left.get());
            bool rightIsString = dynamic_cast<StringLiteral*>(binary->right.get()) ||
                                dynamic_cast<InterpolatedString*>(binary->right.get());
            
            // Check for str() calls
            if (auto* call = dynamic_cast<CallExpr*>(binary->left.get())) {
                if (auto* id = dynamic_cast<Identifier*>(call->callee.get())) {
                    if (id->name == "str") leftIsString = true;
                }
            }
            if (auto* call = dynamic_cast<CallExpr*>(binary->right.get())) {
                if (auto* id = dynamic_cast<Identifier*>(call->callee.get())) {
                    if (id->name == "str") rightIsString = true;
                }
            }
            
            // Check for nested binary (string concat chain)
            if (dynamic_cast<BinaryExpr*>(binary->left.get())) leftIsString = true;
            if (dynamic_cast<BinaryExpr*>(binary->right.get())) rightIsString = true;
            
            if (leftIsString || rightIsString) {
                // Print left side, then right side
                emitPrintExpr(binary->left.get());
                emitPrintExpr(binary->right.get());
                return;
            }
        }
    }
    
    // Handle str() call - convert int to string and print
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        if (auto* id = dynamic_cast<Identifier*>(call->callee.get())) {
            if (id->name == "str" && call->args.size() == 1) {
                // Try constant evaluation first
                int64_t intVal;
                if (tryEvalConstant(call->args[0].get(), intVal)) {
                    std::string numStr = std::to_string(intVal);
                    uint32_t strRVA = addString(numStr);
                    size_t len = numStr.length();
                    emitWriteConsole(strRVA, len);
                    return;
                }
                
                // Runtime: evaluate argument, convert to string, print
                call->args[0]->accept(*this);
                emitItoa();
                
                // rax = string pointer, rcx = length
                // Move to rdx and r8 for WriteConsoleA
                asm_.mov_rdx_rax();
                asm_.mov_r8_rcx();
                emitWriteConsoleBuffer();
                return;
            }
        }
    }
    
    // Handle string literal
    if (auto* strLit = dynamic_cast<StringLiteral*>(expr)) {
        uint32_t strRVA = addString(strLit->value);
        size_t len = strLit->value.length();
        emitWriteConsole(strRVA, len);
        return;
    }
    
    // Handle float literal directly
    if (auto* floatLit = dynamic_cast<FloatLiteral*>(expr)) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(6) << floatLit->value;
        std::string numStr = oss.str();
        uint32_t strRVA = addString(numStr);
        size_t len = numStr.length();
        emitWriteConsole(strRVA, len);
        return;
    }
    
    // Check if expression is a float - handle specially
    if (isFloatExpression(expr)) {
        // Try constant float evaluation first
        double floatVal;
        if (tryEvalConstantFloat(expr, floatVal)) {
            // Format float to string at compile time
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(6) << floatVal;
            std::string numStr = oss.str();
            uint32_t strRVA = addString(numStr);
            size_t len = numStr.length();
            emitWriteConsole(strRVA, len);
            return;
        }
        
        // Runtime float - use ftoa conversion (shared or inline based on opt level)
        expr->accept(*this);
        emitFtoaCall();
        
        // rax = string pointer, rcx = length
        asm_.mov_rdx_rax();
        asm_.mov_r8_rcx();
        emitWriteConsoleBuffer();
        return;
    }
    
    // Handle identifier (variable)
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        // Check if it's a known constant string
        auto strIt = constStrVars.find(ident->name);
        if (strIt != constStrVars.end() && !strIt->second.empty()) {
            uint32_t strRVA = addString(strIt->second);
            size_t len = strIt->second.length();
            emitWriteConsole(strRVA, len);
            return;
        }
        
        // Check if it's a known constant float
        auto floatIt = constFloatVars.find(ident->name);
        if (floatIt != constFloatVars.end()) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(6) << floatIt->second;
            std::string numStr = oss.str();
            uint32_t strRVA = addString(numStr);
            size_t len = numStr.length();
            emitWriteConsole(strRVA, len);
            return;
        }
        
        // Check if it's a runtime float variable
        if (floatVars.count(ident->name)) {
            expr->accept(*this);  // Load float into xmm0
            emitFtoaCall();       // Convert to string (shared or inline based on opt level)
            
            // rax = string pointer, rcx = length
            asm_.mov_rdx_rax();
            asm_.mov_r8_rcx();
            emitWriteConsoleBuffer();
            return;
        }
        
        // Check if it's a known constant int
        auto intIt = constVars.find(ident->name);
        if (intIt != constVars.end()) {
            std::string numStr = std::to_string(intIt->second);
            uint32_t strRVA = addString(numStr);
            size_t len = numStr.length();
            emitWriteConsole(strRVA, len);
            return;
        }
        
        // Check if it's a string variable (marked in constStrVars with empty value)
        if (strIt != constStrVars.end()) {
            // Runtime string variable - load pointer and print using strlen
            expr->accept(*this);  // Load string pointer into rax
            emitPrintStringPtr(); // Print using runtime strlen
            return;
        }
        
        // Runtime variable - load and print as int
        expr->accept(*this);
        emitPrintIntCall();  // Use shared routine for O0/O1/O2
        return;
    }
    
    // Default: evaluate as int and print
    int64_t intVal;
    if (tryEvalConstant(expr, intVal)) {
        std::string numStr = std::to_string(intVal);
        uint32_t strRVA = addString(numStr);
        size_t len = numStr.length();
        emitWriteConsole(strRVA, len);
        return;
    }
    
    // Runtime evaluation
    expr->accept(*this);
    emitPrintIntCall();  // Use shared routine for O0/O1/O2
}

// Float to ASCII conversion - converts float in xmm0 to string
// Returns: rax = pointer to string buffer, rcx = length
// Uses a dedicated buffer in the data section
void NativeCodeGen::emitFtoa() {
    // We'll use a second buffer for ftoa (separate from itoa)
    // Allocate ftoa buffer if not already done
    static uint32_t ftoaBufferRVA = 0;
    if (ftoaBufferRVA == 0) {
        std::vector<uint8_t> ftoaBuf(64, 0);  // 64 bytes for float string
        ftoaBufferRVA = pe_.addData(ftoaBuf.data(), ftoaBuf.size());
    }
    
    // Labels for control flow
    std::string posLabel = newLabel("ftoa_pos");
    std::string zeroIntLabel = newLabel("ftoa_zero_int");
    std::string nonZeroIntLabel = newLabel("ftoa_nonzero_int");
    std::string intLoopLabel = newLabel("ftoa_int_loop");
    std::string intDoneLabel = newLabel("ftoa_int_done");
    std::string revCheckLabel = newLabel("ftoa_rev_check");
    std::string revLoopLabel = newLabel("ftoa_rev_loop");
    std::string revDoneLabel = newLabel("ftoa_rev_done");
    std::string doneLabel = newLabel("ftoa_done");
    
    // Allocate stack space for local variables
    allocLocal("$ftoa_float");    // Original float value (positive)
    allocLocal("$ftoa_bufptr");   // Current buffer position
    allocLocal("$ftoa_start");    // Start of integer digits (for reversal)
    allocLocal("$ftoa_intpart");  // Integer part
    allocLocal("$ftoa_fracpart"); // Fractional part as integer
    allocLocal("$ftoa_left");     // Left pointer for reversal
    allocLocal("$ftoa_right");    // Right pointer for reversal
    allocLocal("$ftoa_tmp");      // Temp storage
    
    // Save the float value
    asm_.movsd_mem_rbp_xmm0(locals["$ftoa_float"]);
    
    // Initialize buffer pointer
    asm_.lea_rax_rip_fixup(ftoaBufferRVA);
    asm_.mov_mem_rbp_rax(locals["$ftoa_bufptr"]);
    
    // Check if negative (test sign bit)
    asm_.movq_rax_xmm0();
    asm_.test_rax_rax();  // Sign bit is in MSB, so negative if < 0
    asm_.jge_rel32(posLabel);  // Jump if positive (sign bit = 0)
    
    // Negative: write '-' and negate the float
    asm_.mov_rax_mem_rbp(locals["$ftoa_bufptr"]);
    asm_.mov_rcx_imm64('-');
    asm_.code.push_back(0x88); asm_.code.push_back(0x08);  // mov byte [rax], cl
    asm_.inc_rax();
    asm_.mov_mem_rbp_rax(locals["$ftoa_bufptr"]);
    
    // Negate: XOR with sign bit
    asm_.movsd_xmm0_mem_rbp(locals["$ftoa_float"]);
    asm_.mov_rcx_imm64(0x8000000000000000LL);
    asm_.movq_xmm1_rcx();
    asm_.xorpd_xmm0_xmm1();
    asm_.movsd_mem_rbp_xmm0(locals["$ftoa_float"]);
    
    asm_.label(posLabel);
    
    // Extract integer part
    asm_.movsd_xmm0_mem_rbp(locals["$ftoa_float"]);
    asm_.cvttsd2si_rax_xmm0();
    asm_.mov_mem_rbp_rax(locals["$ftoa_intpart"]);
    
    // Calculate fractional part: (float - int) * 1000000
    asm_.cvtsi2sd_xmm1_rax();  // xmm1 = int as double
    asm_.movsd_xmm0_mem_rbp(locals["$ftoa_float"]);
    asm_.subsd_xmm0_xmm1();    // xmm0 = fractional part
    asm_.mov_rax_imm64(1000000);
    asm_.cvtsi2sd_xmm1_rax();  // xmm1 = 1000000.0
    asm_.mulsd_xmm0_xmm1();    // xmm0 = frac * 1000000
    asm_.cvttsd2si_rax_xmm0(); // rax = fractional digits
    // Handle negative fractional (can happen due to floating point)
    asm_.test_rax_rax();
    std::string fracPosLabel = newLabel("ftoa_frac_pos");
    asm_.jge_rel32(fracPosLabel);
    asm_.neg_rax();
    asm_.label(fracPosLabel);
    asm_.mov_mem_rbp_rax(locals["$ftoa_fracpart"]);
    
    // Convert integer part to string
    // Save start position for reversal
    asm_.mov_rax_mem_rbp(locals["$ftoa_bufptr"]);
    asm_.mov_mem_rbp_rax(locals["$ftoa_start"]);
    
    // Check if integer part is 0
    asm_.mov_rax_mem_rbp(locals["$ftoa_intpart"]);
    asm_.test_rax_rax();
    asm_.jnz_rel32(nonZeroIntLabel);
    
    // Integer is 0, write '0'
    asm_.label(zeroIntLabel);
    asm_.mov_rax_mem_rbp(locals["$ftoa_bufptr"]);
    asm_.mov_rcx_imm64('0');
    asm_.code.push_back(0x88); asm_.code.push_back(0x08);  // mov byte [rax], cl
    asm_.inc_rax();
    asm_.mov_mem_rbp_rax(locals["$ftoa_bufptr"]);
    asm_.jmp_rel32(intDoneLabel);
    
    // Integer is non-zero, convert digit by digit (in reverse)
    asm_.label(nonZeroIntLabel);
    
    asm_.label(intLoopLabel);
    asm_.mov_rax_mem_rbp(locals["$ftoa_intpart"]);
    asm_.test_rax_rax();
    asm_.jz_rel32(revCheckLabel);  // Done with digits, go reverse
    
    // digit = intpart % 10, intpart = intpart / 10
    asm_.mov_rcx_imm64(10);
    asm_.cqo();
    asm_.idiv_rcx();
    // rax = quotient, rdx = remainder (digit)
    asm_.mov_mem_rbp_rax(locals["$ftoa_intpart"]);  // Save quotient
    
    // Write digit (rdx + '0')
    asm_.mov_rax_mem_rbp(locals["$ftoa_bufptr"]);
    asm_.code.push_back(0x80); asm_.code.push_back(0xC2); asm_.code.push_back('0');  // add dl, '0'
    asm_.code.push_back(0x88); asm_.code.push_back(0x10);  // mov byte [rax], dl
    asm_.inc_rax();
    asm_.mov_mem_rbp_rax(locals["$ftoa_bufptr"]);
    
    asm_.jmp_rel32(intLoopLabel);
    
    // Reverse the integer digits in place
    asm_.label(revCheckLabel);
    asm_.mov_rax_mem_rbp(locals["$ftoa_start"]);
    asm_.mov_mem_rbp_rax(locals["$ftoa_left"]);
    asm_.mov_rax_mem_rbp(locals["$ftoa_bufptr"]);
    asm_.dec_rax();
    asm_.mov_mem_rbp_rax(locals["$ftoa_right"]);
    
    asm_.label(revLoopLabel);
    asm_.mov_rax_mem_rbp(locals["$ftoa_left"]);
    asm_.mov_rcx_mem_rbp(locals["$ftoa_right"]);
    asm_.cmp_rax_rcx();
    asm_.jge_rel32(intDoneLabel);  // if left >= right, done reversing
    
    // Swap bytes at [left] and [right]
    // Load both bytes
    asm_.mov_rax_mem_rbp(locals["$ftoa_left"]);
    asm_.code.push_back(0x0F); asm_.code.push_back(0xB6); asm_.code.push_back(0x10);  // movzx edx, byte [rax]
    asm_.mov_mem_rbp_rdx(locals["$ftoa_tmp"]);  // Save left byte
    
    asm_.mov_rcx_mem_rbp(locals["$ftoa_right"]);
    asm_.code.push_back(0x0F); asm_.code.push_back(0xB6); asm_.code.push_back(0x11);  // movzx edx, byte [rcx]
    
    // Write right byte to left position
    asm_.mov_rax_mem_rbp(locals["$ftoa_left"]);
    asm_.code.push_back(0x88); asm_.code.push_back(0x10);  // mov byte [rax], dl
    
    // Write saved left byte to right position
    asm_.mov_rdx_mem_rbp(locals["$ftoa_tmp"]);
    asm_.mov_rcx_mem_rbp(locals["$ftoa_right"]);
    asm_.code.push_back(0x88); asm_.code.push_back(0x11);  // mov byte [rcx], dl
    
    // left++, right--
    asm_.mov_rax_mem_rbp(locals["$ftoa_left"]);
    asm_.inc_rax();
    asm_.mov_mem_rbp_rax(locals["$ftoa_left"]);
    asm_.mov_rax_mem_rbp(locals["$ftoa_right"]);
    asm_.dec_rax();
    asm_.mov_mem_rbp_rax(locals["$ftoa_right"]);
    asm_.jmp_rel32(revLoopLabel);
    
    asm_.label(intDoneLabel);
    
    // Write decimal point
    asm_.mov_rax_mem_rbp(locals["$ftoa_bufptr"]);
    asm_.mov_rcx_imm64('.');
    asm_.code.push_back(0x88); asm_.code.push_back(0x08);  // mov byte [rax], cl
    asm_.inc_rax();
    asm_.mov_mem_rbp_rax(locals["$ftoa_bufptr"]);
    
    // Write 6 fractional digits with leading zeros
    // We need to write digits from most significant to least
    // frac / 100000 % 10, frac / 10000 % 10, etc.
    
    int64_t divisors[] = {100000, 10000, 1000, 100, 10, 1};
    for (int i = 0; i < 6; i++) {
        asm_.mov_rax_mem_rbp(locals["$ftoa_fracpart"]);
        asm_.mov_rcx_imm64(divisors[i]);
        asm_.cqo();
        asm_.idiv_rcx();
        // rax = frac / divisor
        
        // digit = rax % 10
        asm_.mov_rcx_imm64(10);
        asm_.cqo();
        asm_.idiv_rcx();
        // rdx = digit
        
        // Write digit
        asm_.mov_rax_mem_rbp(locals["$ftoa_bufptr"]);
        asm_.code.push_back(0x80); asm_.code.push_back(0xC2); asm_.code.push_back('0');  // add dl, '0'
        asm_.code.push_back(0x88); asm_.code.push_back(0x10);  // mov byte [rax], dl
        asm_.inc_rax();
        asm_.mov_mem_rbp_rax(locals["$ftoa_bufptr"]);
    }
    
    asm_.label(doneLabel);
    
    // Calculate length and return
    asm_.mov_rcx_mem_rbp(locals["$ftoa_bufptr"]);  // End position
    asm_.lea_rax_rip_fixup(ftoaBufferRVA);         // Start position
    asm_.sub_rax_rcx();                             // This gives negative length
    asm_.neg_rax();                                 // Make positive
    asm_.mov_rcx_rax();                             // rcx = length
    asm_.lea_rax_rip_fixup(ftoaBufferRVA);         // rax = buffer start
}

} // namespace flex

