// Tyl Compiler - Native Code Generator System Builtin Calls
// Handles: exit, sleep, platform, arch, hostname, username, cpu_count, time functions

#include "backend/codegen/codegen_base.h"

namespace tyl {

// System builtin implementations extracted from codegen_call_core.cpp

void NativeCodeGen::emitSystemExit(CallExpr& node) {
    if (node.args.empty()) {
        asm_.xor_ecx_ecx();
    } else {
        int64_t exitCode;
        if (tryEvalConstant(node.args[0].get(), exitCode)) {
            asm_.mov_ecx_imm32(static_cast<int32_t>(exitCode));
        } else {
            node.args[0]->accept(*this);
            asm_.mov_rcx_rax();
        }
    }
    
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("ExitProcess"));
    if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
}

void NativeCodeGen::emitSystemSleep(CallExpr& node) {
    int64_t ms;
    if (tryEvalConstant(node.args[0].get(), ms)) {
        asm_.mov_ecx_imm32(static_cast<int32_t>(ms));
    } else {
        node.args[0]->accept(*this);
        asm_.mov_rcx_rax();
    }
    
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("Sleep"));
    if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
    
    asm_.xor_rax_rax();
}

void NativeCodeGen::emitSystemPlatform(CallExpr& node) {
    (void)node;
    uint32_t rva = addString("windows");
    asm_.lea_rax_rip_fixup(rva);
}

void NativeCodeGen::emitSystemArch(CallExpr& node) {
    (void)node;
    uint32_t rva = addString("x64");
    asm_.lea_rax_rip_fixup(rva);
}

void NativeCodeGen::emitSystemHostname(CallExpr& node) {
    (void)node;
    // GetComputerNameA(buffer, &size)
    allocLocal("$hostname_buf");
    int32_t bufOffset = locals["$hostname_buf"];
    for (int i = 0; i < 31; i++) allocLocal("$hostname_pad" + std::to_string(i));
    
    allocLocal("$hostname_size");
    asm_.mov_rax_imm64(256);
    asm_.mov_mem_rbp_rax(locals["$hostname_size"]);
    
    asm_.lea_rcx_rbp(bufOffset);
    asm_.lea_rdx_rbp_offset(locals["$hostname_size"]);
    
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("GetComputerNameA"));
    if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
    
    asm_.lea_rax_rbp(bufOffset);
}

void NativeCodeGen::emitSystemUsername(CallExpr& node) {
    (void)node;
    // GetUserNameA(buffer, &size)
    allocLocal("$username_buf");
    int32_t bufOffset = locals["$username_buf"];
    for (int i = 0; i < 31; i++) allocLocal("$username_pad" + std::to_string(i));
    
    allocLocal("$username_size");
    asm_.mov_rax_imm64(256);
    asm_.mov_mem_rbp_rax(locals["$username_size"]);
    
    asm_.lea_rcx_rbp(bufOffset);
    asm_.lea_rdx_rbp_offset(locals["$username_size"]);
    
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("GetUserNameA"));
    if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
    
    asm_.lea_rax_rbp(bufOffset);
}

void NativeCodeGen::emitSystemCpuCount(CallExpr& node) {
    (void)node;
    // GetSystemInfo and extract dwNumberOfProcessors
    allocLocal("$sysinfo");
    // SYSTEM_INFO is 48 bytes
    for (int i = 0; i < 5; i++) allocLocal("$sysinfo_pad" + std::to_string(i));
    
    asm_.lea_rcx_rbp(locals["$sysinfo"]);
    
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("GetSystemInfo"));
    if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
    
    // dwNumberOfProcessors is at offset 32 in SYSTEM_INFO
    asm_.mov_rax_mem_rbp(locals["$sysinfo"]);
    asm_.code.push_back(0x48); asm_.code.push_back(0x8B);
    asm_.code.push_back(0x85);
    int32_t offset = locals["$sysinfo"] + 32;
    asm_.code.push_back(offset & 0xFF);
    asm_.code.push_back((offset >> 8) & 0xFF);
    asm_.code.push_back((offset >> 16) & 0xFF);
    asm_.code.push_back((offset >> 24) & 0xFF);
}

