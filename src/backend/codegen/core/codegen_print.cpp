// Tyl Compiler - Native Code Generator Print Helpers
// Console output, itoa, ftoa, and print expression helpers

#include "backend/codegen/codegen_base.h"
#include <sstream>
#include <iomanip>

namespace tyl {

// Emit WriteConsoleA call with cached stdout handle (in RDI)
void NativeCodeGen::emitWriteConsole(uint32_t strRVA, size_t len) {
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x38);
    
    if (useStdoutCaching_ && stdoutHandleCached_) {
        asm_.mov_rcx_rdi();
    } else {
        asm_.mov_ecx_imm32(-11);
        asm_.call_mem_rip(pe_.getImportRVA("GetStdHandle"));
        
        if (useStdoutCaching_ && !stdoutHandleCached_) {
            asm_.code.push_back(0x48); asm_.code.push_back(0x89); asm_.code.push_back(0xC7);
            stdoutHandleCached_ = true;
        }
        
        asm_.mov_rcx_rax();
    }
    
    asm_.lea_rax_rip_fixup(strRVA);
    asm_.mov_rdx_rax();
    asm_.mov_r8d_imm32((int32_t)len);
    // lea r9, [rsp+0x28] - address for lpNumberOfCharsWritten
    asm_.code.push_back(0x4C); asm_.code.push_back(0x8D); asm_.code.push_back(0x4C);
    asm_.code.push_back(0x24); asm_.code.push_back(0x28);
    // mov qword [rsp+0x20], 0 - lpReserved = NULL (5th parameter)
    asm_.code.push_back(0x48); asm_.code.push_back(0xC7); asm_.code.push_back(0x44);
    asm_.code.push_back(0x24); asm_.code.push_back(0x20);
    asm_.code.push_back(0x00); asm_.code.push_back(0x00); asm_.code.push_back(0x00); asm_.code.push_back(0x00);
    asm_.call_mem_rip(pe_.getImportRVA("WriteConsoleA"));
    
    if (!stackAllocated_) asm_.add_rsp_imm32(0x38);
}

// Emit WriteConsoleA for buffer in rdx with length in r8
void NativeCodeGen::emitWriteConsoleBuffer() {
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x38);
    
    if (useStdoutCaching_ && stdoutHandleCached_) {
        asm_.mov_rcx_rdi();
    } else {
        asm_.push_rdx();
        asm_.code.push_back(0x41); asm_.code.push_back(0x50);
        
        asm_.mov_ecx_imm32(-11);
        asm_.call_mem_rip(pe_.getImportRVA("GetStdHandle"));
        
        if (useStdoutCaching_ && !stdoutHandleCached_) {
            asm_.code.push_back(0x48); asm_.code.push_back(0x89); asm_.code.push_back(0xC7);
            stdoutHandleCached_ = true;
        }
        
        asm_.mov_rcx_rax();
        
        asm_.code.push_back(0x41); asm_.code.push_back(0x58);
        asm_.pop_rdx();
    }
    
    // lea r9, [rsp+0x28] - address for lpNumberOfCharsWritten
    asm_.code.push_back(0x4C); asm_.code.push_back(0x8D); asm_.code.push_back(0x4C);
    asm_.code.push_back(0x24); asm_.code.push_back(0x28);
    // mov qword [rsp+0x20], 0 - lpReserved = NULL (5th parameter)
    asm_.code.push_back(0x48); asm_.code.push_back(0xC7); asm_.code.push_back(0x44);
    asm_.code.push_back(0x24); asm_.code.push_back(0x20);
    asm_.code.push_back(0x00); asm_.code.push_back(0x00); asm_.code.push_back(0x00); asm_.code.push_back(0x00);
    asm_.call_mem_rip(pe_.getImportRVA("WriteConsoleA"));
    
    if (!stackAllocated_) asm_.add_rsp_imm32(0x38);
}

// Print a string from pointer in rax - calculates length at runtime
void NativeCodeGen::emitPrintStringPtr() {
    asm_.push_rax();
    
    asm_.mov_rcx_rax();
    asm_.xor_rax_rax();
    
    std::string loopLabel = newLabel("strlen_loop");
    std::string doneLabel = newLabel("strlen_done");
    
    asm_.label(loopLabel);
    asm_.code.push_back(0x48); asm_.code.push_back(0x0F); asm_.code.push_back(0xB6);
    asm_.code.push_back(0x14); asm_.code.push_back(0x01);
    asm_.code.push_back(0x84); asm_.code.push_back(0xD2);
    asm_.jz_rel32(doneLabel);
    asm_.inc_rax();
    asm_.jmp_rel32(loopLabel);
    
    asm_.label(doneLabel);
    asm_.mov_r8_rax();
    
    asm_.pop_rdx();
    
    emitWriteConsoleBuffer();
}

