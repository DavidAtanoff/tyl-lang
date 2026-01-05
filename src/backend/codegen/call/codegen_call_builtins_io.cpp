// Tyl Compiler - Native Code Generator I/O Builtin Calls
// Handles: print, println, open, read, write, close, file_size

#include "backend/codegen/codegen_base.h"

namespace tyl {

// I/O builtin implementations - uses emitPrintExpr for consistency with original

void NativeCodeGen::emitPrint(CallExpr& node, bool newline) {
    (void)newline; // Always prints newline at end
    
    for (auto& arg : node.args) {
        emitPrintExpr(arg.get());
    }
    
    uint32_t nlRVA = addString("\r\n");
    emitWriteConsole(nlRVA, 2);
    
    asm_.xor_rax_rax();
}

void NativeCodeGen::emitFileOpen(CallExpr& node) {
    // open(filename, mode) -> handle (-1 on error)
    // Uses CreateFileA from kernel32.dll
    // mode: "r" = read, "w" = write (create/truncate), "a" = append
    
    // Evaluate filename
    node.args[0]->accept(*this);
    asm_.push_rax();  // Save filename
    
    // Determine access mode and creation disposition
    // Default to read mode
    int64_t desiredAccess = 0x80000000;  // GENERIC_READ
    int64_t creationDisp = 3;  // OPEN_EXISTING
    
    std::string modeStr = "r";
    if (node.args.size() > 1) {
        if (tryEvalConstantString(node.args[1].get(), modeStr)) {
            // Use constant mode
        } else {
            // Runtime mode - for simplicity, default to read
            modeStr = "r";
        }
    }
    
    if (modeStr == "w") {
        desiredAccess = 0x40000000;  // GENERIC_WRITE
        creationDisp = 2;  // CREATE_ALWAYS
    } else if (modeStr == "a") {
        desiredAccess = 0x00000004;  // FILE_APPEND_DATA - writes always go to end
        creationDisp = 4;  // OPEN_ALWAYS
    } else if (modeStr == "rw" || modeStr == "r+") {
        desiredAccess = 0x80000000 | 0x40000000;  // GENERIC_READ | GENERIC_WRITE
        creationDisp = 3;  // OPEN_EXISTING
    }
    
    // CreateFileA(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
    //             dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile)
    asm_.pop_rcx();  // rcx = filename
    asm_.mov_rdx_imm64(desiredAccess);  // rdx = desired access
    asm_.code.push_back(0x41); asm_.code.push_back(0xB8);  // mov r8d, 3 (FILE_SHARE_READ | FILE_SHARE_WRITE)
    asm_.code.push_back(0x03); asm_.code.push_back(0x00); asm_.code.push_back(0x00); asm_.code.push_back(0x00);
    asm_.code.push_back(0x45); asm_.code.push_back(0x31); asm_.code.push_back(0xC9);  // xor r9d, r9d (NULL security)
    
    // Push remaining args on stack (5th, 6th, 7th params)
    // Need 0x20 shadow space + 3*8 = 0x38 bytes, but must be 16-byte aligned
    // Use 0x40 (64 bytes) to ensure alignment
    asm_.sub_rsp_imm32(0x40);  // Shadow space + 3 params (aligned)
    
    // [rsp+0x20] = creation disposition
    asm_.code.push_back(0x48); asm_.code.push_back(0xC7); asm_.code.push_back(0x44);
    asm_.code.push_back(0x24); asm_.code.push_back(0x20);
    asm_.code.push_back(creationDisp & 0xFF); asm_.code.push_back(0x00); asm_.code.push_back(0x00); asm_.code.push_back(0x00);
    
    // [rsp+0x28] = FILE_ATTRIBUTE_NORMAL (0x80)
    asm_.code.push_back(0x48); asm_.code.push_back(0xC7); asm_.code.push_back(0x44);
    asm_.code.push_back(0x24); asm_.code.push_back(0x28);
    asm_.code.push_back(0x80); asm_.code.push_back(0x00); asm_.code.push_back(0x00); asm_.code.push_back(0x00);
    
    // [rsp+0x30] = NULL (template file)
    asm_.code.push_back(0x48); asm_.code.push_back(0xC7); asm_.code.push_back(0x44);
    asm_.code.push_back(0x24); asm_.code.push_back(0x30);
    asm_.code.push_back(0x00); asm_.code.push_back(0x00); asm_.code.push_back(0x00); asm_.code.push_back(0x00);
    
    asm_.call_mem_rip(pe_.getImportRVA("CreateFileA"));
    asm_.add_rsp_imm32(0x40);
    
    // Check for INVALID_HANDLE_VALUE (-1)
    // If failed, rax is already -1, which is what we want to return
}

void NativeCodeGen::emitFileRead(CallExpr& node) {
    // read(handle, size) -> string
    // Uses ReadFile from kernel32.dll
    
    // We need a contiguous buffer for reading. Since allocLocal allocates 8 bytes at a time
    // going to more negative offsets, we'll allocate the bytesRead first, then reserve
    // space for the buffer by adjusting stackOffset directly.
    
    // Allocate space for bytesRead variable first
    allocLocal("$bytes_read");
    int32_t bytesReadOffset = locals["$bytes_read"];
    
    // Reserve 1024 bytes for buffer by adjusting stackOffset
    // The buffer will be at [rbp + bufOffset] through [rbp + bufOffset + 1023]
    stackOffset -= 1024;
    int32_t bufOffset = stackOffset;
    
    // Evaluate handle
    node.args[0]->accept(*this);
    asm_.push_rax();  // Save handle
    
    // Evaluate size
    node.args[1]->accept(*this);
    asm_.push_rax();  // Save size
    
    // Cap size to buffer size (1024 bytes max)
    // cmp rax, 1024
    asm_.code.push_back(0x48); asm_.code.push_back(0x3D);
    asm_.code.push_back(0x00); asm_.code.push_back(0x04); asm_.code.push_back(0x00); asm_.code.push_back(0x00);
    std::string sizeOk = newLabel("size_ok");
    asm_.jle_rel32(sizeOk);
    // mov rax, 1024
    asm_.mov_rax_imm64(1024);
    asm_.label(sizeOk);
    asm_.mov_r8_rax();  // r8 = size (capped)
    
    asm_.pop_rax();  // discard original size (we use capped)
    asm_.pop_rcx();  // rcx = handle
    
    // ReadFile(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped)
    asm_.lea_rax_rbp(bufOffset);
    asm_.mov_rdx_rax();  // rdx = buffer
    
    asm_.lea_rax_rbp(bytesReadOffset);
    asm_.code.push_back(0x49); asm_.code.push_back(0x89); asm_.code.push_back(0xC1);  // mov r9, rax
    
    asm_.sub_rsp_imm32(0x30);  // 0x20 shadow + 0x8 param + 0x8 alignment
    // [rsp+0x20] = NULL (lpOverlapped)
    asm_.code.push_back(0x48); asm_.code.push_back(0xC7); asm_.code.push_back(0x44);
    asm_.code.push_back(0x24); asm_.code.push_back(0x20);
    asm_.code.push_back(0x00); asm_.code.push_back(0x00); asm_.code.push_back(0x00); asm_.code.push_back(0x00);
    
    asm_.call_mem_rip(pe_.getImportRVA("ReadFile"));
    asm_.add_rsp_imm32(0x30);
    
    // Null-terminate the buffer
    asm_.mov_rax_mem_rbp(bytesReadOffset);
    asm_.lea_rcx_rbp(bufOffset);
    asm_.code.push_back(0x48); asm_.code.push_back(0x01); asm_.code.push_back(0xC1);  // add rcx, rax
    asm_.code.push_back(0xC6); asm_.code.push_back(0x01); asm_.code.push_back(0x00);  // mov byte [rcx], 0
    
    // Return buffer pointer
    asm_.lea_rax_rbp(bufOffset);
}

void NativeCodeGen::emitFileWrite(CallExpr& node) {
    // write(handle, data) -> bytes_written
    // Uses WriteFile from kernel32.dll
    
    // Evaluate handle
    node.args[0]->accept(*this);
    asm_.push_rax();  // Save handle
    
    // Evaluate data (string)
    node.args[1]->accept(*this);
    asm_.push_rax();  // Save data pointer
    
    // Calculate string length
    asm_.mov_rcx_rax();
    asm_.xor_rax_rax();
    std::string lenLoop = newLabel("write_len");
    std::string lenDone = newLabel("write_len_done");
    
    asm_.label(lenLoop);
    asm_.code.push_back(0x80); asm_.code.push_back(0x39); asm_.code.push_back(0x00);  // cmp byte [rcx], 0
    asm_.jz_rel32(lenDone);
    asm_.inc_rax();
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF); asm_.code.push_back(0xC1);  // inc rcx
    asm_.jmp_rel32(lenLoop);
    asm_.label(lenDone);
    
