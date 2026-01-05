// Tyl Compiler - Native Code Generator String Builtin Calls
// Handles: len, upper, lower, trim, starts_with, ends_with, substring, replace, split, join, index_of

#include "backend/codegen/codegen_base.h"

namespace tyl {

// String builtin implementations extracted from codegen_call_core.cpp
// These handle string manipulation functions

void NativeCodeGen::emitStringLen(CallExpr& node) {
    if (auto* strLit = dynamic_cast<StringLiteral*>(node.args[0].get())) {
        asm_.mov_rax_imm64((int64_t)strLit->value.length());
        return;
    }
    if (auto* ident = dynamic_cast<Identifier*>(node.args[0].get())) {
        // Check for compile-time known string
        auto strIt = constStrVars.find(ident->name);
        if (strIt != constStrVars.end() && !strIt->second.empty()) {
            asm_.mov_rax_imm64((int64_t)strIt->second.length());
            return;
        }
        
        // Check for compile-time known list size
        auto listIt = listSizes.find(ident->name);
        if (listIt != listSizes.end()) {
            asm_.mov_rax_imm64((int64_t)listIt->second);
            return;
        }
        
        // Check for constant list
        auto constListIt = constListVars.find(ident->name);
        if (constListIt != constListVars.end()) {
            asm_.mov_rax_imm64((int64_t)constListIt->second.size());
            return;
        }
        
        // Check if this is a runtime list variable - read count from offset 0
        // Lists have structure: [count:8][capacity:8][elements...]
        if (listVars.count(ident->name)) {
            node.args[0]->accept(*this);
            asm_.mov_rax_mem_rax();  // rax = [rax] = count
            return;
        }
        
        // Check if variable is on stack (could be string or list)
        if (locals.find(ident->name) != locals.end() && strIt == constStrVars.end()) {
            // Not a known string, might be a list - read count
            node.args[0]->accept(*this);
            asm_.mov_rax_mem_rax();  // rax = [rax] = count
            return;
        }
        
        // Known string variable - compute length at runtime
        if (strIt != constStrVars.end()) {
            node.args[0]->accept(*this);
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
            return;
        }
    }
    if (auto* list = dynamic_cast<ListExpr*>(node.args[0].get())) {
        asm_.mov_rax_imm64((int64_t)list->elements.size());
        return;
    }
    
    // For any other expression that might be a list, try to read count from header
    node.args[0]->accept(*this);
    asm_.mov_rax_mem_rax();  // rax = [rax] = count (for lists)
}

void NativeCodeGen::emitStringUpper(CallExpr& node) {
    std::string strVal;
    if (tryEvalConstantString(node.args[0].get(), strVal)) {
        for (char& c : strVal) {
            if (c >= 'a' && c <= 'z') c -= 32;
        }
        uint32_t rva = addString(strVal);
        asm_.lea_rax_rip_fixup(rva);
        return;
    }
    
    allocLocal("$upper_buf");
    int32_t bufOffset = locals["$upper_buf"];
    for (int i = 0; i < 31; i++) allocLocal("$upper_pad" + std::to_string(i));
    
    node.args[0]->accept(*this);
    asm_.mov_rcx_rax();
    
    asm_.lea_rax_rbp(bufOffset);
    asm_.mov_rdx_rax();
    
    std::string loopLabel = newLabel("upper_loop");
    std::string doneLabel = newLabel("upper_done");
    std::string noConvertLabel = newLabel("upper_noconv");
    
    asm_.label(loopLabel);
    asm_.code.push_back(0x0F); asm_.code.push_back(0xB6); asm_.code.push_back(0x01);
    
    asm_.test_rax_rax();
    asm_.jz_rel32(doneLabel);
    
    asm_.code.push_back(0x3C); asm_.code.push_back('a');
    asm_.jl_rel32(noConvertLabel);
    asm_.code.push_back(0x3C); asm_.code.push_back('z');
    asm_.jg_rel32(noConvertLabel);
    
    asm_.code.push_back(0x2C); asm_.code.push_back(32);
    
    asm_.label(noConvertLabel);
    asm_.code.push_back(0x88); asm_.code.push_back(0x02);
    
    asm_.inc_rax();
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC1);
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC2);
    
    asm_.jmp_rel32(loopLabel);
    
    asm_.label(doneLabel);
    asm_.code.push_back(0xC6); asm_.code.push_back(0x02); asm_.code.push_back(0x00);
    
    asm_.lea_rax_rbp(bufOffset);
}