void NativeCodeGen::emitTimeNow(CallExpr& node) {
    (void)node;
    // GetSystemTimeAsFileTime and convert to seconds since epoch
    allocLocal("$filetime");
    allocLocal("$filetime_high");
    
    asm_.lea_rcx_rbp(locals["$filetime"]);
    
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("GetSystemTimeAsFileTime"));
    if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
    
    // FILETIME is 100-nanosecond intervals since Jan 1, 1601
    // Convert to seconds since Unix epoch (Jan 1, 1970)
    asm_.mov_rax_mem_rbp(locals["$filetime"]);
    // Subtract 116444736000000000 (difference between 1601 and 1970 in 100ns)
    // Then divide by 10000000 to get seconds
    asm_.mov_rcx_imm64(116444736000000000LL);
    asm_.sub_rax_rcx();
    asm_.mov_rcx_imm64(10000000);
    asm_.cqo();
    asm_.idiv_rcx();
}

void NativeCodeGen::emitTimeNowMs(CallExpr& node) {
    (void)node;
    // GetSystemTimeAsFileTime and convert to milliseconds since epoch
    allocLocal("$filetime_ms");
    allocLocal("$filetime_ms_high");
    
    asm_.lea_rcx_rbp(locals["$filetime_ms"]);
    
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("GetSystemTimeAsFileTime"));
    if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
    
    asm_.mov_rax_mem_rbp(locals["$filetime_ms"]);
    asm_.mov_rcx_imm64(116444736000000000LL);
    asm_.sub_rax_rcx();
    asm_.mov_rcx_imm64(10000);  // Convert to milliseconds
    asm_.cqo();
    asm_.idiv_rcx();
}

void NativeCodeGen::emitTimeYear(CallExpr& node) {
    (void)node;
    emitGetLocalTimeField(0);  // wYear is at offset 0
}

void NativeCodeGen::emitTimeMonth(CallExpr& node) {
    (void)node;
    emitGetLocalTimeField(2);  // wMonth is at offset 2
}

void NativeCodeGen::emitTimeDay(CallExpr& node) {
    (void)node;
    emitGetLocalTimeField(6);  // wDay is at offset 6
}

void NativeCodeGen::emitTimeHour(CallExpr& node) {
    (void)node;
    emitGetLocalTimeField(8);  // wHour is at offset 8
}

void NativeCodeGen::emitTimeMinute(CallExpr& node) {
    (void)node;
    emitGetLocalTimeField(10);  // wMinute is at offset 10
}

void NativeCodeGen::emitTimeSecond(CallExpr& node) {
    (void)node;
    emitGetLocalTimeField(12);  // wSecond is at offset 12
}

void NativeCodeGen::emitGetLocalTimeField(int32_t fieldOffset) {
    // SYSTEMTIME structure - use unique names per call
    std::string systimeName = "$systime_" + std::to_string(labelCounter++);
    std::string padName = systimeName + "_pad";
    
    allocLocal(systimeName);
    // SYSTEMTIME is 16 bytes (8 WORDs)
    allocLocal(padName);
    
    asm_.lea_rcx_rbp(locals[systimeName]);
    
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("GetLocalTime"));
    if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
    
    // Load the WORD field and zero-extend
    int32_t offset = locals[systimeName] + fieldOffset;
    asm_.code.push_back(0x48);  // REX.W
    asm_.code.push_back(0x0F);  // movzx
    asm_.code.push_back(0xB7);  // movzx rax, word [rbp+offset]
    asm_.code.push_back(0x85);
    asm_.code.push_back(offset & 0xFF);
    asm_.code.push_back((offset >> 8) & 0xFF);
    asm_.code.push_back((offset >> 16) & 0xFF);
    asm_.code.push_back((offset >> 24) & 0xFF);
}

} // namespace tyl
