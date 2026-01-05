// Tyl Compiler - Extended String Builtins for Native Code Generation
// Additional string functions ported from stdlib/string/string.cpp

#include "backend/codegen/codegen_base.h"

namespace tyl {

// ltrim(str) -> str - Remove leading whitespace
void NativeCodeGen::emitStringLtrim(CallExpr& node) {
    std::string strVal;
    if (tryEvalConstantString(node.args[0].get(), strVal)) {
        size_t start = strVal.find_first_not_of(" \t\n\r\f\v");
        if (start == std::string::npos) {
            strVal = "";
        } else {
            strVal = strVal.substr(start);
        }
        uint32_t rva = addString(strVal);
        asm_.lea_rax_rip_fixup(rva);
        return;
    }
    
    // Runtime: skip leading whitespace
    node.args[0]->accept(*this);
    asm_.mov_rcx_rax();
    
    std::string loopLabel = newLabel("ltrim_loop");
    std::string doneLabel = newLabel("ltrim_done");
    
    asm_.label(loopLabel);
    // Load byte at rcx
    asm_.code.push_back(0x0F); asm_.code.push_back(0xB6); asm_.code.push_back(0x01); // movzx eax, byte [rcx]
    asm_.test_rax_rax();
    asm_.jz_rel32(doneLabel);
    
    // Check for whitespace characters
    asm_.code.push_back(0x3C); asm_.code.push_back(' ');  // cmp al, ' '
    asm_.code.push_back(0x74); asm_.code.push_back(0x10); // je skip
    asm_.code.push_back(0x3C); asm_.code.push_back('\t'); // cmp al, '\t'
    asm_.code.push_back(0x74); asm_.code.push_back(0x0C); // je skip
    asm_.code.push_back(0x3C); asm_.code.push_back('\n'); // cmp al, '\n'
    asm_.code.push_back(0x74); asm_.code.push_back(0x08); // je skip
    asm_.code.push_back(0x3C); asm_.code.push_back('\r'); // cmp al, '\r'
    asm_.jnz_rel32(doneLabel);
    
    // Increment pointer
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC1); // inc rcx
    asm_.jmp_rel32(loopLabel);
    
    asm_.label(doneLabel);
    asm_.mov_rax_rcx();
}

// rtrim(str) -> str - Remove trailing whitespace
void NativeCodeGen::emitStringRtrim(CallExpr& node) {
    std::string strVal;
    if (tryEvalConstantString(node.args[0].get(), strVal)) {
        size_t end = strVal.find_last_not_of(" \t\n\r\f\v");
        if (end == std::string::npos) {
            strVal = "";
        } else {
            strVal = strVal.substr(0, end + 1);
        }
        uint32_t rva = addString(strVal);
        asm_.lea_rax_rip_fixup(rva);
        return;
    }
    
    // Runtime: copy string and trim trailing whitespace
    allocLocal("$rtrim_buf");
    int32_t bufOffset = locals["$rtrim_buf"];
    for (int i = 0; i < 63; i++) allocLocal("$rtrim_pad" + std::to_string(i));
    
    node.args[0]->accept(*this);
    asm_.mov_rcx_rax();
    asm_.lea_rax_rbp(bufOffset);
    asm_.mov_rdx_rax();
    
    // Copy string
    std::string copyLoop = newLabel("rtrim_copy");
    std::string copyDone = newLabel("rtrim_copy_done");
    
    asm_.label(copyLoop);
    asm_.code.push_back(0x0F); asm_.code.push_back(0xB6); asm_.code.push_back(0x01); // movzx eax, byte [rcx]
    asm_.code.push_back(0x88); asm_.code.push_back(0x02); // mov [rdx], al
    asm_.test_rax_rax();
    asm_.jz_rel32(copyDone);
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC1); // inc rcx
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC2); // inc rdx
    asm_.jmp_rel32(copyLoop);
    
    asm_.label(copyDone);
    // rdx points to null terminator, go back and trim
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xCA); // dec rdx
    asm_.lea_rcx_rbp(bufOffset);
    
    std::string trimLoop = newLabel("rtrim_trim");
    std::string trimDone = newLabel("rtrim_done");
    
    asm_.label(trimLoop);
    asm_.code.push_back(0x48); asm_.code.push_back(0x39); asm_.code.push_back(0xCA); // cmp rdx, rcx
    asm_.jl_rel32(trimDone);
    
    asm_.code.push_back(0x0F); asm_.code.push_back(0xB6); asm_.code.push_back(0x02); // movzx eax, byte [rdx]
    asm_.code.push_back(0x3C); asm_.code.push_back(' ');
    asm_.code.push_back(0x74); asm_.code.push_back(0x10);
    asm_.code.push_back(0x3C); asm_.code.push_back('\t');
    asm_.code.push_back(0x74); asm_.code.push_back(0x0C);
    asm_.code.push_back(0x3C); asm_.code.push_back('\n');
    asm_.code.push_back(0x74); asm_.code.push_back(0x08);
    asm_.code.push_back(0x3C); asm_.code.push_back('\r');
    asm_.jnz_rel32(trimDone);
    
    asm_.code.push_back(0xC6); asm_.code.push_back(0x02); asm_.code.push_back(0x00); // mov byte [rdx], 0
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xCA); // dec rdx
    asm_.jmp_rel32(trimLoop);
    
    asm_.label(trimDone);
    asm_.lea_rax_rbp(bufOffset);
}