void NativeCodeGen::emitStringLower(CallExpr& node) {
    std::string strVal;
    if (tryEvalConstantString(node.args[0].get(), strVal)) {
        for (char& c : strVal) {
            if (c >= 'A' && c <= 'Z') c += 32;
        }
        uint32_t rva = addString(strVal);
        asm_.lea_rax_rip_fixup(rva);
        return;
    }
    
    allocLocal("$lower_buf");
    int32_t bufOffset = locals["$lower_buf"];
    for (int i = 0; i < 31; i++) allocLocal("$lower_pad" + std::to_string(i));
    
    node.args[0]->accept(*this);
    asm_.mov_rcx_rax();
    
    asm_.lea_rax_rbp(bufOffset);
    asm_.mov_rdx_rax();
    
    std::string loopLabel = newLabel("lower_loop");
    std::string doneLabel = newLabel("lower_done");
    std::string noConvertLabel = newLabel("lower_noconv");
    
    asm_.label(loopLabel);
    asm_.code.push_back(0x0F); asm_.code.push_back(0xB6); asm_.code.push_back(0x01);
    
    asm_.test_rax_rax();
    asm_.jz_rel32(doneLabel);
    
    asm_.code.push_back(0x3C); asm_.code.push_back('A');
    asm_.jl_rel32(noConvertLabel);
    asm_.code.push_back(0x3C); asm_.code.push_back('Z');
    asm_.jg_rel32(noConvertLabel);
    
    asm_.code.push_back(0x04); asm_.code.push_back(32);
    
    asm_.label(noConvertLabel);
    asm_.code.push_back(0x88); asm_.code.push_back(0x02);
    
    asm_.inc_rax();
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC1);
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC2);
    
    asm_.jmp_rel32(loopLabel);
    
    asm_.label(doneLabel);
    asm_.code.push_back(0xC6); asm_.code.push_back(0x02); asm_.code.push_back(0x00);
    
    asm_.lea_rax_rbp(bufOffset);
}

void NativeCodeGen::emitStringTrim(CallExpr& node) {
    std::string strVal;
    if (tryEvalConstantString(node.args[0].get(), strVal)) {
        size_t start = strVal.find_first_not_of(" \t\n\r");
        size_t end = strVal.find_last_not_of(" \t\n\r");
        if (start == std::string::npos) {
            strVal = "";
        } else {
            strVal = strVal.substr(start, end - start + 1);
        }
        uint32_t rva = addString(strVal);
        asm_.lea_rax_rip_fixup(rva);
        return;
    }
    
    allocLocal("$trim_buf");
    int32_t bufOffset = locals["$trim_buf"];
    for (int i = 0; i < 31; i++) allocLocal("$trim_pad" + std::to_string(i));
    
    node.args[0]->accept(*this);
    asm_.mov_rcx_rax();
    
    std::string skipLeadLoop = newLabel("trim_lead");
    std::string skipLeadDone = newLabel("trim_lead_done");
    
    asm_.label(skipLeadLoop);
    asm_.code.push_back(0x0F); asm_.code.push_back(0xB6); asm_.code.push_back(0x01);
    asm_.test_rax_rax();
    asm_.jz_rel32(skipLeadDone);
    asm_.code.push_back(0x3C); asm_.code.push_back(' ');
    asm_.code.push_back(0x74); asm_.code.push_back(0x0C);
    asm_.code.push_back(0x3C); asm_.code.push_back('\t');
    asm_.code.push_back(0x74); asm_.code.push_back(0x08);
    asm_.code.push_back(0x3C); asm_.code.push_back('\n');
    asm_.code.push_back(0x74); asm_.code.push_back(0x04);
    asm_.code.push_back(0x3C); asm_.code.push_back('\r');
    asm_.jnz_rel32(skipLeadDone);
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC1);
    asm_.jmp_rel32(skipLeadLoop);
    
    asm_.label(skipLeadDone);
    
    asm_.lea_rax_rbp(bufOffset);
    asm_.mov_rdx_rax();
    asm_.push_rdx();
    
    std::string copyLoop = newLabel("trim_copy");
    std::string copyDone = newLabel("trim_copy_done");
    
    asm_.label(copyLoop);
    asm_.code.push_back(0x0F); asm_.code.push_back(0xB6); asm_.code.push_back(0x01);
    asm_.test_rax_rax();
    asm_.jz_rel32(copyDone);
    asm_.code.push_back(0x88); asm_.code.push_back(0x02);
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC1);
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC2);
    asm_.jmp_rel32(copyLoop);
    
    asm_.label(copyDone);
    asm_.code.push_back(0xC6); asm_.code.push_back(0x02); asm_.code.push_back(0x00);
    
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xCA);
    asm_.pop_rcx();
    
    std::string trimTrailLoop = newLabel("trim_trail");
    std::string trimTrailDone = newLabel("trim_trail_done");
    
    asm_.label(trimTrailLoop);
    asm_.code.push_back(0x48); asm_.code.push_back(0x39); asm_.code.push_back(0xCA);
    asm_.jl_rel32(trimTrailDone);
    asm_.code.push_back(0x0F); asm_.code.push_back(0xB6); asm_.code.push_back(0x02);
    asm_.code.push_back(0x3C); asm_.code.push_back(' ');
    asm_.code.push_back(0x74); asm_.code.push_back(0x0C);
    asm_.code.push_back(0x3C); asm_.code.push_back('\t');
    asm_.code.push_back(0x74); asm_.code.push_back(0x08);
    asm_.code.push_back(0x3C); asm_.code.push_back('\n');
    asm_.code.push_back(0x74); asm_.code.push_back(0x04);
    asm_.code.push_back(0x3C); asm_.code.push_back('\r');
    asm_.jnz_rel32(trimTrailDone);
    asm_.code.push_back(0xC6); asm_.code.push_back(0x02); asm_.code.push_back(0x00);
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xCA);
    asm_.jmp_rel32(trimTrailLoop);
    
    asm_.label(trimTrailDone);
    asm_.mov_rax_rcx();
}

