// Flex Compiler - Native Code Generator Call Expression Handler
// Handles: print, println, str, len, upper, contains, push, range and general function calls

#include "codegen_base.h"

namespace flex {

void NativeCodeGen::visit(CallExpr& node) {
    // Handle module function calls (e.g., math.add)
    if (auto* member = dynamic_cast<MemberExpr*>(node.callee.get())) {
        if (auto* moduleId = dynamic_cast<Identifier*>(member->object.get())) {
            // This is a module.function call
            std::string mangledName = moduleId->name + "." + member->member;
            
            // Check if this is a known module function
            if (asm_.labels.count(mangledName)) {
                // Load arguments into registers
                for (int i = (int)node.args.size() - 1; i >= 0; i--) {
                    node.args[i]->accept(*this);
                    asm_.push_rax();
                }
                
                // Pop arguments into registers (Windows x64: rcx, rdx, r8, r9)
                if (node.args.size() >= 1) asm_.pop_rcx();
                if (node.args.size() >= 2) asm_.pop_rdx();
                if (node.args.size() >= 3) {
                    asm_.code.push_back(0x41); asm_.code.push_back(0x58);
                }
                if (node.args.size() >= 4) {
                    asm_.code.push_back(0x41); asm_.code.push_back(0x59);
                }
                
                // Call the module function
                if (!stackAllocated_) asm_.sub_rsp_imm32(0x20);
                asm_.call_rel32(mangledName);
                if (!stackAllocated_) asm_.add_rsp_imm32(0x20);
                return;
            }
        }
    }
    
    if (auto* id = dynamic_cast<Identifier*>(node.callee.get())) {
        // Check if this is an extern function
        auto externIt = externFunctions_.find(id->name);
        if (externIt != externFunctions_.end()) {
            // Load arguments into registers
            for (int i = (int)node.args.size() - 1; i >= 0; i--) {
                node.args[i]->accept(*this);
                asm_.push_rax();
            }
            
            // Pop arguments into registers (Windows x64: rcx, rdx, r8, r9)
            if (node.args.size() >= 1) asm_.pop_rcx();
            if (node.args.size() >= 2) asm_.pop_rdx();
            if (node.args.size() >= 3) {
                asm_.code.push_back(0x41); asm_.code.push_back(0x58);
            }
            if (node.args.size() >= 4) {
                asm_.code.push_back(0x41); asm_.code.push_back(0x59);
            }
            
            // Call the extern function via import table
            if (!stackAllocated_) asm_.sub_rsp_imm32(0x20);
            asm_.call_mem_rip(pe_.getImportRVA(id->name));
            if (!stackAllocated_) asm_.add_rsp_imm32(0x20);
            return;
        }
        
        // Handle len() - returns length of list or string
        if (id->name == "len" && node.args.size() == 1) {
            // For strings, calculate length
            if (auto* strLit = dynamic_cast<StringLiteral*>(node.args[0].get())) {
                asm_.mov_rax_imm64((int64_t)strLit->value.length());
                return;
            }
            if (auto* ident = dynamic_cast<Identifier*>(node.args[0].get())) {
                // Check if it's a known constant string with value
                auto strIt = constStrVars.find(ident->name);
                if (strIt != constStrVars.end() && !strIt->second.empty()) {
                    asm_.mov_rax_imm64((int64_t)strIt->second.length());
                    return;
                }
                // Check if it's a list variable
                auto listIt = listSizes.find(ident->name);
                if (listIt != listSizes.end()) {
                    asm_.mov_rax_imm64((int64_t)listIt->second);
                    return;
                }
                // Check if it's a constant list
                auto constListIt = constListVars.find(ident->name);
                if (constListIt != constListVars.end()) {
                    asm_.mov_rax_imm64((int64_t)constListIt->second.size());
                    return;
                }
                // Check if it's a runtime string variable - calculate length at runtime
                if (strIt != constStrVars.end()) {
                    // Load string pointer
                    node.args[0]->accept(*this);
                    // Calculate strlen
                    asm_.mov_rcx_rax();  // rcx = string pointer
                    asm_.xor_rax_rax();  // rax = length counter
                    
                    std::string loopLabel = newLabel("strlen_loop");
                    std::string doneLabel = newLabel("strlen_done");
                    
                    asm_.label(loopLabel);
                    // movzx rdx, byte [rcx + rax]
                    asm_.code.push_back(0x48); asm_.code.push_back(0x0F); asm_.code.push_back(0xB6);
                    asm_.code.push_back(0x14); asm_.code.push_back(0x01);
                    // test dl, dl
                    asm_.code.push_back(0x84); asm_.code.push_back(0xD2);
                    // jz done
                    asm_.jz_rel32(doneLabel);
                    // inc rax
                    asm_.inc_rax();
                    // jmp loop
                    asm_.jmp_rel32(loopLabel);
                    
                    asm_.label(doneLabel);
                    return;
                }
            }
            // For lists passed directly
            if (auto* list = dynamic_cast<ListExpr*>(node.args[0].get())) {
                asm_.mov_rax_imm64((int64_t)list->elements.size());
                return;
            }
            asm_.xor_rax_rax();
            return;
        }
        
        // Handle upper() - returns uppercase string
        if (id->name == "upper" && node.args.size() == 1) {
            std::string strVal;
            if (tryEvalConstantString(node.args[0].get(), strVal)) {
                // Convert to uppercase at compile time
                for (char& c : strVal) {
                    if (c >= 'a' && c <= 'z') c -= 32;
                }
                uint32_t rva = addString(strVal);
                asm_.lea_rax_rip_fixup(rva);
                return;
            }
            
            // For runtime strings, we need to allocate a buffer and convert
            // For now, allocate a buffer on the stack and copy with uppercase conversion
            allocLocal("$upper_buf");
            int32_t bufOffset = locals["$upper_buf"];
            // Allocate more space for the buffer (256 bytes)
            for (int i = 0; i < 31; i++) allocLocal("$upper_pad" + std::to_string(i));
            
            // Load source string pointer
            node.args[0]->accept(*this);
            asm_.mov_rcx_rax();  // rcx = source pointer
            
            // Get buffer address
            asm_.lea_rax_rbp(bufOffset);
            asm_.mov_rdx_rax();  // rdx = dest pointer
            
            // Copy and convert loop
            std::string loopLabel = newLabel("upper_loop");
            std::string doneLabel = newLabel("upper_done");
            std::string noConvertLabel = newLabel("upper_noconv");
            
            asm_.label(loopLabel);
            // Load byte from source: movzx eax, byte [rcx]
            asm_.code.push_back(0x0F); asm_.code.push_back(0xB6); asm_.code.push_back(0x01);
            
            // Check for null terminator
            asm_.test_rax_rax();
            asm_.jz_rel32(doneLabel);
            
            // Check if lowercase (a-z): cmp al, 'a'
            asm_.code.push_back(0x3C); asm_.code.push_back('a');
            asm_.jl_rel32(noConvertLabel);
            // cmp al, 'z'
            asm_.code.push_back(0x3C); asm_.code.push_back('z');
            asm_.jg_rel32(noConvertLabel);
            
            // Convert to uppercase: sub al, 32
            asm_.code.push_back(0x2C); asm_.code.push_back(32);
            
            asm_.label(noConvertLabel);
            // Store byte to dest: mov byte [rdx], al
            asm_.code.push_back(0x88); asm_.code.push_back(0x02);
            
            // Increment pointers
            asm_.inc_rax();  // This clobbers rax, but we already stored the byte
            // inc rcx
            asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC1);
            // inc rdx
            asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC2);
            
            asm_.jmp_rel32(loopLabel);
            
            asm_.label(doneLabel);
            // Write null terminator: mov byte [rdx], 0
            asm_.code.push_back(0xC6); asm_.code.push_back(0x02); asm_.code.push_back(0x00);
            
            // Return pointer to buffer
            asm_.lea_rax_rbp(bufOffset);
            return;
        }
        