// Print str_view from pointer in rax (ptr at [rax], len at [rax+8])
void NativeCodeGen::emitPrintStrView() {
    // str_view layout: { ptr: *u8 at offset 0, len: i64 at offset 8 }
    // Load length first (at [rax+8])
    asm_.push_rax();  // Save str_view pointer
    asm_.mov_r8_mem_rax(8);  // r8 = length at [rax+8]
    
    // Load string pointer (at [rax])
    asm_.pop_rax();
    asm_.mov_rdx_mem_rax();  // rdx = string pointer at [rax]
    
    // Call WriteConsoleBuffer with rdx=buffer, r8=length
    emitWriteConsoleBuffer();
}

void NativeCodeGen::emitPrintExpr(Expression* expr) {
    // First, try to evaluate the entire expression as a constant string
    std::string constStr;
    if (tryEvalConstantString(expr, constStr)) {
        uint32_t strRVA = addString(constStr);
        emitWriteConsole(strRVA, constStr.length());
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
                    emitWriteConsole(strRVA, str->length());
                }
            } else if (auto* exprPtr = std::get_if<ExprPtr>(&part)) {
                // Expression part - try constant eval first, then runtime
                std::string strVal;
                int64_t intVal;
                double floatVal;
                
                if (tryEvalConstantString(exprPtr->get(), strVal)) {
                    uint32_t strRVA = addString(strVal);
                    emitWriteConsole(strRVA, strVal.length());
                } else if (tryEvalConstantFloat(exprPtr->get(), floatVal) && isFloatExpression(exprPtr->get())) {
                    std::ostringstream oss;
                    oss << std::fixed << std::setprecision(6) << floatVal;
                    std::string numStr = oss.str();
                    uint32_t strRVA = addString(numStr);
                    emitWriteConsole(strRVA, numStr.length());
                } else if (tryEvalConstant(exprPtr->get(), intVal)) {
                    std::string numStr = std::to_string(intVal);
                    uint32_t strRVA = addString(numStr);
                    emitWriteConsole(strRVA, numStr.length());
                } else if (isFloatExpression(exprPtr->get())) {
                    // Runtime float
                    (*exprPtr)->accept(*this);
                    emitFtoaCall();
                    asm_.mov_rdx_rax();
                    asm_.mov_r8_rcx();
                    emitWriteConsoleBuffer();
                } else if (isStringReturningExpr(exprPtr->get())) {
                    (*exprPtr)->accept(*this);
                    emitPrintStringPtr();
                } else {
                    // Runtime int - evaluate and print
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
            bool leftIsString = dynamic_cast<StringLiteral*>(binary->left.get()) ||
                               dynamic_cast<InterpolatedString*>(binary->left.get());
            bool rightIsString = dynamic_cast<StringLiteral*>(binary->right.get()) ||
                                dynamic_cast<InterpolatedString*>(binary->right.get());
            
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
            
            if (dynamic_cast<BinaryExpr*>(binary->left.get())) leftIsString = true;
            if (dynamic_cast<BinaryExpr*>(binary->right.get())) rightIsString = true;
            
            if (leftIsString || rightIsString) {
                emitPrintExpr(binary->left.get());
                emitPrintExpr(binary->right.get());
                return;
            }
        }
    }
    
    // Handle str() call
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        if (auto* id = dynamic_cast<Identifier*>(call->callee.get())) {
            if (id->name == "str" && call->args.size() == 1) {
                int64_t intVal;
                if (tryEvalConstant(call->args[0].get(), intVal)) {
                    std::string numStr = std::to_string(intVal);
                    uint32_t strRVA = addString(numStr);
                    emitWriteConsole(strRVA, numStr.length());
                    return;
                }
                call->args[0]->accept(*this);
                emitItoa();
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
        emitWriteConsole(strRVA, strLit->value.length());
        return;
    }
    
    // Handle float literal
    if (auto* floatLit = dynamic_cast<FloatLiteral*>(expr)) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(6) << floatLit->value;
        std::string numStr = oss.str();
        uint32_t strRVA = addString(numStr);
        emitWriteConsole(strRVA, numStr.length());
        return;
    }
    
    // Check if expression is a float
    if (isFloatExpression(expr)) {
        double floatVal;
        if (tryEvalConstantFloat(expr, floatVal)) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(6) << floatVal;
            std::string numStr = oss.str();
            uint32_t strRVA = addString(numStr);
            emitWriteConsole(strRVA, numStr.length());
            return;
        }
        expr->accept(*this);
        emitFtoaCall();
        asm_.mov_rdx_rax();
        asm_.mov_r8_rcx();
        emitWriteConsoleBuffer();
        return;
    }
    
    // Handle index expression (list[i]) - check if indexing into a string list
    if (auto* indexExpr = dynamic_cast<IndexExpr*>(expr)) {
        if (auto* ident = dynamic_cast<Identifier*>(indexExpr->object.get())) {
            // Check if this is a constant list with integer elements
            auto constListIt = constListVars.find(ident->name);
            if (constListIt != constListVars.end()) {
                // Constant list of integers - try to constant fold
                int64_t indexVal;
                if (tryEvalConstant(indexExpr->index.get(), indexVal)) {
                    int64_t zeroBasedIndex = indexVal - 1;
                    if (zeroBasedIndex >= 0 && (size_t)zeroBasedIndex < constListIt->second.size()) {
                        // Print the constant integer value
                        std::string numStr = std::to_string(constListIt->second[zeroBasedIndex]);
                        uint32_t strRVA = addString(numStr);
                        emitWriteConsole(strRVA, numStr.length());
                        return;
                    }
                }
                // Runtime index into constant integer list - evaluate and print as int
                expr->accept(*this);
                emitPrintIntCall();
                return;
            }
            
            // Check if this is a list variable (from split, etc.) - these are string lists
            if (listVars.count(ident->name) && !constListVars.count(ident->name)) {
                // Assume list of strings - evaluate and print as string
                expr->accept(*this);
                emitPrintStringPtr();
                return;
            }
        }
        // Fall through to default handling
    }
    
    // Handle identifier (variable)
    if (auto* ident = dynamic_cast<Identifier*>(expr)) {
        auto strIt = constStrVars.find(ident->name);
        if (strIt != constStrVars.end()) {
            if (!strIt->second.empty()) {
                // Constant string - print directly
                uint32_t strRVA = addString(strIt->second);
                emitWriteConsole(strRVA, strIt->second.length());
                return;
            } else {
                // Runtime string variable - load and print as string pointer
                expr->accept(*this);
                emitPrintStringPtr();
                return;
            }
        }
        
        // Check for integer constant FIRST (before float)
        // This ensures integer comptime functions are printed as integers
        auto intIt = constVars.find(ident->name);
        if (intIt != constVars.end()) {
            std::string numStr = std::to_string(intIt->second);
            uint32_t strRVA = addString(numStr);
            emitWriteConsole(strRVA, numStr.length());
            return;
        }
        
        auto floatIt = constFloatVars.find(ident->name);
        if (floatIt != constFloatVars.end()) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(6) << floatIt->second;
            std::string numStr = oss.str();
            uint32_t strRVA = addString(numStr);
            emitWriteConsole(strRVA, numStr.length());
            return;
        }
        
        if (floatVars.count(ident->name)) {
            expr->accept(*this);
            emitFtoaCall();
            asm_.mov_rdx_rax();
            asm_.mov_r8_rcx();
            emitWriteConsoleBuffer();
            return;
        }
        
        // Check if this is a known string variable (has a non-empty entry in constStrVars)
        // Note: Empty entries in constStrVars are used to mark parameters, not actual strings
        // So we only treat it as a string if the entry is non-empty OR if the type is explicitly str
        // For now, fall through to integer printing for unknown variables
        
        // Runtime variable - load and print as int
        expr->accept(*this);
        emitPrintIntCall();
        return;
    }
    
    // Default: evaluate as int and print
    int64_t intVal;
    if (tryEvalConstant(expr, intVal)) {
        std::string numStr = std::to_string(intVal);
        uint32_t strRVA = addString(numStr);
        emitWriteConsole(strRVA, numStr.length());
        return;
    }
    
    // Check if this is a string-returning expression (like ltrim, rtrim, etc.)
    if (isStringReturningExpr(expr)) {
        expr->accept(*this);
        emitPrintStringPtr();
        return;
    }
    
    // Runtime evaluation
    expr->accept(*this);
    emitPrintIntCall();
}