void NativeCodeGen::emitStringStartsWith(CallExpr& node) {
    std::string strVal, prefix;
    if (tryEvalConstantString(node.args[0].get(), strVal) && 
        tryEvalConstantString(node.args[1].get(), prefix)) {
        bool result = strVal.size() >= prefix.size() && 
                      strVal.compare(0, prefix.size(), prefix) == 0;
        asm_.mov_rax_imm64(result ? 1 : 0);
        return;
    }
    
    node.args[0]->accept(*this);
    asm_.push_rax();
    node.args[1]->accept(*this);
    asm_.mov_rdx_rax();
    asm_.pop_rcx();
    
    std::string loopLabel = newLabel("starts_loop");
    std::string matchLabel = newLabel("starts_match");
    std::string noMatchLabel = newLabel("starts_nomatch");
    
    asm_.label(loopLabel);
    asm_.code.push_back(0x0F); asm_.code.push_back(0xB6); asm_.code.push_back(0x02);
    asm_.test_rax_rax();
    asm_.jz_rel32(matchLabel);
    
    asm_.code.push_back(0x0F); asm_.code.push_back(0xB6); asm_.code.push_back(0x39);
    asm_.code.push_back(0x39); asm_.code.push_back(0xC7);
    asm_.jnz_rel32(noMatchLabel);
    
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC1);
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC2);
    asm_.jmp_rel32(loopLabel);
    
    asm_.label(matchLabel);
    asm_.mov_rax_imm64(1);
    std::string doneLabel = newLabel("starts_done");
    asm_.jmp_rel32(doneLabel);
    
    asm_.label(noMatchLabel);
    asm_.xor_rax_rax();
    
    asm_.label(doneLabel);
}

void NativeCodeGen::emitStringEndsWith(CallExpr& node) {
    std::string strVal, suffix;
    if (tryEvalConstantString(node.args[0].get(), strVal) && 
        tryEvalConstantString(node.args[1].get(), suffix)) {
        bool result = strVal.size() >= suffix.size() && 
                      strVal.compare(strVal.size() - suffix.size(), suffix.size(), suffix) == 0;
        asm_.mov_rax_imm64(result ? 1 : 0);
        return;
    }
    
    node.args[0]->accept(*this);
    asm_.push_rax();
    
    asm_.mov_rcx_rax();
    asm_.xor_rax_rax();
    std::string lenLoop1 = newLabel("ends_len1");
    std::string lenDone1 = newLabel("ends_len1_done");
    asm_.label(lenLoop1);
    asm_.code.push_back(0x80); asm_.code.push_back(0x39); asm_.code.push_back(0x00);
    asm_.jz_rel32(lenDone1);
    asm_.inc_rax();
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC1);
    asm_.jmp_rel32(lenLoop1);
    asm_.label(lenDone1);
    asm_.push_rax();
    
    node.args[1]->accept(*this);
    asm_.push_rax();
    
    asm_.mov_rcx_rax();
    asm_.xor_rax_rax();
    std::string lenLoop2 = newLabel("ends_len2");
    std::string lenDone2 = newLabel("ends_len2_done");
    asm_.label(lenLoop2);
    asm_.code.push_back(0x80); asm_.code.push_back(0x39); asm_.code.push_back(0x00);
    asm_.jz_rel32(lenDone2);
    asm_.inc_rax();
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC1);
    asm_.jmp_rel32(lenLoop2);
    asm_.label(lenDone2);
    
    asm_.pop_rdx();
    asm_.pop_rcx();
    asm_.pop_rdi();
    
    std::string noMatchLabel = newLabel("ends_nomatch");
    asm_.code.push_back(0x48); asm_.code.push_back(0x39); asm_.code.push_back(0xC1);
    asm_.jl_rel32(noMatchLabel);
    
    asm_.code.push_back(0x48); asm_.code.push_back(0x29); asm_.code.push_back(0xC1);
    asm_.code.push_back(0x48); asm_.code.push_back(0x01); asm_.code.push_back(0xCF);
    asm_.mov_rcx_rdi();
    
    std::string cmpLoop = newLabel("ends_cmp");
    std::string matchLabel = newLabel("ends_match");
    
    asm_.label(cmpLoop);
    asm_.code.push_back(0x0F); asm_.code.push_back(0xB6); asm_.code.push_back(0x02);
    asm_.test_rax_rax();
    asm_.jz_rel32(matchLabel);
    
    asm_.code.push_back(0x0F); asm_.code.push_back(0xB6); asm_.code.push_back(0x39);
    asm_.code.push_back(0x39); asm_.code.push_back(0xC7);
    asm_.jnz_rel32(noMatchLabel);
    
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC1);
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC2);
    asm_.jmp_rel32(cmpLoop);
    
    asm_.label(matchLabel);
    asm_.mov_rax_imm64(1);
    std::string doneLabel = newLabel("ends_done");
    asm_.jmp_rel32(doneLabel);
    
    asm_.label(noMatchLabel);
    asm_.xor_rax_rax();
    
    asm_.label(doneLabel);
}

