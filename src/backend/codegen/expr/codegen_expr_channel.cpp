// Tyl Compiler - Native Code Generator Channel Expressions
// Handles: ChanSendExpr, ChanRecvExpr, MakeChanExpr

#include "backend/codegen/codegen_base.h"

namespace tyl {

// Channel structure layout (allocated on heap):
// Offset 0:  mutex handle (8 bytes) - for synchronization
// Offset 8:  event_not_empty handle (8 bytes) - signaled when data available
// Offset 16: event_not_full handle (8 bytes) - signaled when space available
// Offset 24: buffer pointer (8 bytes) - circular buffer for data
// Offset 32: buffer capacity (8 bytes) - max elements
// Offset 40: element size (8 bytes) - size of each element
// Offset 48: head index (8 bytes) - read position
// Offset 56: tail index (8 bytes) - write position
// Offset 64: count (8 bytes) - current number of elements
// Offset 72: closed flag (8 bytes) - 1 if channel is closed
// Total: 80 bytes for channel header

void NativeCodeGen::emitChannelCreate(size_t bufferSize, int32_t elementSize) {
    // Allocate channel structure (80 bytes) + buffer
    size_t totalSize = 80 + (bufferSize > 0 ? bufferSize * elementSize : elementSize);
    
    // Allocate memory for channel
    asm_.mov_rcx_imm64(totalSize);
    emitGCAllocRaw(totalSize);
    // RAX now contains pointer to channel structure
    
    asm_.push_rax();  // Save channel pointer
    
    // Create mutex - always allocate shadow space for safety
    asm_.xor_rcx_rcx();  // lpMutexAttributes = NULL
    asm_.xor_rdx_rdx();  // bInitialOwner = FALSE
    asm_.xor_r8_r8();    // lpName = NULL
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("CreateMutexA"));
    asm_.add_rsp_imm32(0x28);
    
    // Store mutex handle at offset 0
    asm_.mov_rcx_rax();  // mutex handle
    asm_.mov_rax_mem_rsp(0);  // channel pointer
    asm_.mov_mem_rax_rcx(0);  // store mutex at offset 0
    
    // Create event for "not empty" (manual reset, initially not signaled)
    asm_.xor_rcx_rcx();  // lpEventAttributes = NULL
    asm_.mov_edx_imm32(1);  // bManualReset = TRUE
    asm_.xor_r8_r8();    // bInitialState = FALSE
    asm_.xor_r9_r9();    // lpName = NULL
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("CreateEventA"));
    asm_.add_rsp_imm32(0x28);
    
    // Store event_not_empty handle at offset 8
    asm_.mov_rcx_rax();
    asm_.mov_rax_mem_rsp(0);
    asm_.mov_mem_rax_rcx(8);
    
    // Create event for "not full" (manual reset, initially signaled for buffered)
    asm_.xor_rcx_rcx();
    asm_.mov_edx_imm32(1);  // bManualReset = TRUE
    asm_.mov_r8d_imm32(bufferSize > 0 ? 1 : 0);  // bInitialState
    asm_.xor_r9_r9();
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("CreateEventA"));
    asm_.add_rsp_imm32(0x28);
    
    // Store event_not_full handle at offset 16
    asm_.mov_rcx_rax();
    asm_.mov_rax_mem_rsp(0);
    asm_.mov_mem_rax_rcx(16);
    
    // Set buffer pointer (offset 24) - points to offset 80
    asm_.mov_rax_mem_rsp(0);
    asm_.lea_rcx_rax_offset(80);
    asm_.mov_mem_rax_rcx(24);
    
    // Set buffer capacity (offset 32)
    asm_.mov_rax_mem_rsp(0);
    asm_.mov_rcx_imm64(bufferSize > 0 ? bufferSize : 1);
    asm_.mov_mem_rax_rcx(32);
    
    // Set element size (offset 40)
    asm_.mov_rax_mem_rsp(0);
    asm_.mov_rcx_imm64(elementSize);
    asm_.mov_mem_rax_rcx(40);
    
    // Initialize head, tail, count to 0 (offsets 48, 56, 64)
    asm_.mov_rax_mem_rsp(0);
    asm_.xor_rcx_rcx();
    asm_.mov_mem_rax_rcx(48);
    asm_.mov_mem_rax_rcx(56);
    asm_.mov_mem_rax_rcx(64);
    
    // Initialize closed flag to 0 (offset 72)
    asm_.mov_mem_rax_rcx(72);
    
    // Return channel pointer
    asm_.pop_rax();
}