void NativeCodeGen::emitFtoa() {
    // Float to ASCII conversion
    // Input: xmm0 = float value
    // Output: rax = pointer to string buffer, rcx = length
    
    asm_.push_rbx();
    asm_.push_r12();
    asm_.push_r13();
    // push r14
    asm_.code.push_back(0x41); asm_.code.push_back(0x56);
    // push r15
    asm_.code.push_back(0x41); asm_.code.push_back(0x57);
    
    // Get buffer address
    asm_.lea_rax_rip_fixup(itoaBufferRVA_);
    asm_.mov_r12_rax();  // r12 = buffer start
    asm_.mov_r13_rax();  // r13 = current write position
    
    // Check for negative
    std::string notNeg = newLabel("ftoa_pos");
    asm_.movq_rax_xmm0();
    asm_.test_rax_rax();
    // jns (jump if not sign)
    asm_.code.push_back(0x0F); asm_.code.push_back(0x89);
    size_t jnsFixup1 = asm_.code.size();
    asm_.code.push_back(0x00); asm_.code.push_back(0x00); asm_.code.push_back(0x00); asm_.code.push_back(0x00);
    
    // Negative: store '-' and negate
    // mov byte [r13], '-'
    asm_.code.push_back(0x41); asm_.code.push_back(0xC6); asm_.code.push_back(0x45); asm_.code.push_back(0x00); asm_.code.push_back('-');
    // inc r13
    asm_.code.push_back(0x49); asm_.code.push_back(0xFF); asm_.code.push_back(0xC5);
    // Negate xmm0
    asm_.mov_rcx_imm64(0x8000000000000000LL);
    asm_.movq_xmm1_rcx();
    asm_.xorpd_xmm0_xmm1();
    
    // Patch the jns jump
    size_t notNegOffset = asm_.code.size();
    int32_t jnsDisp1 = (int32_t)(notNegOffset - jnsFixup1 - 4);
    asm_.code[jnsFixup1] = jnsDisp1 & 0xFF;
    asm_.code[jnsFixup1 + 1] = (jnsDisp1 >> 8) & 0xFF;
    asm_.code[jnsFixup1 + 2] = (jnsDisp1 >> 16) & 0xFF;
    asm_.code[jnsFixup1 + 3] = (jnsDisp1 >> 24) & 0xFF;
    
    asm_.label(notNeg);
    
    // Save xmm0 (fractional part) in xmm2 for later
    // movsd xmm2, xmm0
    asm_.code.push_back(0xF2); asm_.code.push_back(0x0F); asm_.code.push_back(0x10); asm_.code.push_back(0xD0);
    
    // Get integer part
    asm_.cvttsd2si_rax_xmm0();
    asm_.mov_rbx_rax();  // rbx = integer part
    
    // Compute fractional part: xmm2 = xmm2 - (double)rbx
    asm_.cvtsi2sd_xmm1_rax();
    // subsd xmm2, xmm1
    asm_.code.push_back(0xF2); asm_.code.push_back(0x0F); asm_.code.push_back(0x5C); asm_.code.push_back(0xD1);
    
    // Handle zero integer part
    std::string notZeroInt = newLabel("ftoa_nz_int");
    std::string intDone = newLabel("ftoa_int_done");
    // test rbx, rbx
    asm_.code.push_back(0x48); asm_.code.push_back(0x85); asm_.code.push_back(0xDB);
    asm_.jnz_rel32(notZeroInt);
    // mov byte [r13], '0'
    asm_.code.push_back(0x41); asm_.code.push_back(0xC6); asm_.code.push_back(0x45); asm_.code.push_back(0x00); asm_.code.push_back('0');
    // inc r13
    asm_.code.push_back(0x49); asm_.code.push_back(0xFF); asm_.code.push_back(0xC5);
    asm_.jmp_rel32(intDone);
    
    asm_.label(notZeroInt);
    
    // Convert integer part using a temporary buffer approach:
    // Write digits to a temp area, then reverse them
    // r14 = start of integer digits, r15 = count of digits
    // mov r14, r13 (save start position)
    asm_.code.push_back(0x4D); asm_.code.push_back(0x89); asm_.code.push_back(0xEE);
    // xor r15d, r15d (digit count = 0)
    asm_.code.push_back(0x45); asm_.code.push_back(0x31); asm_.code.push_back(0xFF);
    
    std::string intLoop = newLabel("ftoa_int_loop");
    asm_.label(intLoop);
    // test rbx, rbx
    asm_.code.push_back(0x48); asm_.code.push_back(0x85); asm_.code.push_back(0xDB);
    asm_.jz_rel32(intDone);
    
    // rax = rbx / 10, rdx = rbx % 10
    asm_.mov_rax_rbx();
    asm_.mov_rcx_imm64(10);
    asm_.cqo();
    asm_.idiv_rcx();
    asm_.mov_rbx_rax();  // rbx = quotient
    
    // Store digit (rdx + '0') at [r13]
    // add dl, '0'
    asm_.code.push_back(0x80); asm_.code.push_back(0xC2); asm_.code.push_back('0');
    // mov byte [r13], dl
    asm_.code.push_back(0x41); asm_.code.push_back(0x88); asm_.code.push_back(0x55); asm_.code.push_back(0x00);
    // inc r13
    asm_.code.push_back(0x49); asm_.code.push_back(0xFF); asm_.code.push_back(0xC5);
    // inc r15
    asm_.code.push_back(0x49); asm_.code.push_back(0xFF); asm_.code.push_back(0xC7);
    
    asm_.jmp_rel32(intLoop);
    
    asm_.label(intDone);
    
    // Reverse the integer digits in place (from r14 to r13-1)
    // Only if we wrote more than 1 digit (r15 > 1)
    std::string skipReverse = newLabel("ftoa_skip_rev");
    // cmp r15, 1
    asm_.code.push_back(0x49); asm_.code.push_back(0x83); asm_.code.push_back(0xFF); asm_.code.push_back(0x01);
    // jle skipReverse
    asm_.code.push_back(0x0F); asm_.code.push_back(0x8E);
    size_t jleFixup = asm_.code.size();
    asm_.code.push_back(0x00); asm_.code.push_back(0x00); asm_.code.push_back(0x00); asm_.code.push_back(0x00);
    
    // Reverse: rax = r14 (left), rcx = r13 - 1 (right)
    // mov rax, r14
    asm_.code.push_back(0x4C); asm_.code.push_back(0x89); asm_.code.push_back(0xF0);
    // lea rcx, [r13 - 1]
    asm_.code.push_back(0x49); asm_.code.push_back(0x8D); asm_.code.push_back(0x4D); asm_.code.push_back(0xFF);
    
    std::string revLoop = newLabel("ftoa_rev_loop");
    std::string revDone = newLabel("ftoa_rev_done");
    asm_.label(revLoop);
    // cmp rax, rcx
    asm_.code.push_back(0x48); asm_.code.push_back(0x39); asm_.code.push_back(0xC8);
    // jge revDone
    asm_.code.push_back(0x0F); asm_.code.push_back(0x8D);
    size_t jgeFixup = asm_.code.size();
    asm_.code.push_back(0x00); asm_.code.push_back(0x00); asm_.code.push_back(0x00); asm_.code.push_back(0x00);
    
    // Swap bytes at [rax] and [rcx]
    // mov dl, [rax]
    asm_.code.push_back(0x8A); asm_.code.push_back(0x10);
    // mov dh, [rcx]
    asm_.code.push_back(0x8A); asm_.code.push_back(0x31);
    // mov [rax], dh
    asm_.code.push_back(0x88); asm_.code.push_back(0x30);
    // mov [rcx], dl
    asm_.code.push_back(0x88); asm_.code.push_back(0x11);
    
    // inc rax
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC0);
    // dec rcx
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC9);
    asm_.jmp_rel32(revLoop);
    
    asm_.label(revDone);
    // Patch jge
    int32_t jgeDisp = (int32_t)(asm_.code.size() - jgeFixup - 4);
    asm_.code[jgeFixup] = jgeDisp & 0xFF;
    asm_.code[jgeFixup + 1] = (jgeDisp >> 8) & 0xFF;
    asm_.code[jgeFixup + 2] = (jgeDisp >> 16) & 0xFF;
    asm_.code[jgeFixup + 3] = (jgeDisp >> 24) & 0xFF;
    
    asm_.label(skipReverse);
    // Patch jle
    int32_t jleDisp = (int32_t)(asm_.code.size() - jleFixup - 4);
    asm_.code[jleFixup] = jleDisp & 0xFF;
    asm_.code[jleFixup + 1] = (jleDisp >> 8) & 0xFF;
    asm_.code[jleFixup + 2] = (jleDisp >> 16) & 0xFF;
    asm_.code[jleFixup + 3] = (jleDisp >> 24) & 0xFF;
    
    // Add decimal point
    // mov byte [r13], '.'
    asm_.code.push_back(0x41); asm_.code.push_back(0xC6); asm_.code.push_back(0x45); asm_.code.push_back(0x00); asm_.code.push_back('.');
    // inc r13
    asm_.code.push_back(0x49); asm_.code.push_back(0xFF); asm_.code.push_back(0xC5);
    
    // Add fractional digits (6 digits) from xmm2
    // movsd xmm0, xmm2
    asm_.code.push_back(0xF2); asm_.code.push_back(0x0F); asm_.code.push_back(0x10); asm_.code.push_back(0xC2);
    
    for (int i = 0; i < 6; i++) {
        // xmm0 *= 10
        asm_.mov_rax_imm64(10);
        asm_.cvtsi2sd_xmm1_rax();
        asm_.mulsd_xmm0_xmm1();
        
        // Get digit: rax = (int)xmm0
        asm_.cvttsd2si_rax_xmm0();
        // Clamp to 0-9 range (and 0x0F handles any overflow)
        // and eax, 0x0F
        asm_.code.push_back(0x83); asm_.code.push_back(0xE0); asm_.code.push_back(0x0F);
        // add al, '0'
        asm_.code.push_back(0x04); asm_.code.push_back('0');
        // mov byte [r13], al
        asm_.code.push_back(0x41); asm_.code.push_back(0x88); asm_.code.push_back(0x45); asm_.code.push_back(0x00);
        // inc r13
        asm_.code.push_back(0x49); asm_.code.push_back(0xFF); asm_.code.push_back(0xC5);
        
        // Subtract integer part for next iteration
        asm_.cvttsd2si_rax_xmm0();
        asm_.cvtsi2sd_xmm1_rax();
        asm_.subsd_xmm0_xmm1();
    }
    
    // Null terminate
    // mov byte [r13], 0
    asm_.code.push_back(0x41); asm_.code.push_back(0xC6); asm_.code.push_back(0x45); asm_.code.push_back(0x00); asm_.code.push_back(0x00);
    
    // Calculate length: rcx = r13 - r12
    // mov rcx, r13
    asm_.code.push_back(0x4C); asm_.code.push_back(0x89); asm_.code.push_back(0xE9);
    // sub rcx, r12
    asm_.code.push_back(0x4C); asm_.code.push_back(0x29); asm_.code.push_back(0xE1);
    
    // Return buffer start in rax
    asm_.mov_rax_r12();
    
    // pop r15
    asm_.code.push_back(0x41); asm_.code.push_back(0x5F);
    // pop r14
    asm_.code.push_back(0x41); asm_.code.push_back(0x5E);
    asm_.pop_r13();
    asm_.pop_r12();
    asm_.pop_rbx();
}