void NativeCodeGen::emitStringSubstring(CallExpr& node) {
    std::string strVal;
    int64_t start, len = -1;
    bool hasLen = node.args.size() == 3;
    
    if (tryEvalConstantString(node.args[0].get(), strVal) &&
        tryEvalConstant(node.args[1].get(), start) &&
        (!hasLen || tryEvalConstant(node.args[2].get(), len))) {
        if (start < 0) start = 0;
        if ((size_t)start >= strVal.size()) {
            strVal = "";
        } else if (hasLen && len >= 0) {
            strVal = strVal.substr(start, len);
        } else {
            strVal = strVal.substr(start);
        }
        uint32_t rva = addString(strVal);
        asm_.lea_rax_rip_fixup(rva);
        return;
    }
    
    allocLocal("$substr_buf");
    int32_t bufOffset = locals["$substr_buf"];
    for (int i = 0; i < 63; i++) allocLocal("$substr_pad" + std::to_string(i));
    
    node.args[0]->accept(*this);
    asm_.push_rax();
    node.args[1]->accept(*this);
    asm_.mov_rcx_rax();
    
    if (hasLen) {
        node.args[2]->accept(*this);
        asm_.mov_r8_rax();
    } else {
        asm_.mov_rax_imm64(0x7FFFFFFF);
        asm_.mov_r8_rax();
    }
    
    asm_.pop_rax();
    asm_.code.push_back(0x48); asm_.code.push_back(0x01); asm_.code.push_back(0xC8);
    asm_.mov_rcx_rax();
    
    asm_.lea_rax_rbp(bufOffset);
    asm_.mov_rdx_rax();
    
    std::string copyLoop = newLabel("substr_copy");
    std::string copyDone = newLabel("substr_done");
    
    asm_.label(copyLoop);
    asm_.code.push_back(0x4D); asm_.code.push_back(0x85); asm_.code.push_back(0xC0);
    asm_.jz_rel32(copyDone);
    
    asm_.code.push_back(0x0F); asm_.code.push_back(0xB6); asm_.code.push_back(0x01);
    asm_.test_rax_rax();
    asm_.jz_rel32(copyDone);
    
    asm_.code.push_back(0x88); asm_.code.push_back(0x02);
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC1);
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC2);
    asm_.code.push_back(0x49); asm_.code.push_back(0xFF); asm_.code.push_back(0xC8);
    asm_.jmp_rel32(copyLoop);
    
    asm_.label(copyDone);
    asm_.code.push_back(0xC6); asm_.code.push_back(0x02); asm_.code.push_back(0x00);
    
    asm_.lea_rax_rbp(bufOffset);
}

void NativeCodeGen::emitStringReplace(CallExpr& node) {
    std::string strVal, oldStr, newStr;
    if (tryEvalConstantString(node.args[0].get(), strVal) &&
        tryEvalConstantString(node.args[1].get(), oldStr) &&
        tryEvalConstantString(node.args[2].get(), newStr)) {
        size_t pos = strVal.find(oldStr);
        if (pos != std::string::npos) {
            strVal.replace(pos, oldStr.size(), newStr);
        }
        uint32_t rva = addString(strVal);
        asm_.lea_rax_rip_fixup(rva);
        return;
    }
    
    // Runtime: simplified - just return original string for now
    node.args[0]->accept(*this);
}