        // Handle contains() - returns bool if string contains substring
        if (id->name == "contains" && node.args.size() == 2) {
            std::string haystack, needle;
            bool haystackConst = tryEvalConstantString(node.args[0].get(), haystack);
            bool needleConst = tryEvalConstantString(node.args[1].get(), needle);
            
            if (haystackConst && needleConst) {
                bool found = haystack.find(needle) != std::string::npos;
                asm_.mov_rax_imm64(found ? 1 : 0);
                return;
            }
            // Non-constant case - return 0 for now
            asm_.xor_rax_rax();
            return;
        }
        
        // Handle push() - appends element to list, returns new list
        // List structure: [size:8 bytes][capacity:8 bytes][elements...]
        if (id->name == "push" && node.args.size() == 2) {
            // Get the list pointer
            node.args[0]->accept(*this);
            asm_.push_rax();  // Save list pointer
            
            // Get the element to push
            node.args[1]->accept(*this);
            asm_.push_rax();  // Save element
            
            // Check if we know the list size at compile time
            std::string listName;
            size_t oldSize = 0;
            bool knownSize = false;
            if (auto* ident = dynamic_cast<Identifier*>(node.args[0].get())) {
                listName = ident->name;
                auto sizeIt = listSizes.find(listName);
                if (sizeIt != listSizes.end()) {
                    oldSize = sizeIt->second;
                    knownSize = true;
                }
            }
            
            if (knownSize && oldSize > 0) {
                // Compile-time known size - use optimized path
                size_t newSize = oldSize + 1;
                size_t allocSize = newSize * 8;
                
                if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
                asm_.call_mem_rip(pe_.getImportRVA("GetProcessHeap"));
                
                asm_.mov_rcx_rax();
                asm_.xor_rax_rax();
                asm_.mov_rdx_rax();
                asm_.mov_r8d_imm32((int32_t)allocSize);
                
                asm_.call_mem_rip(pe_.getImportRVA("HeapAlloc"));
                if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
                
                // Save new list pointer
                allocLocal("$push_newlist");
                asm_.mov_mem_rbp_rax(locals["$push_newlist"]);
                
                // Copy old elements
                for (size_t i = 0; i < oldSize; i++) {
                    // Load from old list: mov rax, [rsp + 8]
                    asm_.code.push_back(0x48); asm_.code.push_back(0x8B);
                    asm_.code.push_back(0x44); asm_.code.push_back(0x24);
                    asm_.code.push_back(0x08);
                    
                    if (i > 0) {
                        asm_.add_rax_imm32((int32_t)(i * 8));
                    }
                    asm_.mov_rax_mem_rax();
                    
                    asm_.mov_rcx_mem_rbp(locals["$push_newlist"]);
                    if (i > 0) {
                        asm_.add_rcx_imm32((int32_t)(i * 8));
                    }
                    asm_.mov_mem_rcx_rax();
                }
                
                // Store new element at the end
                asm_.pop_rax();  // Get element
                asm_.mov_rcx_mem_rbp(locals["$push_newlist"]);
                asm_.add_rcx_imm32((int32_t)(oldSize * 8));
                asm_.mov_mem_rcx_rax();
                
                asm_.pop_rcx();  // Discard old list pointer
                
                // Return new list pointer
                asm_.mov_rax_mem_rbp(locals["$push_newlist"]);
                
                // Update size tracking
                if (!listName.empty()) {
                    listSizes[listName] = newSize;
                }
            } else {
                // Dynamic list - use runtime size from list header
                // List format: first 8 bytes = size, then elements
                // Allocate new list with size + 1
                
                allocLocal("$push_oldlist");
                allocLocal("$push_element");
                allocLocal("$push_oldsize");
                allocLocal("$push_newlist");
                
                // Save element and old list
                asm_.pop_rax();
                asm_.mov_mem_rbp_rax(locals["$push_element"]);
                asm_.pop_rax();
                asm_.mov_mem_rbp_rax(locals["$push_oldlist"]);
                
                // Get old size from list header (first 8 bytes)
                asm_.mov_rax_mem_rax();  // size = *oldlist
                asm_.mov_mem_rbp_rax(locals["$push_oldsize"]);
                
                // Calculate new allocation size: (size + 1 + 1) * 8 = (size + 2) * 8
                // +1 for header, +1 for new element
                asm_.add_rax_imm32(2);
                // Multiply by 8: shl rax, 3
                asm_.code.push_back(0x48); asm_.code.push_back(0xC1);
                asm_.code.push_back(0xE0); asm_.code.push_back(0x03);
                asm_.push_rax();  // Save alloc size
                
                // Allocate new list
                if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
                asm_.call_mem_rip(pe_.getImportRVA("GetProcessHeap"));
                asm_.mov_rcx_rax();
                asm_.xor_rax_rax();
                asm_.mov_rdx_rax();
                // pop r8 (alloc size)
                asm_.code.push_back(0x41); asm_.code.push_back(0x58);
                asm_.call_mem_rip(pe_.getImportRVA("HeapAlloc"));
                if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
                
                asm_.mov_mem_rbp_rax(locals["$push_newlist"]);
                
                // Write new size (oldsize + 1) to header
                asm_.mov_rcx_mem_rbp(locals["$push_oldsize"]);
                asm_.inc_rcx();
                asm_.mov_mem_rax_rcx();  // *newlist = newsize
                
                // Copy old elements using a loop
                // for (i = 0; i < oldsize; i++) newlist[i+1] = oldlist[i+1]
                allocLocal("$push_idx");
                asm_.xor_rax_rax();
                asm_.mov_mem_rbp_rax(locals["$push_idx"]);
                
                std::string copyLoop = newLabel("push_copy");
                std::string copyDone = newLabel("push_done");
                
                asm_.label(copyLoop);
                asm_.mov_rax_mem_rbp(locals["$push_idx"]);
                asm_.cmp_rax_mem_rbp(locals["$push_oldsize"]);
                asm_.jge_rel32(copyDone);
                
                // Load from old list: oldlist[(idx+1)*8]
                asm_.mov_rcx_mem_rbp(locals["$push_oldlist"]);
                asm_.mov_rax_mem_rbp(locals["$push_idx"]);
                asm_.inc_rax();
                // shl rax, 3
                asm_.code.push_back(0x48); asm_.code.push_back(0xC1);
                asm_.code.push_back(0xE0); asm_.code.push_back(0x03);
                asm_.add_rax_rcx();
                asm_.mov_rax_mem_rax();  // element value
                asm_.push_rax();
                
                // Store to new list: newlist[(idx+1)*8]
                asm_.mov_rcx_mem_rbp(locals["$push_newlist"]);
                asm_.mov_rax_mem_rbp(locals["$push_idx"]);
                asm_.inc_rax();
                // shl rax, 3
                asm_.code.push_back(0x48); asm_.code.push_back(0xC1);
                asm_.code.push_back(0xE0); asm_.code.push_back(0x03);
                asm_.add_rax_rcx();
                asm_.pop_rcx();
                asm_.mov_mem_rax_rcx();
                
                // idx++
                asm_.mov_rax_mem_rbp(locals["$push_idx"]);
                asm_.inc_rax();
                asm_.mov_mem_rbp_rax(locals["$push_idx"]);
                asm_.jmp_rel32(copyLoop);
                
                asm_.label(copyDone);
                
                // Store new element at newlist[(oldsize+1)*8]
                asm_.mov_rcx_mem_rbp(locals["$push_newlist"]);
                asm_.mov_rax_mem_rbp(locals["$push_oldsize"]);
                asm_.inc_rax();
                // shl rax, 3
                asm_.code.push_back(0x48); asm_.code.push_back(0xC1);
                asm_.code.push_back(0xE0); asm_.code.push_back(0x03);
                asm_.add_rax_rcx();
                asm_.mov_rcx_mem_rbp(locals["$push_element"]);
                asm_.mov_mem_rax_rcx();
                
                // Return new list pointer
                asm_.mov_rax_mem_rbp(locals["$push_newlist"]);
            }
            return;
        }
        