    asm_.push_rax();  // Save length
    
    // Allocate space for bytesWritten
    allocLocal("$bytes_written");
    int32_t bytesWrittenOffset = locals["$bytes_written"];
    
    // WriteFile(hFile, lpBuffer, nNumberOfBytesToWrite, lpNumberOfBytesWritten, lpOverlapped)
    asm_.pop_r8();  // r8 = length
    asm_.pop_rdx();  // rdx = buffer
    asm_.pop_rcx();  // rcx = handle
    
    asm_.lea_rax_rbp(bytesWrittenOffset);
    asm_.code.push_back(0x49); asm_.code.push_back(0x89); asm_.code.push_back(0xC1);  // mov r9, rax
    
    asm_.sub_rsp_imm32(0x30);  // 0x20 shadow + 0x8 param + 0x8 alignment
    // [rsp+0x20] = NULL (lpOverlapped)
    asm_.code.push_back(0x48); asm_.code.push_back(0xC7); asm_.code.push_back(0x44);
    asm_.code.push_back(0x24); asm_.code.push_back(0x20);
    asm_.code.push_back(0x00); asm_.code.push_back(0x00); asm_.code.push_back(0x00); asm_.code.push_back(0x00);
    
    asm_.call_mem_rip(pe_.getImportRVA("WriteFile"));
    asm_.add_rsp_imm32(0x30);
    