// Dump generated assembly as hex bytes with labels
void NativeCodeGen::dumpAssembly(std::ostream& out) const {
    out << "=== Generated Assembly (" << asm_.code.size() << " bytes) ===\n";
    
    std::map<size_t, std::string> offsetLabels;
    for (const auto& [name, offset] : asm_.labels) {
        offsetLabels[offset] = name;
    }
    
    for (size_t i = 0; i < asm_.code.size(); ) {
        auto labelIt = offsetLabels.find(i);
        if (labelIt != offsetLabels.end()) {
            out << "\n" << labelIt->second << ":\n";
        }
        
        out << std::hex << std::setw(8) << std::setfill('0') << i << ": ";
        
        size_t lineStart = i;
        size_t lineEnd = std::min(i + 8, asm_.code.size());
        
        for (size_t j = i + 1; j < lineEnd; j++) {
            if (offsetLabels.count(j)) {
                lineEnd = j;
                break;
            }
        }
        
        for (size_t j = lineStart; j < lineEnd; j++) {
            out << std::hex << std::setw(2) << std::setfill('0') << (int)asm_.code[j] << " ";
        }
        
        for (size_t j = lineEnd - lineStart; j < 8; j++) {
            out << "   ";
        }
        
        out << " ; ";
        uint8_t b0 = asm_.code[lineStart];
        if (lineEnd - lineStart >= 1) {
            if (b0 == 0xC3) out << "ret";
            else if (b0 == 0x55) out << "push rbp";
            else if (b0 == 0x5D) out << "pop rbp";
            else if (b0 == 0x50) out << "push rax";
            else if (b0 == 0x58) out << "pop rax";
            else if (b0 == 0xE8) out << "call rel32";
            else if (b0 == 0xE9) out << "jmp rel32";
        }
        
        out << "\n";
        i = lineEnd;
    }
    
    out << std::dec;
    out << "\n=== End Assembly ===\n";
}

} // namespace tyl