        // Handle pop() - removes last element from list, returns the element
        if (id->name == "pop" && node.args.size() == 1) {
            // Get the list pointer
            node.args[0]->accept(*this);
            
            // Check if we know the list size at compile time
            std::string listName;
            size_t listSize = 0;
            bool knownSize = false;
            if (auto* ident = dynamic_cast<Identifier*>(node.args[0].get())) {
                listName = ident->name;
                auto sizeIt = listSizes.find(listName);
                if (sizeIt != listSizes.end()) {
                    listSize = sizeIt->second;
                    knownSize = true;
                }
            }
            
            if (knownSize && listSize > 0) {
                // Compile-time known size
                asm_.add_rax_imm32((int32_t)((listSize - 1) * 8));
                asm_.mov_rax_mem_rax();
                
                if (!listName.empty()) {
                    listSizes[listName] = listSize - 1;
                }
            } else {
                // Dynamic list - read size from header
                // List format: first 8 bytes = size, then elements
                allocLocal("$pop_list");
                asm_.mov_mem_rbp_rax(locals["$pop_list"]);
                
                // Get size from header
                asm_.mov_rcx_mem_rax();  // size = *list
                
                // Return element at list[size * 8] (last element)
                // shl rcx, 3
                asm_.code.push_back(0x48); asm_.code.push_back(0xC1);
                asm_.code.push_back(0xE1); asm_.code.push_back(0x03);
                asm_.add_rax_rcx();
                asm_.mov_rax_mem_rax();
                
                // Note: We don't actually shrink the list, just return the last element
                // A proper implementation would decrement the size in the header
            }
            return;
        }
        