void NativeCodeGen::emitChannelSend() {
    // Channel pointer in RAX, value to send in RCX
    // Save both - after 2 pushes, stack is 16-byte aligned (assuming it was before)
    asm_.push_rax();  // channel
    asm_.push_rcx();  // value
    
    std::string waitLoop = newLabel("chan_send_wait");
    std::string sendDone = newLabel("chan_send_done");
    
    asm_.label(waitLoop);
    
    // Acquire mutex - always use shadow space since we've pushed values
    asm_.mov_rax_mem_rsp(8);  // channel pointer
    asm_.mov_rcx_mem_rax(0);  // mutex handle
    asm_.mov_rdx_imm64(0xFFFFFFFF);  // INFINITE
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("WaitForSingleObject"));
    asm_.add_rsp_imm32(0x28);
    
    // Check if buffer is full (count >= capacity)
    asm_.mov_rax_mem_rsp(8);  // channel
    asm_.mov_rcx_mem_rax(64);  // count
    asm_.mov_rdx_mem_rax(32);  // capacity
    asm_.cmp_rcx_rdx();
    
    std::string notFull = newLabel("chan_not_full");
    asm_.jl_rel32(notFull);
    
    // Buffer is full - release mutex and wait for not_full event
    asm_.mov_rax_mem_rsp(8);
    asm_.mov_rcx_mem_rax(0);  // mutex
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("ReleaseMutex"));
    asm_.add_rsp_imm32(0x28);
    
    // Wait for not_full event
    asm_.mov_rax_mem_rsp(8);
    asm_.mov_rcx_mem_rax(16);  // event_not_full
    asm_.mov_rdx_imm64(0xFFFFFFFF);
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("WaitForSingleObject"));
    asm_.add_rsp_imm32(0x28);
    
    asm_.jmp_rel32(waitLoop);
    
    asm_.label(notFull);
    
    // Write value to buffer at tail position
    asm_.mov_rax_mem_rsp(8);  // channel
    asm_.mov_rcx_mem_rax(24);  // buffer pointer
    asm_.mov_rdx_mem_rax(56);  // tail index
    asm_.mov_r8_mem_rax(40);   // element size
    
    // Calculate offset: buffer + (tail * element_size)
    asm_.imul_rdx_r8();
    asm_.add_rcx_rdx();  // RCX = buffer + tail * elem_size
    
    // Copy value
    asm_.mov_rax_mem_rsp(0);  // value to send
    asm_.mov_mem_rcx_rax(0);  // store value
    
    // Increment tail: tail = (tail + 1) % capacity
    asm_.mov_rax_mem_rsp(8);  // channel
    asm_.mov_rcx_mem_rax(56);  // tail in RCX
    asm_.inc_rcx();            // tail + 1
    asm_.mov_rax_rcx();        // RAX = tail + 1 (dividend)
    asm_.push_rax();           // save channel pointer location
    asm_.mov_rax_mem_rsp(16);  // reload channel
    asm_.mov_rcx_mem_rax(32);  // capacity in RCX (divisor)
    asm_.pop_rax();            // RAX = tail + 1
    asm_.xor_rdx_rdx();        // RDX = 0 (high bits of dividend)
    asm_.div_rdx();            // div rcx: RAX = quotient, RDX = remainder
    asm_.mov_rax_mem_rsp(8);   // channel
    asm_.mov_mem_rax_rdx(56);  // store new tail (remainder)
    
    // Increment count
    asm_.mov_rcx_mem_rax(64);
    asm_.inc_rcx();
    asm_.mov_mem_rax_rcx(64);
    
    // Signal not_empty event
    asm_.mov_rcx_mem_rax(8);  // event_not_empty
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("SetEvent"));
    asm_.add_rsp_imm32(0x28);
    
    // Release mutex
    asm_.mov_rax_mem_rsp(8);
    asm_.mov_rcx_mem_rax(0);
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("ReleaseMutex"));
    asm_.add_rsp_imm32(0x28);
    
    asm_.label(sendDone);
    asm_.add_rsp_imm32(16);  // Clean up saved values
}

