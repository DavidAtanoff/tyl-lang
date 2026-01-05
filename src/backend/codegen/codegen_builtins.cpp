// Tyl Compiler - Native Code Generator Built-in Functions
// Handles: print, println, str, itoa, runtime helpers

#include "codegen_base.h"

namespace tyl {

// Check if itoa should be inlined based on optimization level
bool NativeCodeGen::shouldInlineItoa() const {
    // Always inline itoa for now - the shared routine requires
    // emitRuntimeRoutines() to be called, which is not currently integrated
    return true;
}

// Check if ftoa should be inlined based on optimization level
bool NativeCodeGen::shouldInlineFtoa() const {
    // Always inline ftoa for now - the shared routine is complex and requires
    // proper stack frame management that's not set up in emitRuntimeRoutines
    return true;
}

// Call the shared itoa routine instead of inlining
// Input: rax = integer value
// Output: rax = string pointer, rcx = length
void NativeCodeGen::emitItoaCall() {
    if (shouldInlineItoa()) {
        emitItoa();
    } else {
        // Call shared routine
        asm_.call_rel32(itoaRoutineLabel_);
    }
}

// Call the shared ftoa routine instead of inlining
// Input: xmm0 = float value
// Output: rax = string pointer, rcx = length
void NativeCodeGen::emitFtoaCall() {
    if (shouldInlineFtoa()) {
        emitFtoa();
    } else {
        // Call shared routine
        asm_.call_rel32(ftoaRoutineLabel_);
    }
}

// Call the shared print_int routine
// Input: rax = integer value
void NativeCodeGen::emitPrintIntCall() {
    if (shouldInlineItoa()) {
        // Inline the whole thing
        emitItoa();
        asm_.mov_rdx_rax();
        asm_.code.push_back(0x49); asm_.code.push_back(0x89); asm_.code.push_back(0xC8);
        emitWriteConsoleBuffer();
    } else {
        // Ensure stdout handle is cached in RDI before calling shared routine
        if (useStdoutCaching_ && !stdoutHandleCached_) {
            // Save rax (the value to print)
            asm_.push_rax();
            
            // Get stdout handle and cache it in RDI
            asm_.mov_ecx_imm32(-11);  // STD_OUTPUT_HANDLE
            asm_.call_mem_rip(pe_.getImportRVA("GetStdHandle"));
            // mov rdi, rax
            asm_.mov_rdi_rax();
            stdoutHandleCached_ = true;
            
            // Restore rax
            asm_.pop_rax();
        }
        
        // Call shared routine (expects value in rax, stdout handle in rdi)
        asm_.call_rel32(printIntRoutineLabel_);
        asm_.xor_rax_rax();
    }
}

// Emit shared runtime routines at the end of the code section
// These are called by multiple print statements to reduce code size
void NativeCodeGen::emitRuntimeRoutines() {
    if (runtimeRoutinesEmitted_) return;
    // O3/Ofast inline everything, skip shared routines
    if (optLevel_ == CodeGenOptLevel::O3 || optLevel_ == CodeGenOptLevel::Ofast) return;
    
    runtimeRoutinesEmitted_ = true;
    
    // === __TYL_itoa routine ===
    // Input: rax = integer value
    // Output: rax = string pointer, rcx = length
    // Preserves: rdi (stdout handle)
    asm_.label(itoaRoutineLabel_);
    
    // Save callee-saved registers we'll use
    asm_.push_rbx();
    // push r12
    asm_.code.push_back(0x41); asm_.code.push_back(0x54);
    
    // The actual itoa implementation
    // mov r10, rax (save original value)
    asm_.code.push_back(0x49); asm_.code.push_back(0x89); asm_.code.push_back(0xC2);
    
    // lea r8, [rip + buffer + 30] (point to end of buffer)
    asm_.lea_rax_rip_fixup(itoaBufferRVA_ + 30);
    asm_.code.push_back(0x49); asm_.code.push_back(0x89); asm_.code.push_back(0xC0);
    
    // mov byte [r8], 0 (null terminator)
    asm_.code.push_back(0x41); asm_.code.push_back(0xC6); asm_.code.push_back(0x00);
    asm_.code.push_back(0x00);
    
    // mov rax, r10 (restore value)
    asm_.code.push_back(0x4C); asm_.code.push_back(0x89); asm_.code.push_back(0xD0);
    
    // xor r9d, r9d (negative flag)
    asm_.code.push_back(0x45); asm_.code.push_back(0x31); asm_.code.push_back(0xC9);
    
    // test rax, rax
    asm_.test_rax_rax();
    // jns positive
    asm_.code.push_back(0x0F); asm_.code.push_back(0x89);
    size_t jnsPatch = asm_.code.size();
    asm_.code.push_back(0x00); asm_.code.push_back(0x00);
    asm_.code.push_back(0x00); asm_.code.push_back(0x00);
    
    // neg rax
    asm_.neg_rax();
    // mov r9d, 1 (set negative flag)
    asm_.code.push_back(0x41); asm_.code.push_back(0xB9);
    asm_.code.push_back(0x01); asm_.code.push_back(0x00);
    asm_.code.push_back(0x00); asm_.code.push_back(0x00);
    
    // Patch jns
    int32_t jnsOffset = (int32_t)(asm_.code.size() - jnsPatch - 4);
    asm_.code[jnsPatch] = jnsOffset & 0xFF;
    asm_.code[jnsPatch + 1] = (jnsOffset >> 8) & 0xFF;
    asm_.code[jnsPatch + 2] = (jnsOffset >> 16) & 0xFF;
    asm_.code[jnsPatch + 3] = (jnsOffset >> 24) & 0xFF;
    
    // test rax, rax (check for zero)
    asm_.test_rax_rax();
    // jne loop
    asm_.code.push_back(0x0F); asm_.code.push_back(0x85);
    size_t jnzPatch = asm_.code.size();
    asm_.code.push_back(0x00); asm_.code.push_back(0x00);
    asm_.code.push_back(0x00); asm_.code.push_back(0x00);
    
    // Zero case: write '0'
    // dec r8
    asm_.code.push_back(0x49); asm_.code.push_back(0xFF); asm_.code.push_back(0xC8);
    // mov byte [r8], '0'
    asm_.code.push_back(0x41); asm_.code.push_back(0xC6); asm_.code.push_back(0x00);
    asm_.code.push_back(0x30);
    // jmp done
    asm_.code.push_back(0xE9);
    size_t jmpDonePatch = asm_.code.size();
    asm_.code.push_back(0x00); asm_.code.push_back(0x00);
    asm_.code.push_back(0x00); asm_.code.push_back(0x00);
    
    // Patch jnz to loop
    int32_t jnzOffset = (int32_t)(asm_.code.size() - jnzPatch - 4);
    asm_.code[jnzPatch] = jnzOffset & 0xFF;
    asm_.code[jnzPatch + 1] = (jnzOffset >> 8) & 0xFF;
    asm_.code[jnzPatch + 2] = (jnzOffset >> 16) & 0xFF;
    asm_.code[jnzPatch + 3] = (jnzOffset >> 24) & 0xFF;
    
    // Loop: convert digits
    size_t loopStart = asm_.code.size();
    
    // mov rcx, 10
    asm_.mov_rcx_imm64(10);
    // xor rdx, rdx
    asm_.code.push_back(0x48); asm_.code.push_back(0x31); asm_.code.push_back(0xD2);
    // div rcx
    asm_.code.push_back(0x48); asm_.code.push_back(0xF7); asm_.code.push_back(0xF1);
    
    // add dl, '0'
    asm_.code.push_back(0x80); asm_.code.push_back(0xC2); asm_.code.push_back(0x30);
    
    // dec r8
    asm_.code.push_back(0x49); asm_.code.push_back(0xFF); asm_.code.push_back(0xC8);
    
    // mov byte [r8], dl
    asm_.code.push_back(0x41); asm_.code.push_back(0x88); asm_.code.push_back(0x10);
    
    // test rax, rax
    asm_.test_rax_rax();
    // jne loop
    asm_.code.push_back(0x0F); asm_.code.push_back(0x85);
    int32_t loopOffset = (int32_t)(loopStart - asm_.code.size() - 4);
    asm_.code.push_back(loopOffset & 0xFF);
    asm_.code.push_back((loopOffset >> 8) & 0xFF);
    asm_.code.push_back((loopOffset >> 16) & 0xFF);
    asm_.code.push_back((loopOffset >> 24) & 0xFF);
    
    // Check negative flag
    // test r9d, r9d
    asm_.code.push_back(0x45); asm_.code.push_back(0x85); asm_.code.push_back(0xC9);
    // je done
    asm_.code.push_back(0x74);
    size_t jzPatch = asm_.code.size();
    asm_.code.push_back(0x00);
    
    // Add minus sign
    // dec r8
    asm_.code.push_back(0x49); asm_.code.push_back(0xFF); asm_.code.push_back(0xC8);
    // mov byte [r8], '-'
    asm_.code.push_back(0x41); asm_.code.push_back(0xC6); asm_.code.push_back(0x00);
    asm_.code.push_back(0x2D);
    
    // Patch je
    asm_.code[jzPatch] = (uint8_t)(asm_.code.size() - jzPatch - 1);
    
    // Patch jmp done
    int32_t jmpDoneOffset = (int32_t)(asm_.code.size() - jmpDonePatch - 4);
    asm_.code[jmpDonePatch] = jmpDoneOffset & 0xFF;
    asm_.code[jmpDonePatch + 1] = (jmpDoneOffset >> 8) & 0xFF;
    asm_.code[jmpDonePatch + 2] = (jmpDoneOffset >> 16) & 0xFF;
    asm_.code[jmpDonePatch + 3] = (jmpDoneOffset >> 24) & 0xFF;
    
    // Done: calculate length and return
    // mov rax, r8 (string pointer)
    asm_.code.push_back(0x4C); asm_.code.push_back(0x89); asm_.code.push_back(0xC0);
    
    // lea rcx, [rip + buffer + 30]
    asm_.lea_rcx_rip_fixup(itoaBufferRVA_ + 30);
    // sub rcx, r8 (length = end - start)
    asm_.code.push_back(0x4C); asm_.code.push_back(0x29); asm_.code.push_back(0xC1);
    
    // Restore callee-saved registers
    // pop r12
    asm_.code.push_back(0x41); asm_.code.push_back(0x5C);
    asm_.pop_rbx();
    
    asm_.ret();
    
    // === __TYL_print_int routine ===
    // Input: rax = integer value
    // Uses cached stdout handle in rdi
    // This routine needs its own stack frame for WriteConsoleA
    asm_.label(printIntRoutineLabel_);
    
    // Set up stack frame (0x38 for shadow space + alignment + saved value)
    // Stack must be 16-byte aligned before call, and we need shadow space (0x20)
    // plus space for the 5th parameter at [rsp+0x28]
    asm_.sub_rsp_imm32(0x38);
    
    // Save the input value (we don't actually need it after itoa, but keep for safety)
    // mov [rsp+0x30], rax
    asm_.code.push_back(0x48); asm_.code.push_back(0x89); asm_.code.push_back(0x44);
    asm_.code.push_back(0x24); asm_.code.push_back(0x30);
    
    // Call itoa - input is already in rax
    asm_.call_rel32(itoaRoutineLabel_);
    
    // rax = string pointer, rcx = length
    // Set up WriteConsoleA(hConsole, lpBuffer, nChars, lpCharsWritten, lpReserved)
    //   rcx = handle (from rdi)
    //   rdx = buffer pointer (from rax)
    //   r8  = length (from rcx)
    //   r9  = &written (use stack)
    //   [rsp+0x28] = NULL (reserved)
    
    asm_.mov_rdx_rax();  // rdx = buffer pointer
    // mov r8, rcx (length)
    asm_.code.push_back(0x49); asm_.code.push_back(0x89); asm_.code.push_back(0xC8);
    
    // mov rcx, rdi (cached stdout handle)
    asm_.code.push_back(0x48); asm_.code.push_back(0x89); asm_.code.push_back(0xF9);
    
    // lea r9, [rsp+0x20] (address for bytes written)
    asm_.code.push_back(0x4C); asm_.code.push_back(0x8D); asm_.code.push_back(0x4C);
    asm_.code.push_back(0x24); asm_.code.push_back(0x20);
    
    // mov qword [rsp+0x28], 0 (reserved parameter = NULL)
    asm_.code.push_back(0x48); asm_.code.push_back(0xC7); asm_.code.push_back(0x44);
    asm_.code.push_back(0x24); asm_.code.push_back(0x28);
    asm_.code.push_back(0x00); asm_.code.push_back(0x00); asm_.code.push_back(0x00); asm_.code.push_back(0x00);
    
    // call WriteConsoleA
    asm_.call_mem_rip(pe_.getImportRVA("WriteConsoleA"));
    
    // Clean up stack and return
    asm_.add_rsp_imm32(0x38);
    asm_.ret();
}

void NativeCodeGen::emitPrintInt(int32_t localOffset) {
    uint32_t getStdHandle = pe_.getImportRVA("GetStdHandle");
    uint32_t writeConsoleA = pe_.getImportRVA("WriteConsoleA");
    (void)getStdHandle;
    (void)writeConsoleA;
    asm_.mov_rax_mem_rbp(localOffset);
}

void NativeCodeGen::emitPrintString(uint32_t dataRVA) {
    uint32_t getStdHandle = pe_.getImportRVA("GetStdHandle");
    uint32_t writeConsoleA = pe_.getImportRVA("WriteConsoleA");
    
    asm_.sub_rsp_imm32(0x28);
    asm_.mov_ecx_imm32(-11);
    asm_.call_mem_rip(getStdHandle);
    asm_.mov_mem_rbp_rax(-0x30);
    
    asm_.lea_rcx_rip_fixup(dataRVA);
    asm_.mov_mem_rbp_rcx(-0x38);
    asm_.xor_rax_rax();
    
    std::string lenLoop = newLabel("strlen_loop");
    std::string lenDone = newLabel("strlen_done");
    
    asm_.label(lenLoop);
    asm_.mov_rcx_mem_rbp(-0x38);
    asm_.add_rax_rcx();
    asm_.mov_rax_mem_rax();
    asm_.mov_rcx_imm64(0xFF);
    asm_.and_rax_rcx();
    asm_.test_rax_rax();
    asm_.jz_rel32(lenDone);
    asm_.mov_rax_mem_rbp(-0x40);
    asm_.inc_rax();
    asm_.mov_mem_rbp_rax(-0x40);
    asm_.jmp_rel32(lenLoop);
    asm_.label(lenDone);
    
    asm_.mov_rcx_mem_rbp(-0x30);
    asm_.mov_rdx_mem_rbp(-0x38);
    asm_.mov_rax_mem_rbp(-0x40);
    asm_.mov_r8_rax();
    asm_.lea_rax_rip_fixup(PEGenerator::DATA_RVA);
    asm_.code.push_back(0x49); asm_.code.push_back(0x89); asm_.code.push_back(0xC1);
    asm_.push_rax();
    asm_.xor_rax_rax();
    asm_.mov_mem_rbp_rax(-0x48);
    
    asm_.call_mem_rip(writeConsoleA);
    asm_.add_rsp_imm32(0x28);
}

void NativeCodeGen::emitPrintNewline() {
    uint32_t newlineRVA = addString("\r\n");
    emitPrintString(newlineRVA);
}

void NativeCodeGen::emitPrintRuntimeValue() {
    asm_.push_rax();
    emitItoa();
    
    // rax = string pointer, rcx = length
    asm_.mov_rdx_rax();
    // mov r8, rcx
    asm_.code.push_back(0x49); asm_.code.push_back(0x89); asm_.code.push_back(0xC8);
    emitWriteConsoleBuffer();
}

void NativeCodeGen::emitItoa() {
    std::string negLabel = newLabel("itoa_neg");
    std::string posLabel = newLabel("itoa_pos");
    std::string loopLabel = newLabel("itoa_loop");
    std::string doneLabel = newLabel("itoa_done");
    
    asm_.code.push_back(0x49); asm_.code.push_back(0x89); asm_.code.push_back(0xC2);
    
    asm_.lea_rax_rip_fixup(itoaBufferRVA_ + 30);
    asm_.code.push_back(0x49); asm_.code.push_back(0x89); asm_.code.push_back(0xC0);
    
    asm_.code.push_back(0x41); asm_.code.push_back(0xC6); asm_.code.push_back(0x00);
    asm_.code.push_back(0x00);
    
    asm_.code.push_back(0x4C); asm_.code.push_back(0x89); asm_.code.push_back(0xD0);
    
    asm_.code.push_back(0x45); asm_.code.push_back(0x31); asm_.code.push_back(0xC9);
    
    asm_.test_rax_rax();
    asm_.code.push_back(0x0F); asm_.code.push_back(0x89);
    size_t jnsPatch = asm_.code.size();
    asm_.code.push_back(0x00); asm_.code.push_back(0x00);
    asm_.code.push_back(0x00); asm_.code.push_back(0x00);
    
    asm_.neg_rax();
    asm_.code.push_back(0x41); asm_.code.push_back(0xB9);
    asm_.code.push_back(0x01); asm_.code.push_back(0x00);
    asm_.code.push_back(0x00); asm_.code.push_back(0x00);
    
    int32_t jnsOffset = (int32_t)(asm_.code.size() - jnsPatch - 4);
    asm_.code[jnsPatch] = jnsOffset & 0xFF;
    asm_.code[jnsPatch + 1] = (jnsOffset >> 8) & 0xFF;
    asm_.code[jnsPatch + 2] = (jnsOffset >> 16) & 0xFF;
    asm_.code[jnsPatch + 3] = (jnsOffset >> 24) & 0xFF;
    
    asm_.test_rax_rax();
    asm_.code.push_back(0x0F); asm_.code.push_back(0x85);
    size_t jnzPatch = asm_.code.size();
    asm_.code.push_back(0x00); asm_.code.push_back(0x00);
    asm_.code.push_back(0x00); asm_.code.push_back(0x00);
    
    asm_.code.push_back(0x49); asm_.code.push_back(0xFF); asm_.code.push_back(0xC8);
    asm_.code.push_back(0x41); asm_.code.push_back(0xC6); asm_.code.push_back(0x00);
    asm_.code.push_back(0x30);
    asm_.code.push_back(0xE9);
    size_t jmpDonePatch = asm_.code.size();
    asm_.code.push_back(0x00); asm_.code.push_back(0x00);
    asm_.code.push_back(0x00); asm_.code.push_back(0x00);
    
    int32_t jnzOffset = (int32_t)(asm_.code.size() - jnzPatch - 4);
    asm_.code[jnzPatch] = jnzOffset & 0xFF;
    asm_.code[jnzPatch + 1] = (jnzOffset >> 8) & 0xFF;
    asm_.code[jnzPatch + 2] = (jnzOffset >> 16) & 0xFF;
    asm_.code[jnzPatch + 3] = (jnzOffset >> 24) & 0xFF;
    
    size_t loopStart = asm_.code.size();
    
    asm_.mov_rcx_imm64(10);
    asm_.code.push_back(0x48); asm_.code.push_back(0x31); asm_.code.push_back(0xD2);
    asm_.code.push_back(0x48); asm_.code.push_back(0xF7); asm_.code.push_back(0xF1);
    
    asm_.code.push_back(0x80); asm_.code.push_back(0xC2); asm_.code.push_back(0x30);
    
    asm_.code.push_back(0x49); asm_.code.push_back(0xFF); asm_.code.push_back(0xC8);
    
    asm_.code.push_back(0x41); asm_.code.push_back(0x88); asm_.code.push_back(0x10);
    
    asm_.test_rax_rax();
    asm_.code.push_back(0x0F); asm_.code.push_back(0x85);
    int32_t loopOffset = (int32_t)(loopStart - asm_.code.size() - 4);
    asm_.code.push_back(loopOffset & 0xFF);
    asm_.code.push_back((loopOffset >> 8) & 0xFF);
    asm_.code.push_back((loopOffset >> 16) & 0xFF);
    asm_.code.push_back((loopOffset >> 24) & 0xFF);
    
    asm_.code.push_back(0x45); asm_.code.push_back(0x85); asm_.code.push_back(0xC9);
    asm_.code.push_back(0x74);
    size_t jzPatch = asm_.code.size();
    asm_.code.push_back(0x00);
    
    asm_.code.push_back(0x49); asm_.code.push_back(0xFF); asm_.code.push_back(0xC8);
    asm_.code.push_back(0x41); asm_.code.push_back(0xC6); asm_.code.push_back(0x00);
    asm_.code.push_back(0x2D);
    
    asm_.code[jzPatch] = (uint8_t)(asm_.code.size() - jzPatch - 1);
    
    int32_t jmpDoneOffset = (int32_t)(asm_.code.size() - jmpDonePatch - 4);
    asm_.code[jmpDonePatch] = jmpDoneOffset & 0xFF;
    asm_.code[jmpDonePatch + 1] = (jmpDoneOffset >> 8) & 0xFF;
    asm_.code[jmpDonePatch + 2] = (jmpDoneOffset >> 16) & 0xFF;
    asm_.code[jmpDonePatch + 3] = (jmpDoneOffset >> 24) & 0xFF;
    
    asm_.code.push_back(0x4C); asm_.code.push_back(0x89); asm_.code.push_back(0xC0);
    
    asm_.lea_rcx_rip_fixup(itoaBufferRVA_ + 30);
    asm_.code.push_back(0x4C); asm_.code.push_back(0x29); asm_.code.push_back(0xC1);
}

} // namespace tyl