    // Return bytes written
    asm_.mov_rax_mem_rbp(bytesWrittenOffset);
}

void NativeCodeGen::emitFileClose(CallExpr& node) {
    // close(handle) -> success (1 or 0)
    // Uses CloseHandle from kernel32.dll
    
    node.args[0]->accept(*this);
    asm_.mov_rcx_rax();  // rcx = handle
    
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x20);
    asm_.call_mem_rip(pe_.getImportRVA("CloseHandle"));
    if (!stackAllocated_) asm_.add_rsp_imm32(0x20);
    
    // rax = result (non-zero on success)
}

void NativeCodeGen::emitFileSize(CallExpr& node) {
    // file_size(handle) -> size in bytes
    // Uses GetFileSize from kernel32.dll
    
    node.args[0]->accept(*this);
    asm_.mov_rcx_rax();  // rcx = handle
    // xor edx, edx (zero rdx for lpFileSizeHigh = NULL)
    asm_.code.push_back(0x31); asm_.code.push_back(0xD2);
    
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x20);
    asm_.call_mem_rip(pe_.getImportRVA("GetFileSize"));
    if (!stackAllocated_) asm_.add_rsp_imm32(0x20);
    
    // rax = file size (low 32 bits, but sufficient for most files)
}

void NativeCodeGen::emitRead(CallExpr& node) {
    // read() - read a line from stdin
    allocLocal("$read_buf");
    int32_t bufOffset = locals["$read_buf"];
    // Allocate 255 more bytes for buffer
    for (int i = 0; i < 31; i++) allocLocal("$read_pad" + std::to_string(i));
    
    // If there's a prompt argument, print it first
    if (!node.args.empty()) {
        std::string prompt;
        if (tryEvalConstantString(node.args[0].get(), prompt)) {
            uint32_t rva = addString(prompt);
            asm_.lea_rcx_rip_fixup(rva);
            if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
            asm_.call_mem_rip(pe_.getImportRVA("printf"));
            if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
        } else {
            node.args[0]->accept(*this);
            asm_.mov_rcx_rax();
            if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
            asm_.call_mem_rip(pe_.getImportRVA("printf"));
            if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
        }
    }
    
    // fgets(buffer, size, stdin)
    // First get stdin handle
    asm_.lea_rcx_rip_fixup(pe_.getImportRVA("__iob_func"));
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("__iob_func"));
    if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
    // stdin is at offset 0 from __iob_func result
    asm_.mov_r8_rax();
    
    asm_.lea_rcx_rbp(bufOffset);
    asm_.mov_rdx_imm64(255);
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("fgets"));
    if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
    
    // Strip trailing newline
    asm_.lea_rax_rbp(bufOffset);
    asm_.mov_rcx_rax();
    
    std::string stripLoop = newLabel("strip_nl");
    std::string stripDone = newLabel("strip_done");
    
    asm_.label(stripLoop);
    asm_.code.push_back(0x0F); asm_.code.push_back(0xB6); asm_.code.push_back(0x01);
    asm_.test_rax_rax();
    asm_.jz_rel32(stripDone);
    asm_.code.push_back(0x3C); asm_.code.push_back('\n');
    asm_.code.push_back(0x74); asm_.code.push_back(0x06);
    asm_.code.push_back(0x3C); asm_.code.push_back('\r');
    asm_.code.push_back(0x74); asm_.code.push_back(0x02);
    asm_.inc_rcx();
    asm_.jmp_rel32(stripLoop);
    
    // Found newline - null terminate
    asm_.code.push_back(0xC6); asm_.code.push_back(0x01); asm_.code.push_back(0x00);
    
    asm_.label(stripDone);
    asm_.lea_rax_rbp(bufOffset);
}

} // namespace tyl