void NativeCodeGen::emitChannelRecv() {
    // Channel pointer in RAX
    // Returns received value in RAX
    // After 1 push, stack needs adjustment for alignment
    asm_.push_rax();  // channel
    asm_.sub_rsp_imm32(8);  // Align stack to 16 bytes
    
    std::string waitLoop = newLabel("chan_recv_wait");
    std::string recvDone = newLabel("chan_recv_done");
    
    asm_.label(waitLoop);
    
    // Acquire mutex - always use shadow space
    asm_.mov_rax_mem_rsp(8);  // channel pointer (adjusted for alignment padding)
    asm_.mov_rcx_mem_rax(0);  // mutex handle
    asm_.mov_rdx_imm64(0xFFFFFFFF);  // INFINITE
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("WaitForSingleObject"));
    asm_.add_rsp_imm32(0x28);
    
    // Check if buffer is empty (count == 0)
    asm_.mov_rax_mem_rsp(8);  // channel
    asm_.mov_rcx_mem_rax(64);  // count
    asm_.test_rcx_rcx();
    
    std::string notEmpty = newLabel("chan_not_empty");
    asm_.jnz_rel32(notEmpty);
    
    // Buffer is empty - release mutex and wait for not_empty event
    asm_.mov_rax_mem_rsp(8);
    asm_.mov_rcx_mem_rax(0);  // mutex
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("ReleaseMutex"));
    asm_.add_rsp_imm32(0x28);
    
    // Wait for not_empty event
    asm_.mov_rax_mem_rsp(8);
    asm_.mov_rcx_mem_rax(8);  // event_not_empty
    asm_.mov_rdx_imm64(0xFFFFFFFF);
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("WaitForSingleObject"));
    asm_.add_rsp_imm32(0x28);
    
    asm_.jmp_rel32(waitLoop);
    
    asm_.label(notEmpty);
    
    // Read value from buffer at head position
    asm_.mov_rax_mem_rsp(8);  // channel
    asm_.mov_rcx_mem_rax(24);  // buffer pointer
    asm_.mov_rdx_mem_rax(48);  // head index
    asm_.mov_r8_mem_rax(40);   // element size
    
    // Calculate offset: buffer + (head * element_size)
    asm_.imul_rdx_r8();
    asm_.add_rcx_rdx();  // RCX = buffer + head * elem_size
    
    // Read value
    asm_.mov_r9_mem_rcx(0);  // R9 = received value
    asm_.push_r9();  // Save received value
    
    // Increment head: head = (head + 1) % capacity
    asm_.mov_rax_mem_rsp(16);  // channel (offset: 8 padding + 8 push)
    asm_.mov_rcx_mem_rax(48);  // head in RCX
    asm_.inc_rcx();            // head + 1
    asm_.mov_rax_rcx();        // RAX = head + 1 (dividend)
    asm_.push_rax();           // save it
    asm_.mov_rax_mem_rsp(24);  // reload channel (offset increased by 8)
    asm_.mov_rcx_mem_rax(32);  // capacity in RCX (divisor)
    asm_.pop_rax();            // RAX = head + 1
    asm_.xor_rdx_rdx();        // RDX = 0 (high bits of dividend)
    asm_.div_rdx();            // div rcx: RAX = quotient, RDX = remainder
    asm_.mov_rax_mem_rsp(16);  // channel
    asm_.mov_mem_rax_rdx(48);  // store new head (remainder)
    
    // Decrement count
    asm_.mov_rcx_mem_rax(64);
    asm_.dec_rcx();
    asm_.mov_mem_rax_rcx(64);
    
    // Signal not_full event
    asm_.mov_rcx_mem_rax(16);  // event_not_full
    asm_.sub_rsp_imm32(0x20);  // Only 0x20 since we have 1 push (8 bytes) for alignment
    asm_.call_mem_rip(pe_.getImportRVA("SetEvent"));
    asm_.add_rsp_imm32(0x20);
    
    // Release mutex
    asm_.mov_rax_mem_rsp(16);
    asm_.mov_rcx_mem_rax(0);
    asm_.sub_rsp_imm32(0x20);
    asm_.call_mem_rip(pe_.getImportRVA("ReleaseMutex"));
    asm_.add_rsp_imm32(0x20);
    
    asm_.label(recvDone);
    asm_.pop_rax();  // Return received value in RAX
    asm_.add_rsp_imm32(16);  // Clean up: 8 alignment + 8 channel pointer
}