// char_at(str, index) -> str - Get character at index
void NativeCodeGen::emitStringCharAt(CallExpr& node) {
    std::string strVal;
    int64_t idx;
    if (tryEvalConstantString(node.args[0].get(), strVal) && tryEvalConstant(node.args[1].get(), idx)) {
        if (idx < 0) idx = (int64_t)strVal.size() + idx;
        if (idx >= 0 && idx < (int64_t)strVal.size()) {
            std::string result(1, strVal[idx]);
            uint32_t rva = addString(result);
            asm_.lea_rax_rip_fixup(rva);
        } else {
            uint32_t rva = addString("");
            asm_.lea_rax_rip_fixup(rva);
        }
        return;
    }
    
    // Runtime
    allocLocal("$char_buf");
    int32_t bufOffset = locals["$char_buf"];
    
    node.args[0]->accept(*this);
    asm_.push_rax();
    node.args[1]->accept(*this);
    asm_.mov_rcx_rax();
    asm_.pop_rax();
    
    // Add index to string pointer
    asm_.code.push_back(0x48); asm_.code.push_back(0x01); asm_.code.push_back(0xC8); // add rax, rcx
    
    // Load single character
    asm_.code.push_back(0x0F); asm_.code.push_back(0xB6); asm_.code.push_back(0x00); // movzx eax, byte [rax]
    
    // Store in buffer with null terminator
    asm_.lea_rcx_rbp(bufOffset);
    asm_.code.push_back(0x88); asm_.code.push_back(0x01); // mov [rcx], al
    asm_.code.push_back(0xC6); asm_.code.push_back(0x41); asm_.code.push_back(0x01); asm_.code.push_back(0x00); // mov byte [rcx+1], 0
    
    asm_.lea_rax_rbp(bufOffset);
}

// repeat(str, count) -> str - Repeat string n times
void NativeCodeGen::emitStringRepeat(CallExpr& node) {
    std::string strVal;
    int64_t count;
    if (tryEvalConstantString(node.args[0].get(), strVal) && tryEvalConstant(node.args[1].get(), count)) {
        std::string result;
        for (int64_t i = 0; i < count; i++) {
            result += strVal;
        }
        uint32_t rva = addString(result);
        asm_.lea_rax_rip_fixup(rva);
        return;
    }
    
    // Runtime - simplified, just return original for now
    node.args[0]->accept(*this);
}

