// Tyl Compiler - Extended System Builtins for Native Code Generation
// Additional system functions ported from stdlib/system/system.cpp

#include "backend/codegen/codegen_base.h"

namespace tyl {

// env(name) -> str - Get environment variable
void NativeCodeGen::emitSystemEnv(CallExpr& node) {
    allocLocal("$env_buf");
    int32_t bufOffset = locals["$env_buf"];
    for (int i = 0; i < 127; i++) allocLocal("$env_pad" + std::to_string(i));
    
    node.args[0]->accept(*this);
    asm_.mov_rcx_rax();
    asm_.lea_rdx_rbp_offset(bufOffset);
    asm_.mov_r8_imm64(1024);
    
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("GetEnvironmentVariableA"));
    if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
    
    // If failed, return empty string
    asm_.test_rax_rax();
    std::string emptyLabel = newLabel("env_empty");
    std::string doneLabel = newLabel("env_done");
    
    asm_.jz_rel32(emptyLabel);
    asm_.lea_rax_rbp(bufOffset);
    asm_.jmp_rel32(doneLabel);
    
    asm_.label(emptyLabel);
    uint32_t emptyRva = addString("");
    asm_.lea_rax_rip_fixup(emptyRva);
    
    asm_.label(doneLabel);
}

// set_env(name, value) -> bool - Set environment variable
void NativeCodeGen::emitSystemSetEnv(CallExpr& node) {
    node.args[0]->accept(*this);
    asm_.push_rax();
    node.args[1]->accept(*this);
    asm_.mov_rdx_rax();
    asm_.pop_rcx();
    
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("SetEnvironmentVariableA"));
    if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
    
    // Return 1 on success, 0 on failure
    asm_.test_rax_rax();
    asm_.setne_al();
    asm_.movzx_rax_al();
}

// home_dir() -> str - Get user home directory
void NativeCodeGen::emitSystemHomeDir(CallExpr& node) {
    (void)node;
    allocLocal("$home_buf");
    int32_t bufOffset = locals["$home_buf"];
    for (int i = 0; i < 63; i++) allocLocal("$home_pad" + std::to_string(i));
    
    // Get USERPROFILE environment variable
    uint32_t varRva = addString("USERPROFILE");
    asm_.lea_rcx_rip_fixup(varRva);
    asm_.lea_rdx_rbp_offset(bufOffset);
    asm_.mov_r8_imm64(512);
    
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("GetEnvironmentVariableA"));
    if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
    
    asm_.test_rax_rax();
    std::string emptyLabel = newLabel("home_empty");
    std::string doneLabel = newLabel("home_done");
    
    asm_.jz_rel32(emptyLabel);
    asm_.lea_rax_rbp(bufOffset);
    asm_.jmp_rel32(doneLabel);
    
    asm_.label(emptyLabel);
    uint32_t emptyRva = addString("");
    asm_.lea_rax_rip_fixup(emptyRva);
    
    asm_.label(doneLabel);
}

// temp_dir() -> str - Get temp directory
void NativeCodeGen::emitSystemTempDir(CallExpr& node) {
    (void)node;
    allocLocal("$temp_buf");
    int32_t bufOffset = locals["$temp_buf"];
    for (int i = 0; i < 63; i++) allocLocal("$temp_pad" + std::to_string(i));
    
    asm_.mov_ecx_imm32(512);
    asm_.lea_rdx_rbp_offset(bufOffset);
    
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("GetTempPathA"));
    if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
    
    asm_.lea_rax_rbp(bufOffset);
}

// assert(condition, message?) -> nil - Assert condition is true
void NativeCodeGen::emitSystemAssert(CallExpr& node) {
    node.args[0]->accept(*this);
    asm_.test_rax_rax();
    
    std::string passLabel = newLabel("assert_pass");
    
    asm_.jnz_rel32(passLabel);
    
    // Assertion failed - print message and exit
    if (node.args.size() > 1) {
        uint32_t prefixRva = addString("Assertion failed: ");
        emitWriteConsole(prefixRva, 18);
        
        std::string msg;
        if (tryEvalConstantString(node.args[1].get(), msg)) {
            uint32_t msgRva = addString(msg);
            emitWriteConsole(msgRva, (uint32_t)msg.size());
        }
    } else {
        uint32_t msgRva = addString("Assertion failed!");
        emitWriteConsole(msgRva, 17);
    }
    
    uint32_t nlRva = addString("\r\n");
    emitWriteConsole(nlRva, 2);
    
    // Exit with code 1
    asm_.mov_ecx_imm32(1);
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("ExitProcess"));
    if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
    
    asm_.label(passLabel);
    asm_.xor_rax_rax();
}

// panic(message) -> nil - Exit with error message
void NativeCodeGen::emitSystemPanic(CallExpr& node) {
    uint32_t prefixRva = addString("Panic: ");
    emitWriteConsole(prefixRva, 7);
    
    std::string msg;
    if (tryEvalConstantString(node.args[0].get(), msg)) {
        uint32_t msgRva = addString(msg);
        emitWriteConsole(msgRva, (uint32_t)msg.size());
    } else {
        node.args[0]->accept(*this);
        // Would need to print runtime string here
    }
    
    uint32_t nlRva = addString("\r\n");
    emitWriteConsole(nlRva, 2);
    
    asm_.mov_ecx_imm32(1);
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("ExitProcess"));
    if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
}

// debug(value) -> value - Print debug info and return value
void NativeCodeGen::emitSystemDebug(CallExpr& node) {
    uint32_t prefixRva = addString("[debug] ");
    emitWriteConsole(prefixRva, 8);
    
    node.args[0]->accept(*this);
    asm_.push_rax();
    
    // Print the value (simplified - just print as int)
    emitPrintExpr(node.args[0].get());
    
    uint32_t nlRva = addString("\r\n");
    emitWriteConsole(nlRva, 2);
    
    asm_.pop_rax();
}

// system(command) -> int - Execute command, return exit code
void NativeCodeGen::emitSystemCommand(CallExpr& node) {
    // Use CreateProcessA to execute command
    // Simplified - return 0 for now
    (void)node;
    asm_.xor_rax_rax();
}

} // namespace tyl