        // Handle len() - returns list length
        if (id->name == "len" && node.args.size() == 1) {
            // Check if we know the size at compile time
            if (auto* ident = dynamic_cast<Identifier*>(node.args[0].get())) {
                auto sizeIt = listSizes.find(ident->name);
                if (sizeIt != listSizes.end()) {
                    asm_.mov_rax_imm64(sizeIt->second);
                    return;
                }
            }
            
            // Dynamic list - read size from header
            node.args[0]->accept(*this);
            asm_.mov_rax_mem_rax();  // size = *list
            return;
        }
        
        // Handle range() - this is handled specially in ForStmt, but if called directly return 0
        if (id->name == "range") {
            asm_.xor_rax_rax();
            return;
        }
        
        // Handle platform() - returns "windows"
        if (id->name == "platform") {
            uint32_t rva = addString("windows");
            asm_.lea_rax_rip_fixup(rva);
            return;
        }
        
        // Handle arch() - returns "x64"
        if (id->name == "arch") {
            uint32_t rva = addString("x64");
            asm_.lea_rax_rip_fixup(rva);
            return;
        }
        
        // Handle hostname() - get computer name
        if (id->name == "hostname") {
            // Allocate buffer on stack for hostname (256 bytes)
            allocLocal("$hostname_buf");
            int32_t bufOffset = locals["$hostname_buf"];
            // We need more space, allocate 256 bytes
            for (int i = 0; i < 31; i++) allocLocal("$hostname_pad" + std::to_string(i));
            
            allocLocal("$hostname_size");
            int32_t sizeOffset = locals["$hostname_size"];
            
            // Set size to 256
            asm_.mov_rax_imm64(256);
            asm_.mov_mem_rbp_rax(sizeOffset);
            
            // Call GetComputerNameA(buffer, &size)
            // Stack frame optimization: only adjust if not already allocated
            if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
            asm_.lea_rax_rbp(bufOffset);
            asm_.mov_rcx_rax();
            asm_.lea_rax_rbp(sizeOffset);
            asm_.mov_rdx_rax();
            asm_.call_mem_rip(pe_.getImportRVA("GetComputerNameA"));
            if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
            
            // Return pointer to buffer
            asm_.lea_rax_rbp(bufOffset);
            return;
        }
        