void NativeCodeGen::emitChannelClose() {
    // Channel pointer in RAX
    asm_.push_rax();
    asm_.sub_rsp_imm32(8);  // Align stack
    
    // Acquire mutex
    asm_.mov_rax_mem_rsp(8);
    asm_.mov_rcx_mem_rax(0);  // mutex handle
    asm_.mov_rdx_imm64(0xFFFFFFFF);
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("WaitForSingleObject"));
    asm_.add_rsp_imm32(0x28);
    
    // Set closed flag
    asm_.mov_rax_mem_rsp(8);
    asm_.mov_rcx_imm64(1);
    asm_.mov_mem_rax_rcx(72);
    
    // Signal both events to wake up any waiting threads
    asm_.mov_rcx_mem_rax(8);  // event_not_empty
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("SetEvent"));
    asm_.add_rsp_imm32(0x28);
    
    asm_.mov_rax_mem_rsp(8);
    asm_.mov_rcx_mem_rax(16);  // event_not_full
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("SetEvent"));
    asm_.add_rsp_imm32(0x28);
    
    // Release mutex
    asm_.mov_rax_mem_rsp(8);
    asm_.mov_rcx_mem_rax(0);
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("ReleaseMutex"));
    asm_.add_rsp_imm32(0x28);
    
    asm_.add_rsp_imm32(16);  // Clean up: 8 alignment + 8 channel pointer
}

void NativeCodeGen::visit(MakeChanExpr& node) {
    // Create a new channel with the specified buffer size
    int32_t elemSize = getTypeSize(node.elementType);
    if (elemSize == 0) elemSize = 8;  // Default to 8 bytes for unknown types
    
    emitChannelCreate(node.bufferSize, elemSize);
    // RAX now contains pointer to the new channel
}

void NativeCodeGen::visit(ChanSendExpr& node) {
    // Evaluate the value to send first
    node.value->accept(*this);
    asm_.push_rax();  // Save value
    
    // Evaluate the channel
    node.channel->accept(*this);
    asm_.mov_rcx_rax();  // Channel in RCX temporarily
    asm_.pop_rax();      // Value in RAX
    asm_.xchg_rax_rcx(); // Now: channel in RAX, value in RCX
    
    emitChannelSend();
    
    // Send returns void, but we leave RAX as-is
    asm_.xor_rax_rax();
}

void NativeCodeGen::visit(ChanRecvExpr& node) {
    // Evaluate the channel
    node.channel->accept(*this);
    // Channel pointer is in RAX
    
    emitChannelRecv();
    // Received value is now in RAX
}

} // namespace tyl