void NativeCodeGen::emitStringSplit(CallExpr& node) {
    std::string strVal, delim;
    if (tryEvalConstantString(node.args[0].get(), strVal) &&
        tryEvalConstantString(node.args[1].get(), delim)) {
        // Compile-time split - split the string and create a list of strings
        std::vector<std::string> parts;
        
        if (delim.empty()) {
            // Empty delimiter - split into individual characters
            for (char c : strVal) {
                parts.push_back(std::string(1, c));
            }
        } else {
            // Split by delimiter
            size_t start = 0;
            size_t pos;
            while ((pos = strVal.find(delim, start)) != std::string::npos) {
                parts.push_back(strVal.substr(start, pos - start));
                start = pos + delim.length();
            }
            // Add the last part
            parts.push_back(strVal.substr(start));
        }
        
        // Allocate list with capacity for all parts
        size_t capacity = parts.size();
        if (capacity < 4) capacity = 4;
        emitGCAllocList(capacity);
        
        // Save list pointer
        std::string listPtrName = "$split_list_" + std::to_string(labelCounter++);
        allocLocal(listPtrName);
        asm_.mov_mem_rbp_rax(locals[listPtrName]);
        
        // Set count - use mov qword ptr [rax], imm32 directly
        // First reload list pointer into RAX
        asm_.mov_rax_mem_rbp(locals[listPtrName]);
        // Then store count at [RAX+0]
        // mov qword ptr [rax], parts.size()
        asm_.code.push_back(0x48);  // REX.W
        asm_.code.push_back(0xC7);  // MOV r/m64, imm32
        asm_.code.push_back(0x00);  // ModR/M: [rax]
        int32_t count = static_cast<int32_t>(parts.size());
        asm_.code.push_back(count & 0xFF);
        asm_.code.push_back((count >> 8) & 0xFF);
        asm_.code.push_back((count >> 16) & 0xFF);
        asm_.code.push_back((count >> 24) & 0xFF);
        
        // Add each string part to the list
        for (size_t i = 0; i < parts.size(); i++) {
            // Add string to data section
            uint32_t strRva = addString(parts[i]);
            
            // Get string address
            asm_.lea_rax_rip_fixup(strRva);
            
            // Store in list: list[16 + i*8] = string_ptr
            asm_.mov_rcx_mem_rbp(locals[listPtrName]);
            int32_t offset = 16 + static_cast<int32_t>(i * 8);
            asm_.add_rcx_imm32(offset);
            asm_.mov_mem_rcx_rax();
        }
        
        // Return list pointer
        asm_.mov_rax_mem_rbp(locals[listPtrName]);
        return;
    }
    
    // Runtime split - more complex, needs string scanning
    // For now, create a single-element list with the original string
    node.args[0]->accept(*this);
    asm_.push_rax();  // save string pointer
    
    // Allocate list with capacity 16 (reasonable default)
    emitGCAllocList(16);
    
    std::string listPtrName = "$split_rt_" + std::to_string(labelCounter++);
    allocLocal(listPtrName);
    asm_.mov_mem_rbp_rax(locals[listPtrName]);
    
    // For runtime, we need to implement the split loop
    // Save delimiter
    node.args[1]->accept(*this);
    std::string delimPtrName = "$split_delim_" + std::to_string(labelCounter++);
    allocLocal(delimPtrName);
    asm_.mov_mem_rbp_rax(locals[delimPtrName]);
    
    // Get delimiter length
    asm_.mov_rcx_rax();
    asm_.xor_rax_rax();
    std::string delimLenLoop = newLabel("delim_len");
    std::string delimLenDone = newLabel("delim_len_done");
    asm_.label(delimLenLoop);
    // Load byte from [rcx]
    asm_.code.push_back(0x0F); asm_.code.push_back(0xB6); asm_.code.push_back(0x11); // movzx edx, byte [rcx]
    asm_.code.push_back(0x85); asm_.code.push_back(0xD2); // test edx, edx
    asm_.jz_rel32(delimLenDone);
    asm_.inc_rax();
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC1); // inc rcx
    asm_.jmp_rel32(delimLenLoop);
    asm_.label(delimLenDone);
    
    std::string delimLenName = "$split_dlen_" + std::to_string(labelCounter++);
    allocLocal(delimLenName);
    asm_.mov_mem_rbp_rax(locals[delimLenName]);
    
    // Restore string pointer
    asm_.pop_rax();
    std::string strPtrName = "$split_str_" + std::to_string(labelCounter++);
    allocLocal(strPtrName);
    asm_.mov_mem_rbp_rax(locals[strPtrName]);
    
    // Current position in string
    std::string curPosName = "$split_pos_" + std::to_string(labelCounter++);
    allocLocal(curPosName);
    asm_.mov_mem_rbp_rax(locals[curPosName]);
    
    // Start of current part
    std::string partStartName = "$split_start_" + std::to_string(labelCounter++);
    allocLocal(partStartName);
    asm_.mov_mem_rbp_rax(locals[partStartName]);
    
    // Count of parts
    std::string countName = "$split_count_" + std::to_string(labelCounter++);
    allocLocal(countName);
    asm_.xor_rax_rax();
    asm_.mov_mem_rbp_rax(locals[countName]);
    
    // Main split loop
    std::string splitLoop = newLabel("split_loop");
    std::string splitDone = newLabel("split_done");
    std::string foundDelim = newLabel("found_delim");
    std::string noMatch = newLabel("no_match");
    
    asm_.label(splitLoop);
    
    // Check if current char is null
    asm_.mov_rax_mem_rbp(locals[curPosName]);
    asm_.code.push_back(0x0F); asm_.code.push_back(0xB6); asm_.code.push_back(0x00); // movzx eax, byte [rax]
    asm_.test_rax_rax();
    asm_.jz_rel32(splitDone);
    
    // Check if delimiter matches at current position
    // Compare delimiter with string at current position
    asm_.mov_rax_mem_rbp(locals[delimLenName]);
    asm_.test_rax_rax();
    asm_.jz_rel32(noMatch);  // empty delimiter, no match
    
    // Simple single-char delimiter check for now
    asm_.mov_rax_mem_rbp(locals[curPosName]);
    asm_.code.push_back(0x0F); asm_.code.push_back(0xB6); asm_.code.push_back(0x00); // movzx eax, byte [rax]
    asm_.mov_rcx_mem_rbp(locals[delimPtrName]);
    asm_.code.push_back(0x0F); asm_.code.push_back(0xB6); asm_.code.push_back(0x09); // movzx ecx, byte [rcx]
    asm_.code.push_back(0x39); asm_.code.push_back(0xC8); // cmp eax, ecx
    asm_.jnz_rel32(noMatch);
    
    // Found delimiter - add current part to list
    asm_.label(foundDelim);
    
    // Calculate part length
    asm_.mov_rax_mem_rbp(locals[curPosName]);
    asm_.mov_rcx_mem_rbp(locals[partStartName]);
    asm_.code.push_back(0x48); asm_.code.push_back(0x29); asm_.code.push_back(0xC8); // sub rax, rcx -> length in rax
    asm_.push_rax();  // save length
    
    // Allocate string for this part (length + 1 for null)
    asm_.inc_rax();
    asm_.mov_rcx_rax();
    emitGCAllocRaw(256);  // allocate buffer
    
    std::string partStrName = "$split_part_" + std::to_string(labelCounter++);
    allocLocal(partStrName);
    asm_.mov_mem_rbp_rax(locals[partStrName]);
    
    // Copy part to new string
    asm_.mov_rdi_rax();  // dest
    asm_.mov_rsi_mem_rbp(locals[partStartName]);  // src
    asm_.pop_rcx();  // length
    asm_.push_rcx();
    
    std::string copyLoop = newLabel("copy_part");
    std::string copyDone = newLabel("copy_done");
    asm_.label(copyLoop);
    asm_.test_rcx_rcx();
    asm_.jz_rel32(copyDone);
    asm_.code.push_back(0x0F); asm_.code.push_back(0xB6); asm_.code.push_back(0x06); // movzx eax, byte [rsi]
    asm_.code.push_back(0x88); asm_.code.push_back(0x07); // mov [rdi], al
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC6); // inc rsi
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC7); // inc rdi
    asm_.dec_rcx();
    asm_.jmp_rel32(copyLoop);
    asm_.label(copyDone);
    asm_.code.push_back(0xC6); asm_.code.push_back(0x07); asm_.code.push_back(0x00); // mov byte [rdi], 0
    
    asm_.pop_rcx();  // discard length
    
    // Store part in list
    asm_.mov_rax_mem_rbp(locals[countName]);
    asm_.mov_rcx_rax();
    asm_.code.push_back(0x48); asm_.code.push_back(0xC1); asm_.code.push_back(0xE1); asm_.code.push_back(0x03); // shl rcx, 3
    asm_.add_rcx_imm32(16);
    asm_.mov_rax_mem_rbp(locals[listPtrName]);
    asm_.code.push_back(0x48); asm_.code.push_back(0x01); asm_.code.push_back(0xC8); // add rax, rcx
    asm_.mov_rcx_mem_rbp(locals[partStrName]);
    asm_.mov_mem_rax_rcx();
    
    // Increment count
    asm_.mov_rax_mem_rbp(locals[countName]);
    asm_.inc_rax();
    asm_.mov_mem_rbp_rax(locals[countName]);
    
    // Move past delimiter
    asm_.mov_rax_mem_rbp(locals[curPosName]);
    asm_.mov_rcx_mem_rbp(locals[delimLenName]);
    asm_.code.push_back(0x48); asm_.code.push_back(0x01); asm_.code.push_back(0xC8); // add rax, rcx
    asm_.mov_mem_rbp_rax(locals[curPosName]);
    asm_.mov_mem_rbp_rax(locals[partStartName]);  // new part starts here
    asm_.jmp_rel32(splitLoop);
    
    asm_.label(noMatch);
    // Move to next character
    asm_.mov_rax_mem_rbp(locals[curPosName]);
    asm_.inc_rax();
    asm_.mov_mem_rbp_rax(locals[curPosName]);
    asm_.jmp_rel32(splitLoop);
    
    asm_.label(splitDone);
    
    // Add final part (from partStart to end)
    asm_.mov_rax_mem_rbp(locals[curPosName]);
    asm_.mov_rcx_mem_rbp(locals[partStartName]);
    asm_.code.push_back(0x48); asm_.code.push_back(0x29); asm_.code.push_back(0xC8); // sub rax, rcx
    asm_.push_rax();  // save length
    
    // Allocate string for final part
    asm_.inc_rax();
    asm_.mov_rcx_rax();
    emitGCAllocRaw(256);
    
    std::string finalPartName = "$split_final_" + std::to_string(labelCounter++);
    allocLocal(finalPartName);
    asm_.mov_mem_rbp_rax(locals[finalPartName]);
    
    // Copy final part
    asm_.mov_rdi_rax();
    asm_.mov_rsi_mem_rbp(locals[partStartName]);
    asm_.pop_rcx();
    asm_.push_rcx();
    
    std::string copyLoop2 = newLabel("copy_final");
    std::string copyDone2 = newLabel("copy_final_done");
    asm_.label(copyLoop2);
    asm_.test_rcx_rcx();
    asm_.jz_rel32(copyDone2);
    asm_.code.push_back(0x0F); asm_.code.push_back(0xB6); asm_.code.push_back(0x06);
    asm_.code.push_back(0x88); asm_.code.push_back(0x07);
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC6);
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC7);
    asm_.dec_rcx();
    asm_.jmp_rel32(copyLoop2);
    asm_.label(copyDone2);
    asm_.code.push_back(0xC6); asm_.code.push_back(0x07); asm_.code.push_back(0x00);
    
    asm_.pop_rcx();
    
    // Store final part in list
    asm_.mov_rax_mem_rbp(locals[countName]);
    asm_.mov_rcx_rax();
    asm_.code.push_back(0x48); asm_.code.push_back(0xC1); asm_.code.push_back(0xE1); asm_.code.push_back(0x03);
    asm_.add_rcx_imm32(16);
    asm_.mov_rax_mem_rbp(locals[listPtrName]);
    asm_.code.push_back(0x48); asm_.code.push_back(0x01); asm_.code.push_back(0xC8);
    asm_.mov_rcx_mem_rbp(locals[finalPartName]);
    asm_.mov_mem_rax_rcx();
    
    // Increment count
    asm_.mov_rax_mem_rbp(locals[countName]);
    asm_.inc_rax();
    asm_.mov_mem_rbp_rax(locals[countName]);
    
    // Set list count
    asm_.mov_rax_mem_rbp(locals[listPtrName]);
    asm_.mov_rcx_mem_rbp(locals[countName]);
    asm_.mov_mem_rax_rcx();
    
    // Return list
    asm_.mov_rax_mem_rbp(locals[listPtrName]);
}