        // Handle username() - get current user name via USERNAME env var
        if (id->name == "username") {
            // Allocate buffer on stack (256 bytes)
            allocLocal("$username_buf");
            int32_t bufOffset = locals["$username_buf"];
            for (int i = 0; i < 31; i++) allocLocal("$username_pad" + std::to_string(i));
            
            // Call GetEnvironmentVariableA("USERNAME", buffer, 256)
            uint32_t envVarRVA = addString("USERNAME");
            if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
            asm_.lea_rax_rip_fixup(envVarRVA);
            asm_.mov_rcx_rax();
            asm_.lea_rax_rbp(bufOffset);
            asm_.mov_rdx_rax();
            asm_.mov_r8d_imm32(256);
            asm_.call_mem_rip(pe_.getImportRVA("GetEnvironmentVariableA"));
            if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
            
            // Return pointer to buffer
            asm_.lea_rax_rbp(bufOffset);
            return;
        }
        
        // Handle cpu_count() - get number of processors
        if (id->name == "cpu_count") {
            // Allocate SYSTEM_INFO struct on stack (48 bytes)
            allocLocal("$sysinfo");
            int32_t infoOffset = locals["$sysinfo"];
            for (int i = 0; i < 5; i++) allocLocal("$sysinfo_pad" + std::to_string(i));
            
            // Call GetSystemInfo(&sysinfo)
            if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
            asm_.lea_rax_rbp(infoOffset);
            asm_.mov_rcx_rax();
            asm_.call_mem_rip(pe_.getImportRVA("GetSystemInfo"));
            if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
            
            // dwNumberOfProcessors is at offset 32 in SYSTEM_INFO (after the 8-byte aligned pointers)
            // Read as DWORD (4 bytes) and zero-extend
            asm_.xor_rax_rax();
            // mov eax, dword ptr [rbp + infoOffset + 32]
            asm_.code.push_back(0x8B);
            asm_.code.push_back(0x85);
            int32_t finalOffset = infoOffset + 32;
            asm_.code.push_back(finalOffset & 0xFF);
            asm_.code.push_back((finalOffset >> 8) & 0xFF);
            asm_.code.push_back((finalOffset >> 16) & 0xFF);
            asm_.code.push_back((finalOffset >> 24) & 0xFF);
            return;
        }
        