// reverse_str(str) -> str - Reverse string
void NativeCodeGen::emitStringReverse(CallExpr& node) {
    std::string strVal;
    if (tryEvalConstantString(node.args[0].get(), strVal)) {
        std::reverse(strVal.begin(), strVal.end());
        uint32_t rva = addString(strVal);
        asm_.lea_rax_rip_fixup(rva);
        return;
    }
    
    // Runtime
    allocLocal("$rev_buf");
    int32_t bufOffset = locals["$rev_buf"];
    for (int i = 0; i < 63; i++) allocLocal("$rev_pad" + std::to_string(i));
    
    node.args[0]->accept(*this);
    asm_.mov_rcx_rax();
    
    // Find string length
    asm_.push_rcx();
    asm_.xor_rax_rax();
    std::string lenLoop = newLabel("rev_len");
    std::string lenDone = newLabel("rev_len_done");
    
    asm_.label(lenLoop);
    asm_.code.push_back(0x80); asm_.code.push_back(0x39); asm_.code.push_back(0x00); // cmp byte [rcx], 0
    asm_.jz_rel32(lenDone);
    asm_.inc_rax();
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC1); // inc rcx
    asm_.jmp_rel32(lenLoop);
    
    asm_.label(lenDone);
    asm_.pop_rcx();
    asm_.push_rax(); // Save length
    
    // Point to last character
    asm_.code.push_back(0x48); asm_.code.push_back(0x01); asm_.code.push_back(0xC1); // add rcx, rax
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC9); // dec rcx
    
    asm_.lea_rax_rbp(bufOffset);
    asm_.mov_rdx_rax();
    asm_.pop_rax(); // Get length back
    
    std::string copyLoop = newLabel("rev_copy");
    std::string copyDone = newLabel("rev_done");
    
    asm_.label(copyLoop);
    asm_.test_rax_rax();
    asm_.jz_rel32(copyDone);
    
    asm_.code.push_back(0x0F); asm_.code.push_back(0xB6); asm_.code.push_back(0x39); // movzx edi, byte [rcx]
    asm_.code.push_back(0x40); asm_.code.push_back(0x88); asm_.code.push_back(0x3A); // mov [rdx], dil
    
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC9); // dec rcx
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC2); // inc rdx
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC8); // dec rax
    asm_.jmp_rel32(copyLoop);
    
    asm_.label(copyDone);
    asm_.code.push_back(0xC6); asm_.code.push_back(0x02); asm_.code.push_back(0x00); // mov byte [rdx], 0
    
    asm_.lea_rax_rbp(bufOffset);
}

// is_digit(str) -> bool - Check if string is all digits
void NativeCodeGen::emitStringIsDigit(CallExpr& node) {
    std::string strVal;
    if (tryEvalConstantString(node.args[0].get(), strVal)) {
        bool result = !strVal.empty();
        for (char c : strVal) {
            if (c < '0' || c > '9') { result = false; break; }
        }
        asm_.mov_rax_imm64(result ? 1 : 0);
        return;
    }
    
    // Runtime
    node.args[0]->accept(*this);
    asm_.mov_rcx_rax();
    
    // Check for empty string
    asm_.code.push_back(0x80); asm_.code.push_back(0x39); asm_.code.push_back(0x00); // cmp byte [rcx], 0
    std::string falseLabel = newLabel("isdigit_false");
    std::string trueLabel = newLabel("isdigit_true");
    std::string doneLabel = newLabel("isdigit_done");
    
    asm_.jz_rel32(falseLabel);
    
    std::string loopLabel = newLabel("isdigit_loop");
    asm_.label(loopLabel);
    asm_.code.push_back(0x0F); asm_.code.push_back(0xB6); asm_.code.push_back(0x01); // movzx eax, byte [rcx]
    asm_.test_rax_rax();
    asm_.jz_rel32(trueLabel);
    
    asm_.code.push_back(0x3C); asm_.code.push_back('0'); // cmp al, '0'
    asm_.jl_rel32(falseLabel);
    asm_.code.push_back(0x3C); asm_.code.push_back('9'); // cmp al, '9'
    asm_.jg_rel32(falseLabel);
    
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC1); // inc rcx
    asm_.jmp_rel32(loopLabel);
    
    asm_.label(trueLabel);
    asm_.mov_rax_imm64(1);
    asm_.jmp_rel32(doneLabel);
    
    asm_.label(falseLabel);
    asm_.xor_rax_rax();
    
    asm_.label(doneLabel);
}