void NativeCodeGen::emitStringJoin(CallExpr& node) {
    // join(list, delimiter) -> string
    // Simplified implementation for now
    
    // For constant string lists, we can compute at compile time
    if (auto* listExpr = dynamic_cast<ListExpr*>(node.args[0].get())) {
        std::string delim;
        if (tryEvalConstantString(node.args[1].get(), delim)) {
            std::string result;
            for (size_t i = 0; i < listExpr->elements.size(); i++) {
                std::string elemStr;
                if (tryEvalConstantString(listExpr->elements[i].get(), elemStr)) {
                    if (i > 0) result += delim;
                    result += elemStr;
                } else {
                    // Can't evaluate at compile time, fall through to runtime
                    goto runtime_join;
                }
            }
            uint32_t rva = addString(result);
            asm_.lea_rax_rip_fixup(rva);
            return;
        }
    }
    
runtime_join:
    // Save RDI if stdout caching is enabled (RDI holds stdout handle)
    // We use RDI for string copying in join, so we need to preserve it
    bool savedStdoutCached = stdoutHandleCached_;
    if (useStdoutCaching_ && stdoutHandleCached_) {
        asm_.push_rdi();
    }
    
    // Runtime join - evaluate list and delimiter
    node.args[0]->accept(*this);
    std::string listPtrName = "$join_list_" + std::to_string(labelCounter++);
    allocLocal(listPtrName);
    asm_.mov_mem_rbp_rax(locals[listPtrName]);
    
    node.args[1]->accept(*this);
    std::string delimPtrName = "$join_delim_" + std::to_string(labelCounter++);
    allocLocal(delimPtrName);
    asm_.mov_mem_rbp_rax(locals[delimPtrName]);
    
    // Get list count from [list+0]
    asm_.mov_rax_mem_rbp(locals[listPtrName]);
    asm_.mov_rax_mem_rax();  // rax = [rax] = count
    
    // Check for empty list
    asm_.test_rax_rax();
    std::string emptyList = newLabel("join_empty");
    std::string notEmpty = newLabel("join_not_empty");
    asm_.jz_rel32(emptyList);
    asm_.jmp_rel32(notEmpty);
    
    asm_.label(emptyList);
    uint32_t emptyRva = addString("");
    asm_.lea_rax_rip_fixup(emptyRva);
    // Restore RDI before jumping to end (empty list path)
    if (useStdoutCaching_ && savedStdoutCached) {
        asm_.pop_rdi();
    }
    std::string joinEnd = newLabel("join_end");
    asm_.jmp_rel32(joinEnd);
    
    asm_.label(notEmpty);
    
    // Allocate result buffer (1KB should be enough for most cases)
    emitGCAllocRaw(1024);
    std::string resultPtrName = "$join_result_" + std::to_string(labelCounter++);
    allocLocal(resultPtrName);
    asm_.mov_mem_rbp_rax(locals[resultPtrName]);
    
    // Current write position
    std::string writePosName = "$join_wpos_" + std::to_string(labelCounter++);
    allocLocal(writePosName);
    asm_.mov_mem_rbp_rax(locals[writePosName]);
    
    // Index counter
    std::string idxName = "$join_idx_" + std::to_string(labelCounter++);
    allocLocal(idxName);
    asm_.xor_rax_rax();
    asm_.mov_mem_rbp_rax(locals[idxName]);
    
    // Get count again
    asm_.mov_rax_mem_rbp(locals[listPtrName]);
    asm_.mov_rax_mem_rax();
    std::string countName = "$join_count_" + std::to_string(labelCounter++);
    allocLocal(countName);
    asm_.mov_mem_rbp_rax(locals[countName]);
    
    // Main loop
    std::string loopStart = newLabel("join_loop");
    std::string loopEnd = newLabel("join_loop_end");
    
    asm_.label(loopStart);
    
    // Check if idx < count
    asm_.mov_rax_mem_rbp(locals[idxName]);
    asm_.cmp_rax_mem_rbp(locals[countName]);
    asm_.jge_rel32(loopEnd);
    
    // If not first element, add delimiter
    asm_.test_rax_rax();
    std::string skipDelim = newLabel("join_skip_delim");
    asm_.jz_rel32(skipDelim);
    
    // Copy delimiter
    asm_.mov_rsi_mem_rbp(locals[delimPtrName]);
    asm_.mov_rdi_mem_rbp(locals[writePosName]);
    
    std::string delimCopy = newLabel("join_delim_copy");
    std::string delimCopyDone = newLabel("join_delim_done");
    asm_.label(delimCopy);
    asm_.code.push_back(0x0F); asm_.code.push_back(0xB6); asm_.code.push_back(0x06); // movzx eax, byte [rsi]
    asm_.test_rax_rax();
    asm_.jz_rel32(delimCopyDone);
    asm_.code.push_back(0x88); asm_.code.push_back(0x07); // mov [rdi], al
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC6); // inc rsi
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC7); // inc rdi
    asm_.jmp_rel32(delimCopy);
    asm_.label(delimCopyDone);
    asm_.mov_rax_rdi();
    asm_.mov_mem_rbp_rax(locals[writePosName]);
    
    asm_.label(skipDelim);
    
    // Get string at list[16 + idx*8]
    // Calculate element address: list_ptr + 16 + idx*8
    asm_.mov_rax_mem_rbp(locals[listPtrName]);  // rax = list_ptr
    asm_.add_rax_imm32(16);                      // rax = list_ptr + 16
    asm_.mov_rcx_mem_rbp(locals[idxName]);       // rcx = idx
    asm_.code.push_back(0x48); asm_.code.push_back(0xC1); asm_.code.push_back(0xE1); asm_.code.push_back(0x03); // shl rcx, 3
    asm_.add_rax_rcx();                          // rax = list_ptr + 16 + idx*8
    asm_.mov_rax_mem_rax();                      // rax = [rax] = string pointer
    asm_.mov_rsi_rax();                          // rsi = string pointer
    asm_.mov_rdi_mem_rbp(locals[writePosName]); // rdi = write position
    
    // Copy string
    std::string strCopy = newLabel("join_str_copy");
    std::string strCopyDone = newLabel("join_str_done");
    asm_.label(strCopy);
    asm_.code.push_back(0x0F); asm_.code.push_back(0xB6); asm_.code.push_back(0x06); // movzx eax, byte [rsi]
    asm_.test_rax_rax();
    asm_.jz_rel32(strCopyDone);
    asm_.code.push_back(0x88); asm_.code.push_back(0x07); // mov [rdi], al
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC6); // inc rsi
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC7); // inc rdi
    asm_.jmp_rel32(strCopy);
    asm_.label(strCopyDone);
    asm_.mov_rax_rdi();
    asm_.mov_mem_rbp_rax(locals[writePosName]);
    
    // Increment index
    asm_.mov_rax_mem_rbp(locals[idxName]);
    asm_.inc_rax();
    asm_.mov_mem_rbp_rax(locals[idxName]);
    asm_.jmp_rel32(loopStart);
    
    asm_.label(loopEnd);
    
    // Null terminate
    asm_.mov_rax_mem_rbp(locals[writePosName]);
    asm_.code.push_back(0xC6); asm_.code.push_back(0x00); asm_.code.push_back(0x00); // mov byte [rax], 0
    
    // Return result
    asm_.mov_rax_mem_rbp(locals[resultPtrName]);
    
    // Restore RDI before end (normal path)
    if (useStdoutCaching_ && savedStdoutCached) {
        asm_.pop_rdi();
    }
    
    asm_.label(joinEnd);
}

void NativeCodeGen::emitStringIndexOf(CallExpr& node) {
    std::string strVal, substr;
    if (tryEvalConstantString(node.args[0].get(), strVal) &&
        tryEvalConstantString(node.args[1].get(), substr)) {
        size_t pos = strVal.find(substr);
        int64_t result = (pos == std::string::npos) ? -1 : static_cast<int64_t>(pos);
        asm_.mov_rax_imm64(result);
        return;
    }
    
    // Runtime implementation would go here
    asm_.mov_rax_imm64(-1);
}

} // namespace tyl