        // Handle sleep(ms) - sleep for milliseconds
        if (id->name == "sleep" && node.args.size() >= 1) {
            node.args[0]->accept(*this);
            asm_.mov_rcx_rax();
            if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
            asm_.call_mem_rip(pe_.getImportRVA("Sleep"));
            if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
            asm_.xor_rax_rax();
            return;
        }
        
        // Handle now() - returns tick count in seconds (approximate)
        if (id->name == "now") {
            if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
            asm_.call_mem_rip(pe_.getImportRVA("GetTickCount64"));
            if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
            // Convert ms to seconds
            asm_.mov_rcx_imm64(1000);
            asm_.cqo();
            asm_.idiv_rcx();
            return;
        }
        
        // Handle now_ms() - returns tick count in milliseconds
        if (id->name == "now_ms") {
            if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
            asm_.call_mem_rip(pe_.getImportRVA("GetTickCount64"));
            if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
            return;
        }
        
        // Handle year(), month(), day(), hour(), minute(), second()
        if (id->name == "year" || id->name == "month" || id->name == "day" ||
            id->name == "hour" || id->name == "minute" || id->name == "second") {
            // Allocate SYSTEMTIME struct on stack (16 bytes)
            allocLocal("$systime");
            int32_t timeOffset = locals["$systime"];
            allocLocal("$systime_pad");
            
            // Call GetLocalTime(&systime)
            if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
            asm_.lea_rax_rbp(timeOffset);
            asm_.mov_rcx_rax();
            asm_.call_mem_rip(pe_.getImportRVA("GetLocalTime"));
            if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
            
            // SYSTEMTIME layout: wYear(0), wMonth(2), wDayOfWeek(4), wDay(6), wHour(8), wMinute(10), wSecond(12)
            int offset = 0;
            if (id->name == "year") offset = 0;
            else if (id->name == "month") offset = 2;
            else if (id->name == "day") offset = 6;
            else if (id->name == "hour") offset = 8;
            else if (id->name == "minute") offset = 10;
            else if (id->name == "second") offset = 12;
            
            // Load the WORD value: movzx eax, word ptr [rbp + timeOffset + offset]
            asm_.xor_rax_rax();
            asm_.code.push_back(0x0F); asm_.code.push_back(0xB7);
            asm_.code.push_back(0x85);
            int32_t finalOffset = timeOffset + offset;
            asm_.code.push_back(finalOffset & 0xFF);
            asm_.code.push_back((finalOffset >> 8) & 0xFF);
            asm_.code.push_back((finalOffset >> 16) & 0xFF);
            asm_.code.push_back((finalOffset >> 24) & 0xFF);
            return;
        }