// is_alpha(str) -> bool - Check if string is all letters
void NativeCodeGen::emitStringIsAlpha(CallExpr& node) {
    std::string strVal;
    if (tryEvalConstantString(node.args[0].get(), strVal)) {
        bool result = !strVal.empty();
        for (char c : strVal) {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) { result = false; break; }
        }
        asm_.mov_rax_imm64(result ? 1 : 0);
        return;
    }
    
    // Runtime - similar to is_digit but check a-z, A-Z
    node.args[0]->accept(*this);
    asm_.mov_rcx_rax();
    
    asm_.code.push_back(0x80); asm_.code.push_back(0x39); asm_.code.push_back(0x00);
    std::string falseLabel = newLabel("isalpha_false");
    std::string trueLabel = newLabel("isalpha_true");
    std::string doneLabel = newLabel("isalpha_done");
    std::string checkUpper = newLabel("isalpha_upper");
    
    asm_.jz_rel32(falseLabel);
    
    std::string loopLabel = newLabel("isalpha_loop");
    asm_.label(loopLabel);
    asm_.code.push_back(0x0F); asm_.code.push_back(0xB6); asm_.code.push_back(0x01);
    asm_.test_rax_rax();
    asm_.jz_rel32(trueLabel);
    
    // Check lowercase
    asm_.code.push_back(0x3C); asm_.code.push_back('a');
    asm_.jl_rel32(checkUpper);
    asm_.code.push_back(0x3C); asm_.code.push_back('z');
    asm_.jle_rel32(loopLabel + "_next");
    
    asm_.label(checkUpper);
    asm_.code.push_back(0x3C); asm_.code.push_back('A');
    asm_.jl_rel32(falseLabel);
    asm_.code.push_back(0x3C); asm_.code.push_back('Z');
    asm_.jg_rel32(falseLabel);
    
    asm_.label(loopLabel + "_next");
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC1);
    asm_.jmp_rel32(loopLabel);
    
    asm_.label(trueLabel);
    asm_.mov_rax_imm64(1);
    asm_.jmp_rel32(doneLabel);
    
    asm_.label(falseLabel);
    asm_.xor_rax_rax();
    
    asm_.label(doneLabel);
}

// ord(char) -> int - Get ASCII code of character
void NativeCodeGen::emitStringOrd(CallExpr& node) {
    std::string strVal;
    if (tryEvalConstantString(node.args[0].get(), strVal)) {
        int64_t result = strVal.empty() ? 0 : (unsigned char)strVal[0];
        asm_.mov_rax_imm64(result);
        return;
    }
    
    // Runtime
    node.args[0]->accept(*this);
    asm_.code.push_back(0x48); asm_.code.push_back(0x0F); asm_.code.push_back(0xB6);
    asm_.code.push_back(0x00); // movzx rax, byte [rax]
}

// chr(code) -> str - Get character from ASCII code
void NativeCodeGen::emitStringChr(CallExpr& node) {
    int64_t code;
    if (tryEvalConstant(node.args[0].get(), code)) {
        if (code >= 0 && code <= 255) {
            std::string result(1, (char)code);
            uint32_t rva = addString(result);
            asm_.lea_rax_rip_fixup(rva);
        } else {
            uint32_t rva = addString("");
            asm_.lea_rax_rip_fixup(rva);
        }
        return;
    }
    
    // Runtime
    allocLocal("$chr_buf");
    int32_t bufOffset = locals["$chr_buf"];
    
    node.args[0]->accept(*this);
    asm_.lea_rcx_rbp(bufOffset);
    asm_.code.push_back(0x88); asm_.code.push_back(0x01); // mov [rcx], al
    asm_.code.push_back(0xC6); asm_.code.push_back(0x41); asm_.code.push_back(0x01); asm_.code.push_back(0x00); // mov byte [rcx+1], 0
    
    asm_.lea_rax_rbp(bufOffset);
}

// last_index_of(str, substr) -> int - Find last occurrence
void NativeCodeGen::emitStringLastIndexOf(CallExpr& node) {
    std::string strVal, substr;
    if (tryEvalConstantString(node.args[0].get(), strVal) &&
        tryEvalConstantString(node.args[1].get(), substr)) {
        size_t pos = strVal.rfind(substr);
        int64_t result = (pos == std::string::npos) ? -1 : static_cast<int64_t>(pos);
        asm_.mov_rax_imm64(result);
        return;
    }
    
    // Runtime - simplified, return -1
    asm_.mov_rax_imm64(-1);
}

} // namespace tyl