        if (id->name == "print" || id->name == "println") {
            for (auto& arg : node.args) {
                // Use the helper to print each argument
                emitPrintExpr(arg.get());
            }
            
            // Print newline using cached handle
            uint32_t nlRVA = addString("\r\n");
            emitWriteConsole(nlRVA, 2);
            
            asm_.xor_rax_rax();
            return;
        }

        
        if (id->name == "str" && node.args.size() == 1) {
            std::string strVal;
            if (tryEvalConstantString(node.args[0].get(), strVal)) {
                uint32_t rva = addString(strVal);
                asm_.lea_rax_rip_fixup(rva);
                return;
            }
            
            node.args[0]->accept(*this);
            emitItoa();
            return;
        }
    }
    
    // OPTIMIZATION: For small number of args, try to load directly into registers
    // instead of push/pop pattern
    bool canOptimizeArgs = node.args.size() <= 4;
    
    if (canOptimizeArgs) {
        // Check if all args are simple (constants or variables)
        std::vector<int64_t> constArgs(4, 0);
        std::vector<bool> isConst(4, false);
        std::vector<Identifier*> varArgs(4, nullptr);
        std::vector<bool> isGlobalReg(4, false);  // Track if var is in global register
        
        for (size_t i = 0; i < node.args.size(); ++i) {
            int64_t val;
            if (tryEvalConstant(node.args[i].get(), val)) {
                constArgs[i] = val;
                isConst[i] = true;
            } else if (auto* ident = dynamic_cast<Identifier*>(node.args[i].get())) {
                // Check if variable is in locals (stack)
                if (locals.count(ident->name)) {
                    varArgs[i] = ident;
                }
                // Check if variable is in a global register
                else if (globalVarRegisters_.count(ident->name) && 
                         globalVarRegisters_[ident->name] != VarRegister::NONE) {
                    varArgs[i] = ident;
                    isGlobalReg[i] = true;
                }
                // Check if variable is in a function-local register
                else if (varRegisters_.count(ident->name) && 
                         varRegisters_[ident->name] != VarRegister::NONE) {
                    varArgs[i] = ident;
                    isGlobalReg[i] = true;  // Treat same as global reg for loading
                }
                else {
                    canOptimizeArgs = false;
                    break;
                }
            } else {
                canOptimizeArgs = false;
                break;
            }
        }
        
        if (canOptimizeArgs) {
            // Load arguments directly into registers
            for (size_t i = 0; i < node.args.size(); ++i) {
                if (isConst[i]) {
                    int64_t val = constArgs[i];
                    bool isSmall = val >= 0 && val <= 0x7FFFFFFF;
                    
                    if (i == 0) {
                        if (val == 0) {
                            asm_.xor_ecx_ecx();
                        } else if (isSmall) {
                            // mov ecx, imm32
                            asm_.code.push_back(0xB9);
                            asm_.code.push_back(val & 0xFF);
                            asm_.code.push_back((val >> 8) & 0xFF);
                            asm_.code.push_back((val >> 16) & 0xFF);
                            asm_.code.push_back((val >> 24) & 0xFF);
                        } else {
                            asm_.mov_rcx_imm64(val);
                        }
                    } else if (i == 1) {
                        if (val == 0) {
                            // xor edx, edx
                            asm_.code.push_back(0x31); asm_.code.push_back(0xD2);
                        } else if (isSmall) {
                            // mov edx, imm32
                            asm_.code.push_back(0xBA);
                            asm_.code.push_back(val & 0xFF);
                            asm_.code.push_back((val >> 8) & 0xFF);
                            asm_.code.push_back((val >> 16) & 0xFF);
                            asm_.code.push_back((val >> 24) & 0xFF);
                        } else {
                            asm_.mov_rdx_imm64(val);
                        }
                    } else if (i == 2) {
                        if (isSmall) {
                            // mov r8d, imm32
                            asm_.code.push_back(0x41); asm_.code.push_back(0xB8);
                            asm_.code.push_back(val & 0xFF);
                            asm_.code.push_back((val >> 8) & 0xFF);
                            asm_.code.push_back((val >> 16) & 0xFF);
                            asm_.code.push_back((val >> 24) & 0xFF);
                        } else {
                            asm_.mov_r8_imm64(val);
                        }
                    } else if (i == 3) {
                        if (isSmall) {
                            // mov r9d, imm32
                            asm_.code.push_back(0x41); asm_.code.push_back(0xB9);
                            asm_.code.push_back(val & 0xFF);
                            asm_.code.push_back((val >> 8) & 0xFF);
                            asm_.code.push_back((val >> 16) & 0xFF);
                            asm_.code.push_back((val >> 24) & 0xFF);
                        } else {
                            // mov r9, imm64
                            asm_.code.push_back(0x49); asm_.code.push_back(0xB9);
                            for (int k = 0; k < 8; ++k) {
                                asm_.code.push_back((val >> (k * 8)) & 0xFF);
                            }
                        }
                    }
                } else if (varArgs[i]) {
                    if (isGlobalReg[i]) {
                        // Variable is in a register - need to load to arg register
                        VarRegister srcReg = VarRegister::NONE;
                        if (globalVarRegisters_.count(varArgs[i]->name)) {
                            srcReg = globalVarRegisters_[varArgs[i]->name];
                        } else if (varRegisters_.count(varArgs[i]->name)) {
                            srcReg = varRegisters_[varArgs[i]->name];
                        }
                        
                        // First load to rax, then move to target register
                        switch (srcReg) {
                            case VarRegister::RBX: asm_.mov_rax_rbx(); break;
                            case VarRegister::R12: asm_.mov_rax_r12(); break;
                            case VarRegister::R13: asm_.mov_rax_r13(); break;
                            case VarRegister::R14: asm_.mov_rax_r14(); break;
                            case VarRegister::R15: asm_.mov_rax_r15(); break;
                            default: break;
                        }
                        
                        // Move rax to target arg register
                        if (i == 0) {
                            asm_.mov_rcx_rax();
                        } else if (i == 1) {
                            asm_.mov_rdx_rax();
                        } else if (i == 2) {
                            asm_.mov_r8_rax();
                        } else if (i == 3) {
                            // mov r9, rax
                            asm_.code.push_back(0x49); asm_.code.push_back(0x89); asm_.code.push_back(0xC1);
                        }
                    } else {
                        // Variable is on stack
                        int32_t offset = locals[varArgs[i]->name];
                        if (i == 0) {
                            asm_.mov_rcx_mem_rbp(offset);
                        } else if (i == 1) {
                            asm_.mov_rdx_mem_rbp(offset);
                        } else if (i == 2) {
                            // mov r8, [rbp+offset]
                            asm_.code.push_back(0x4C); asm_.code.push_back(0x8B);
                            asm_.code.push_back(0x85);
                            asm_.code.push_back(offset & 0xFF);
                            asm_.code.push_back((offset >> 8) & 0xFF);
                            asm_.code.push_back((offset >> 16) & 0xFF);
                            asm_.code.push_back((offset >> 24) & 0xFF);
                        } else if (i == 3) {
                            // mov r9, [rbp+offset]
                            asm_.code.push_back(0x4C); asm_.code.push_back(0x8B);
                            asm_.code.push_back(0x8D);
                            asm_.code.push_back(offset & 0xFF);
                            asm_.code.push_back((offset >> 8) & 0xFF);
                            asm_.code.push_back((offset >> 16) & 0xFF);
                            asm_.code.push_back((offset >> 24) & 0xFF);
                        }
                    }
                }
            }
            
            node.callee->accept(*this);
            if (!stackAllocated_) asm_.sub_rsp_imm32(0x20);
            asm_.call_rax();
            if (!stackAllocated_) asm_.add_rsp_imm32(0x20);
            return;
        }
    }
    
    // Standard function call
    if (auto* id = dynamic_cast<Identifier*>(node.callee.get())) {
        if (asm_.labels.count(id->name)) {
            // Function call via label (direct)
            for (int i = (int)node.args.size() - 1; i >= 0; i--) {
                node.args[i]->accept(*this);
                asm_.push_rax();
            }
            
            if (node.args.size() >= 1) asm_.pop_rcx();
            if (node.args.size() >= 2) asm_.pop_rdx();
            if (node.args.size() >= 3) {
                asm_.code.push_back(0x41); asm_.code.push_back(0x58);  // pop r8
            }
            if (node.args.size() >= 4) {
                asm_.code.push_back(0x41); asm_.code.push_back(0x59);  // pop r9
            }
            
            if (!stackAllocated_) asm_.sub_rsp_imm32(0x20);
            asm_.call_rel32(id->name);
            if (!stackAllocated_) asm_.add_rsp_imm32(0x20);
            return;
        }
    }
    
    // Fallback: use push/pop for complex expressions
    for (int i = (int)node.args.size() - 1; i >= 0; i--) {
        node.args[i]->accept(*this);
        asm_.push_rax();
    }
    
    // Pop arguments into registers (Windows x64: rcx, rdx, r8, r9)
    if (node.args.size() >= 1) asm_.pop_rcx();
    if (node.args.size() >= 2) asm_.pop_rdx();
    if (node.args.size() >= 3) {
        // pop r8
        asm_.code.push_back(0x41); asm_.code.push_back(0x58);
    }
    if (node.args.size() >= 4) {
        // pop r9
        asm_.code.push_back(0x41); asm_.code.push_back(0x59);
    }
    
    node.callee->accept(*this);
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x20);
    asm_.call_rax();
    if (!stackAllocated_) asm_.add_rsp_imm32(0x20);
}

} // namespace flex
