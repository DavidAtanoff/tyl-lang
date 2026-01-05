// Tyl Compiler - Native Code Generator GC Support
// Full mark-and-sweep garbage collection with automatic collection by default
// Manual control available via gc_disable(), gc_enable(), gc_collect()

#include "codegen_base.h"

namespace tyl {

// GC Data Section Layout (offsets from gcDataRVA_):
// Offset 0:   gc_alloc_head (8 bytes)     - Head of allocation linked list
// Offset 8:   gc_total_bytes (8 bytes)    - Total bytes currently allocated
// Offset 16:  gc_threshold (8 bytes)      - Collection threshold (default 1MB)
// Offset 24:  gc_enabled (8 bytes)        - GC enabled flag (1 = enabled, default)
// Offset 32:  gc_collections (8 bytes)    - Number of collections performed
// Offset 40:  gc_stack_bottom (8 bytes)   - Bottom of stack for root scanning
// Total: 48 bytes

// GC Object Header Layout (16 bytes, before user data):
// Offset -16: size (4 bytes)   - Size of user data
// Offset -12: type (2 bytes)   - Object type for tracing
// Offset -10: marked (1 byte)  - Mark bit
// Offset -9:  flags (1 byte)   - Flags (pinned, etc.)
// Offset -8:  next (8 bytes)   - Next object in allocation list
// Offset 0:   User data starts here

// Initialize GC data section - called once during compile
void NativeCodeGen::emitGCInit() {
    if (gcInitEmitted_ || !useGC_) return;
    
    // Store stack bottom (RSP at program start) for conservative scanning
    // mov rax, rsp
    asm_.code.push_back(0x48); asm_.code.push_back(0x89); asm_.code.push_back(0xE0);
    // Store to gc_stack_bottom
    asm_.lea_rcx_rip_fixup(gcDataRVA_ + 40);
    asm_.mov_mem_rcx_rax();
    
    gcInitEmitted_ = true;
}

// Emit GC shutdown at program end
void NativeCodeGen::emitGCShutdown() {
    if (!useGC_) return;
    // On Windows, process exit cleans up all memory
    // Could walk allObjects and free each for cleaner shutdown
}


// Emit GC allocation with automatic collection
// size: bytes to allocate (user data only)
// type: object type for tracing
// Result: pointer to user data in RAX
void NativeCodeGen::emitGCAlloc(size_t size, GCObjectType type) {
    // Calculate total size: header (16 bytes) + user data, aligned to 8
    size_t totalSize = 16 + size;
    totalSize = (totalSize + 7) & ~7;
    
    // Labels for control flow
    std::string skipCollectLabel = newLabel("gc_skip_collect");
    std::string afterCollectLabel = newLabel("gc_after_collect");
    
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x28);
    
    // Check if we should collect: gc_total_bytes + size > gc_threshold && gc_enabled
    // Load gc_total_bytes
    asm_.lea_rax_rip_fixup(gcDataRVA_ + 8);
    asm_.mov_rax_mem_rax();
    asm_.add_rax_imm32(static_cast<int32_t>(totalSize));
    asm_.push_rax();  // Save new total
    
    // Load gc_threshold
    asm_.lea_rax_rip_fixup(gcDataRVA_ + 16);
    asm_.mov_rcx_mem_rax();
    
    // Compare: if new_total <= threshold, skip collection
    asm_.pop_rax();
    asm_.cmp_rax_rcx();
    asm_.jle_rel32(skipCollectLabel);
    
    // Check if GC is enabled
    asm_.lea_rax_rip_fixup(gcDataRVA_ + 24);
    asm_.mov_rax_mem_rax();
    asm_.test_rax_rax();
    asm_.jz_rel32(skipCollectLabel);
    
    // Trigger collection
    asm_.call_rel32(gcCollectLabel_);
    
    asm_.label(skipCollectLabel);
    
    // Allocate memory via HeapAlloc
    asm_.call_mem_rip(pe_.getImportRVA("GetProcessHeap"));
    asm_.mov_rcx_rax();
    asm_.mov_rdx_imm64(0x08);  // HEAP_ZERO_MEMORY
    asm_.mov_r8d_imm32(static_cast<int32_t>(totalSize));
    asm_.call_mem_rip(pe_.getImportRVA("HeapAlloc"));
    
    // RAX = pointer to header
    // Check for allocation failure
    asm_.test_rax_rax();
    std::string allocOkLabel = newLabel("gc_alloc_ok");
    asm_.jnz_rel32(allocOkLabel);
    
    // Allocation failed - try collecting and retry
    asm_.call_rel32(gcCollectLabel_);
    asm_.call_mem_rip(pe_.getImportRVA("GetProcessHeap"));
    asm_.mov_rcx_rax();
    asm_.mov_rdx_imm64(0x08);
    asm_.mov_r8d_imm32(static_cast<int32_t>(totalSize));
    asm_.call_mem_rip(pe_.getImportRVA("HeapAlloc"));
    
    asm_.label(allocOkLabel);
    asm_.push_rax();  // Save header pointer
    
    // Initialize header fields
    // [rax+0] = size (4 bytes)
    asm_.code.push_back(0xC7);
    asm_.code.push_back(0x00);
    asm_.code.push_back(size & 0xFF);
    asm_.code.push_back((size >> 8) & 0xFF);
    asm_.code.push_back((size >> 16) & 0xFF);
    asm_.code.push_back((size >> 24) & 0xFF);
    
    // [rax+4] = type (2 bytes)
    asm_.code.push_back(0x66);
    asm_.code.push_back(0xC7);
    asm_.code.push_back(0x40);
    asm_.code.push_back(0x04);
    asm_.code.push_back(static_cast<uint16_t>(type) & 0xFF);
    asm_.code.push_back((static_cast<uint16_t>(type) >> 8) & 0xFF);
    
    // [rax+6] = marked = 0, [rax+7] = flags = 0 (already zeroed)
    
    // Link into allocation list: header->next = gc_alloc_head; gc_alloc_head = header
    // Load current head
    asm_.lea_rcx_rip_fixup(gcDataRVA_);  // gc_alloc_head address
    // mov rdx, [rcx]
    asm_.code.push_back(0x48); asm_.code.push_back(0x8B); asm_.code.push_back(0x11);
    
    // Store current head in header->next [rax+8]
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x89);
    asm_.code.push_back(0x50);
    asm_.code.push_back(0x08);  // mov [rax+8], rdx
    
    // Store header as new head
    asm_.mov_mem_rcx_rax();  // gc_alloc_head = header
    
    // Update gc_total_bytes
    asm_.lea_rcx_rip_fixup(gcDataRVA_ + 8);
    // mov rax, [rcx]
    asm_.code.push_back(0x48); asm_.code.push_back(0x8B); asm_.code.push_back(0x01);
    asm_.add_rax_imm32(static_cast<int32_t>(totalSize));
    asm_.mov_mem_rcx_rax();
    
    // Return pointer to user data (header + 16)
    asm_.pop_rax();
    asm_.add_rax_imm32(16);
    
    if (!stackAllocated_) asm_.add_rsp_imm32(0x28);
}


// Emit the GC collection routine (mark-and-sweep)
// This is called as a function: call gcCollectLabel_
void NativeCodeGen::emitGCCollectRoutine() {
    asm_.label(gcCollectLabel_);
    
    // Prologue - save callee-saved registers FIRST, then set up frame
    asm_.push_rbp();
    asm_.mov_rbp_rsp();
    
    // Save callee-saved registers we'll use
    asm_.push_rbx();
    asm_.push_r12();
    asm_.push_r13();
    asm_.push_r14();
    
    // Allocate local space AFTER saving registers
    asm_.sub_rsp_imm32(0x40);
    
    // ===== MARK PHASE =====
    // First, clear all mark bits
    // r12 = current object (walks allocation list)
    asm_.lea_rax_rip_fixup(gcDataRVA_);
    asm_.mov_rax_mem_rax();  // rax = gc_alloc_head
    asm_.mov_r12_rax();
    
    std::string clearLoopLabel = newLabel("gc_clear_loop");
    std::string clearDoneLabel = newLabel("gc_clear_done");
    
    asm_.label(clearLoopLabel);
    // if (r12 == NULL) break
    asm_.code.push_back(0x4D); asm_.code.push_back(0x85); asm_.code.push_back(0xE4);  // test r12, r12
    asm_.jz_rel32(clearDoneLabel);
    
    // Clear mark bit: [r12+6] = 0
    asm_.code.push_back(0x41); asm_.code.push_back(0xC6);
    asm_.code.push_back(0x44); asm_.code.push_back(0x24); asm_.code.push_back(0x06);
    asm_.code.push_back(0x00);  // mov byte [r12+6], 0
    
    // r12 = r12->next ([r12+8])
    asm_.code.push_back(0x4D); asm_.code.push_back(0x8B);
    asm_.code.push_back(0x64); asm_.code.push_back(0x24); asm_.code.push_back(0x08);  // mov r12, [r12+8]
    asm_.jmp_rel32(clearLoopLabel);
    
    asm_.label(clearDoneLabel);
    
    // ===== CONSERVATIVE STACK SCANNING =====
    // Scan from current RSP to gc_stack_bottom
    // For each potential pointer, check if it points into our heap
    
    // r13 = current stack position (RSP)
    // r14 = stack bottom
    asm_.code.push_back(0x49); asm_.code.push_back(0x89); asm_.code.push_back(0xE5);  // mov r13, rsp
    asm_.lea_rax_rip_fixup(gcDataRVA_ + 40);
    asm_.mov_rax_mem_rax();
    asm_.mov_r14_rax();
    
    std::string scanLoopLabel = newLabel("gc_scan_loop");
    std::string scanDoneLabel = newLabel("gc_scan_done");
    std::string notPtrLabel = newLabel("gc_not_ptr");
    
    asm_.label(scanLoopLabel);
    // if (r13 >= r14) done
    asm_.code.push_back(0x4D); asm_.code.push_back(0x39); asm_.code.push_back(0xF5);  // cmp r13, r14
    asm_.jge_rel32(scanDoneLabel);
    
    // Load potential pointer from stack: rbx = [r13]
    asm_.code.push_back(0x49); asm_.code.push_back(0x8B); asm_.code.push_back(0x5D); asm_.code.push_back(0x00);  // mov rbx, [r13]
    
    // Check if this looks like a pointer (non-null, aligned)
    asm_.test_rax_rax();  // Actually test rbx
    asm_.code.push_back(0x48); asm_.code.push_back(0x85); asm_.code.push_back(0xDB);  // test rbx, rbx
    asm_.jz_rel32(notPtrLabel);
    
    // Check alignment (must be 8-byte aligned for our allocations)
    asm_.code.push_back(0xF6); asm_.code.push_back(0xC3); asm_.code.push_back(0x07);  // test bl, 7
    asm_.jnz_rel32(notPtrLabel);
    
    // Walk allocation list to see if rbx points to any object's user data
    // rbx should equal header + 16 for some header in our list
    // So check if (rbx - 16) is in our allocation list
    asm_.mov_rax_rbx();
    asm_.sub_rax_imm32(16);  // rax = potential header
    
    // Walk list to find this header
    asm_.push_r13();  // Save scan position
    asm_.lea_rcx_rip_fixup(gcDataRVA_);
    // mov rcx, [rcx] - load gc_alloc_head
    asm_.code.push_back(0x48); asm_.code.push_back(0x8B); asm_.code.push_back(0x09);
    
    std::string findLoopLabel = newLabel("gc_find_loop");
    std::string foundLabel = newLabel("gc_found");
    std::string notFoundLabel = newLabel("gc_not_found");
    
    asm_.label(findLoopLabel);
    asm_.code.push_back(0x48); asm_.code.push_back(0x85); asm_.code.push_back(0xC9);  // test rcx, rcx
    asm_.jz_rel32(notFoundLabel);
    
    // if (rcx == rax) found!
    asm_.cmp_rax_rcx();
    asm_.code.push_back(0x0F); asm_.code.push_back(0x84);  // je found
    asm_.fixupLabel(foundLabel);;
    
    // rcx = rcx->next
    asm_.code.push_back(0x48); asm_.code.push_back(0x8B);
    asm_.code.push_back(0x49); asm_.code.push_back(0x08);  // mov rcx, [rcx+8]
    asm_.jmp_rel32(findLoopLabel);
    
    asm_.label(foundLabel);
    // Mark this object: [rcx+6] = 1
    asm_.code.push_back(0xC6); asm_.code.push_back(0x41);
    asm_.code.push_back(0x06); asm_.code.push_back(0x01);  // mov byte [rcx+6], 1
    
    // Note: Recursive tracing of children (LIST, RECORD, CLOSURE) is handled
    // by the conservative stack scan which will find pointers to child objects
    // stored on the stack or in registers.
    
    asm_.label(notFoundLabel);
    asm_.pop_r13();  // Restore scan position
    
    asm_.label(notPtrLabel);
    // r13 += 8 (next stack slot)
    asm_.code.push_back(0x49); asm_.code.push_back(0x83); asm_.code.push_back(0xC5); asm_.code.push_back(0x08);  // add r13, 8
    asm_.jmp_rel32(scanLoopLabel);
    
    asm_.label(scanDoneLabel);

    
    // ===== SWEEP PHASE =====
    // Walk allocation list, free unmarked objects, rebuild list
    // r12 = previous (for relinking), r13 = current
    // rbx = new head
    
    asm_.xor_rbx_rbx();  // new_head = NULL
    asm_.xor_r12_r12();  // prev = NULL
    asm_.lea_rax_rip_fixup(gcDataRVA_);
    asm_.mov_rax_mem_rax();
    asm_.mov_r13_rax();  // current = gc_alloc_head
    
    // r14 = bytes freed (for updating gc_total_bytes)
    asm_.xor_r14_r14();
    
    std::string sweepLoopLabel = newLabel("gc_sweep_loop");
    std::string sweepDoneLabel = newLabel("gc_sweep_done");
    std::string keepObjLabel = newLabel("gc_keep_obj");
    std::string freeObjLabel = newLabel("gc_free_obj");
    
    asm_.label(sweepLoopLabel);
    // if (r13 == NULL) done
    asm_.code.push_back(0x4D); asm_.code.push_back(0x85); asm_.code.push_back(0xED);  // test r13, r13
    asm_.jz_rel32(sweepDoneLabel);
    
    // Save next pointer before potentially freeing: [rbp-8] = r13->next
    asm_.code.push_back(0x4D); asm_.code.push_back(0x8B);
    asm_.code.push_back(0x45); asm_.code.push_back(0x08);  // mov r8, [r13+8] (next)
    asm_.code.push_back(0x4C); asm_.code.push_back(0x89);
    asm_.code.push_back(0x45); asm_.code.push_back(0xF8);  // mov [rbp-8], r8
    
    // Check mark bit: if ([r13+6] != 0) keep
    asm_.code.push_back(0x41); asm_.code.push_back(0x80);
    asm_.code.push_back(0x7D); asm_.code.push_back(0x06); asm_.code.push_back(0x00);  // cmp byte [r13+6], 0
    asm_.jnz_rel32(keepObjLabel);
    
    // ===== FREE THIS OBJECT =====
    asm_.label(freeObjLabel);
    
    // Add size to bytes freed: r14 += [r13+0] (size) + 16 (header)
    asm_.code.push_back(0x41); asm_.code.push_back(0x8B);
    asm_.code.push_back(0x45); asm_.code.push_back(0x00);  // mov eax, [r13+0] (size, 32-bit)
    asm_.code.push_back(0x48); asm_.code.push_back(0x98);  // cdqe (sign extend to 64-bit)
    asm_.add_rax_imm32(16);  // + header size
    // Round up to 8-byte alignment
    asm_.add_rax_imm32(7);
    asm_.code.push_back(0x48); asm_.code.push_back(0x83); asm_.code.push_back(0xE0); asm_.code.push_back(0xF8);  // and rax, ~7
    asm_.code.push_back(0x49); asm_.code.push_back(0x01); asm_.code.push_back(0xC6);  // add r14, rax
    
    // HeapFree(GetProcessHeap(), 0, r13)
    asm_.call_mem_rip(pe_.getImportRVA("GetProcessHeap"));
    asm_.mov_rcx_rax();
    asm_.xor_rax_rax();
    asm_.mov_rdx_rax();  // flags = 0
    asm_.code.push_back(0x4D); asm_.code.push_back(0x89); asm_.code.push_back(0xE8);  // mov r8, r13
    asm_.call_mem_rip(pe_.getImportRVA("HeapFree"));
    
    // Move to next (don't update prev since we removed current)
    asm_.code.push_back(0x4C); asm_.code.push_back(0x8B);
    asm_.code.push_back(0x6D); asm_.code.push_back(0xF8);  // mov r13, [rbp-8] (saved next)
    asm_.jmp_rel32(sweepLoopLabel);
    
    asm_.label(keepObjLabel);
    // Keep this object - add to new list
    // Clear mark bit for next collection
    asm_.code.push_back(0x41); asm_.code.push_back(0xC6);
    asm_.code.push_back(0x45); asm_.code.push_back(0x06); asm_.code.push_back(0x00);  // mov byte [r13+6], 0
    
    // Link: current->next = new_head; new_head = current
    asm_.code.push_back(0x49); asm_.code.push_back(0x89);
    asm_.code.push_back(0x5D); asm_.code.push_back(0x08);  // mov [r13+8], rbx
    asm_.code.push_back(0x4C); asm_.code.push_back(0x89); asm_.code.push_back(0xEB);  // mov rbx, r13
    
    // Move to next
    asm_.code.push_back(0x4C); asm_.code.push_back(0x8B);
    asm_.code.push_back(0x6D); asm_.code.push_back(0xF8);  // mov r13, [rbp-8]
    asm_.jmp_rel32(sweepLoopLabel);
    
    asm_.label(sweepDoneLabel);
    
    // Update gc_alloc_head = new_head (rbx)
    asm_.lea_rax_rip_fixup(gcDataRVA_);
    asm_.code.push_back(0x48); asm_.code.push_back(0x89); asm_.code.push_back(0x18);  // mov [rax], rbx
    
    // Update gc_total_bytes -= bytes_freed (r14)
    asm_.lea_rax_rip_fixup(gcDataRVA_ + 8);
    asm_.mov_rcx_mem_rax();
    asm_.code.push_back(0x4C); asm_.code.push_back(0x29); asm_.code.push_back(0xF1);  // sub rcx, r14
    asm_.mov_mem_rax_rcx();
    
    // Increment gc_collections counter
    asm_.lea_rax_rip_fixup(gcDataRVA_ + 32);
    asm_.mov_rcx_mem_rax();
    asm_.inc_rcx();
    asm_.mov_mem_rax_rcx();
    
    // Epilogue - deallocate local space first, then restore registers
    asm_.add_rsp_imm32(0x40);  // Deallocate local space
    
    // Restore callee-saved registers (in reverse order of saving)
    asm_.pop_r14();
    asm_.pop_r13();
    asm_.pop_r12();
    asm_.pop_rbx();
    
    // Restore frame pointer and return
    asm_.pop_rbp();
    asm_.ret();
}


// Emit list allocation via GC
// capacity: initial capacity (number of elements)
// Result: pointer to list data in RAX
// List layout: [count:8][capacity:8][elements:capacity*8]
void NativeCodeGen::emitGCAllocList(size_t capacity) {
    size_t size = 16 + capacity * 8;  // count + capacity + elements
    emitGCAlloc(size, GCObjectType::LIST);
    
    // Initialize list header
    asm_.push_rax();
    
    // [rax+0] = count = 0
    asm_.code.push_back(0x48);
    asm_.code.push_back(0xC7);
    asm_.code.push_back(0x00);
    asm_.code.push_back(0x00);
    asm_.code.push_back(0x00);
    asm_.code.push_back(0x00);
    asm_.code.push_back(0x00);
    
    // [rax+8] = capacity
    asm_.code.push_back(0x48);
    asm_.code.push_back(0xC7);
    asm_.code.push_back(0x40);
    asm_.code.push_back(0x08);
    asm_.code.push_back(capacity & 0xFF);
    asm_.code.push_back((capacity >> 8) & 0xFF);
    asm_.code.push_back((capacity >> 16) & 0xFF);
    asm_.code.push_back((capacity >> 24) & 0xFF);
    
    asm_.pop_rax();
}

// Emit record allocation via GC
// fieldCount: number of fields
// typeId: unique type identifier for RTTI (0 = anonymous/unknown)
// Result: pointer to record data in RAX
// Record layout: [fieldCount:8][typeId:8][fields:fieldCount*8]
void NativeCodeGen::emitGCAllocRecord(size_t fieldCount, uint64_t typeId) {
    size_t size = 16 + fieldCount * 8;  // fieldCount + typeId + fields
    emitGCAlloc(size, GCObjectType::RECORD);
    
    asm_.push_rax();
    
    // [rax+0] = fieldCount
    asm_.code.push_back(0x48);
    asm_.code.push_back(0xC7);
    asm_.code.push_back(0x00);
    asm_.code.push_back(fieldCount & 0xFF);
    asm_.code.push_back((fieldCount >> 8) & 0xFF);
    asm_.code.push_back((fieldCount >> 16) & 0xFF);
    asm_.code.push_back((fieldCount >> 24) & 0xFF);
    
    // [rax+8] = typeId
    asm_.mov_rcx_imm64(static_cast<int64_t>(typeId));
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x89);
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x08);  // mov [rax+8], rcx
    
    asm_.pop_rax();
}

// Emit closure allocation via GC
// captureCount: number of captured variables
// Result: pointer to closure data in RAX
// Closure layout: [fnPtr:8][captureCount:8][captures:captureCount*8]
void NativeCodeGen::emitGCAllocClosure(size_t captureCount) {
    size_t size = 16 + captureCount * 8;
    emitGCAlloc(size, GCObjectType::CLOSURE);
    
    asm_.push_rax();
    
    // [rax+8] = captureCount
    asm_.code.push_back(0x48);
    asm_.code.push_back(0xC7);
    asm_.code.push_back(0x40);
    asm_.code.push_back(0x08);
    asm_.code.push_back(captureCount & 0xFF);
    asm_.code.push_back((captureCount >> 8) & 0xFF);
    asm_.code.push_back((captureCount >> 16) & 0xFF);
    asm_.code.push_back((captureCount >> 24) & 0xFF);
    
    asm_.pop_rax();
}

// Emit string allocation via GC
// len: string length (not including null terminator)
// Result: pointer to string data in RAX
void NativeCodeGen::emitGCAllocString(size_t len) {
    emitGCAlloc(len + 1, GCObjectType::STRING);
}

// Emit map allocation via GC
// capacity: number of buckets
// Result: pointer to map data in RAX
// Map layout: [capacity:8][size:8][buckets:capacity*8]
void NativeCodeGen::emitGCAllocMap(size_t capacity) {
    size_t size = 16 + capacity * 8;  // capacity + size + bucket pointers
    emitGCAlloc(size, GCObjectType::ARRAY);  // Use ARRAY type for maps
    
    asm_.push_rax();
    
    // [rax+0] = capacity
    asm_.code.push_back(0x48);
    asm_.code.push_back(0xC7);
    asm_.code.push_back(0x00);
    asm_.code.push_back(capacity & 0xFF);
    asm_.code.push_back((capacity >> 8) & 0xFF);
    asm_.code.push_back((capacity >> 16) & 0xFF);
    asm_.code.push_back((capacity >> 24) & 0xFF);
    
    // [rax+8] = size = 0 (already zeroed)
    
    asm_.pop_rax();
}

// Emit map entry allocation via GC
// Result: pointer to entry data in RAX
// Entry layout: [hash:8][key_ptr:8][value:8][next:8] = 32 bytes
void NativeCodeGen::emitGCAllocMapEntry() {
    emitGCAlloc(32, GCObjectType::ARRAY);
}

// Emit raw allocation via GC (for general purpose allocations)
// size: bytes to allocate
// Result: pointer to data in RAX
void NativeCodeGen::emitGCAllocRaw(size_t size) {
    emitGCAlloc(size, GCObjectType::RAW);
}

// Emit stack frame push for GC (conservative stack scanning)
void NativeCodeGen::emitGCPushFrame() {
    if (!useGC_) return;
    // For conservative scanning, we don't need explicit frame tracking
    // The stack scan will find all pointers between RSP and stack_bottom
}

// Emit stack frame pop for GC
void NativeCodeGen::emitGCPopFrame() {
    if (!useGC_) return;
    // No-op for conservative scanning
}

// ============================================================================
// Ownership System - Clone Helpers
// ============================================================================

// Deep copy a list
// Input: RAX = source list pointer
// Output: RAX = new list pointer (deep copy)
// List layout: [count:8][capacity:8][elements:capacity*8]
void NativeCodeGen::emitListClone() {
    // Save source pointer
    allocLocal("$clone_src");
    asm_.mov_mem_rbp_rax(locals["$clone_src"]);
    
    // Get count from source: [rax+0]
    asm_.mov_rcx_mem_rax();  // rcx = count
    allocLocal("$clone_count");
    asm_.mov_rax_rcx();
    asm_.mov_mem_rbp_rax(locals["$clone_count"]);
    
    // Get capacity from source: [src+8]
    asm_.mov_rax_mem_rbp(locals["$clone_src"]);
    asm_.add_rax_imm32(8);
    asm_.mov_rax_mem_rax();  // rax = capacity
    allocLocal("$clone_cap");
    asm_.mov_mem_rbp_rax(locals["$clone_cap"]);
    
    // Calculate allocation size: 16 + 16 + capacity * 8 (GC header + list header + elements)
    // GC header is 16 bytes, list header is 16 bytes (count + capacity)
    asm_.code.push_back(0x48); asm_.code.push_back(0xC1);
    asm_.code.push_back(0xE0); asm_.code.push_back(0x03);  // shl rax, 3 (multiply by 8)
    asm_.add_rax_imm32(32);  // add GC header (16) + list header (16)
    
    // Align to 8 bytes
    asm_.add_rax_imm32(7);
    asm_.code.push_back(0x48); asm_.code.push_back(0x83);
    asm_.code.push_back(0xE0); asm_.code.push_back(0xF8);  // and rax, -8
    
    // Save total size
    allocLocal("$clone_size");
    asm_.mov_mem_rbp_rax(locals["$clone_size"]);
    
    // Call HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size)
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x20);
    asm_.call_mem_rip(pe_.getImportRVA("GetProcessHeap"));
    if (!stackAllocated_) asm_.add_rsp_imm32(0x20);
    
    asm_.mov_rcx_rax();  // heap handle
    asm_.mov_rdx_imm64(0x08);  // HEAP_ZERO_MEMORY
    // Load size into r8: mov r8, [rbp + offset]
    asm_.mov_rax_mem_rbp(locals["$clone_size"]);
    asm_.mov_r8_rax();  // r8 = size
    
    if (!stackAllocated_) asm_.sub_rsp_imm32(0x20);
    asm_.call_mem_rip(pe_.getImportRVA("HeapAlloc"));
    if (!stackAllocated_) asm_.add_rsp_imm32(0x20);
    
    // RAX now has allocated memory pointer
    // Skip GC header (16 bytes) to get user data pointer
    asm_.add_rax_imm32(16);
    
    // Save new list pointer
    allocLocal("$clone_dst");
    asm_.mov_mem_rbp_rax(locals["$clone_dst"]);
    
    // Initialize new list header
    // Set count: [dst+0] = count
    asm_.mov_rcx_mem_rbp(locals["$clone_count"]);
    asm_.mov_mem_rax_rcx();
    
    // Set capacity: [dst+8] = capacity
    asm_.mov_rax_mem_rbp(locals["$clone_dst"]);
    asm_.add_rax_imm32(8);
    asm_.mov_rcx_mem_rbp(locals["$clone_cap"]);
    asm_.mov_mem_rax_rcx();
    
    // Copy elements: loop from 0 to count-1
    asm_.mov_rax_mem_rbp(locals["$clone_count"]);
    asm_.test_rax_rax();
    std::string endLabel = newLabel("clone_end");
    asm_.jz_rel32(endLabel);  // Skip if count == 0
    
    // Initialize loop counter
    allocLocal("$clone_i");
    asm_.xor_rax_rax();
    asm_.mov_mem_rbp_rax(locals["$clone_i"]);
    
    std::string loopLabel = newLabel("clone_loop");
    asm_.label(loopLabel);
    
    // Load element from source: src[16 + i*8]
    asm_.mov_rax_mem_rbp(locals["$clone_src"]);
    asm_.mov_rcx_mem_rbp(locals["$clone_i"]);
    asm_.code.push_back(0x48); asm_.code.push_back(0xC1);
    asm_.code.push_back(0xE1); asm_.code.push_back(0x03);  // shl rcx, 3
    asm_.add_rcx_imm32(16);  // offset = 16 + i*8
    asm_.code.push_back(0x48); asm_.code.push_back(0x01); asm_.code.push_back(0xC8);  // add rax, rcx
    asm_.mov_rax_mem_rax();  // rax = src[i]
    asm_.push_rax();  // Save element value
    
    // Store element to dest: dst[16 + i*8]
    asm_.mov_rax_mem_rbp(locals["$clone_dst"]);
    asm_.mov_rcx_mem_rbp(locals["$clone_i"]);
    asm_.code.push_back(0x48); asm_.code.push_back(0xC1);
    asm_.code.push_back(0xE1); asm_.code.push_back(0x03);  // shl rcx, 3
    asm_.add_rcx_imm32(16);
    asm_.code.push_back(0x48); asm_.code.push_back(0x01); asm_.code.push_back(0xC8);  // add rax, rcx
    asm_.pop_rcx();  // Restore element value
    asm_.mov_mem_rax_rcx();  // dst[i] = element
    
    // Increment counter
    asm_.mov_rax_mem_rbp(locals["$clone_i"]);
    asm_.inc_rax();
    asm_.mov_mem_rbp_rax(locals["$clone_i"]);
    
    // Check if done: i < count
    asm_.mov_rcx_mem_rbp(locals["$clone_count"]);
    asm_.cmp_rax_rcx();
    asm_.jl_rel32(loopLabel);
    
    asm_.label(endLabel);
    
    // Return new list pointer
    asm_.mov_rax_mem_rbp(locals["$clone_dst"]);
}

// Deep copy a constant list (stored as raw data without header)
// Input: RAX = source data pointer (just elements, no count/capacity header)
// Input: count = number of elements
// Output: RAX = new GC-allocated list pointer (with proper header)
void NativeCodeGen::emitConstListClone(size_t count) {
    // Save source pointer
    allocLocal("$cclone_src");
    asm_.mov_mem_rbp_rax(locals["$cclone_src"]);
    
    // Allocate a proper GC list with header
    size_t capacity = count < 4 ? 4 : count;
    emitGCAllocList(capacity);
    
    // Save new list pointer
    allocLocal("$cclone_dst");
    asm_.mov_mem_rbp_rax(locals["$cclone_dst"]);
    
    // Set count: [dst+0] = count
    asm_.mov_rcx_imm64(static_cast<int64_t>(count));
    asm_.mov_mem_rax_rcx();
    
    // Copy elements from source (now has 16-byte header) to dest (also has 16-byte header)
    for (size_t i = 0; i < count; i++) {
        // Load element from source: src[16 + i*8] (skip header)
        asm_.mov_rax_mem_rbp(locals["$cclone_src"]);
        asm_.add_rax_imm32(16 + static_cast<int32_t>(i * 8));
        asm_.mov_rax_mem_rax();  // rax = src[16 + i*8]
        
        // Store to dest: dst[16 + i*8]
        asm_.mov_rcx_mem_rbp(locals["$cclone_dst"]);
        asm_.add_rcx_imm32(16 + static_cast<int32_t>(i * 8));
        asm_.mov_mem_rcx_rax();
    }
    
    // Return new list pointer
    asm_.mov_rax_mem_rbp(locals["$cclone_dst"]);
}

// Deep copy a record
// Input: RAX = source record pointer
// Output: RAX = new record pointer (deep copy)
void NativeCodeGen::emitRecordClone(const std::string& typeName) {
    auto typeIt = recordTypes_.find(typeName);
    if (typeIt == recordTypes_.end()) {
        // Unknown record type - just return the same pointer (shallow copy)
        return;
    }
    
    const RecordTypeInfo& typeInfo = typeIt->second;
    size_t fieldCount = typeInfo.fieldNames.size();
    
    // Save source pointer
    allocLocal("$rec_clone_src");
    asm_.mov_mem_rbp_rax(locals["$rec_clone_src"]);
    
    // Allocate new record
    size_t recordSize = 16 + fieldCount * 8;  // fieldCount + typeId + fields
    emitGCAllocRaw(recordSize);
    
    // Save new record pointer
    allocLocal("$rec_clone_dst");
    asm_.mov_mem_rbp_rax(locals["$rec_clone_dst"]);
    
    // Copy header (fieldCount and typeId)
    // Copy fieldCount: [dst+0] = [src+0]
    asm_.mov_rcx_mem_rbp(locals["$rec_clone_src"]);
    asm_.mov_rax_mem_rcx();  // rax = [rcx] = fieldCount
    asm_.mov_rcx_rax();      // rcx = fieldCount
    asm_.mov_rax_mem_rbp(locals["$rec_clone_dst"]);
    asm_.mov_mem_rax_rcx();
    
    // Copy typeId: [dst+8] = [src+8]
    asm_.mov_rax_mem_rbp(locals["$rec_clone_src"]);
    asm_.add_rax_imm32(8);
    asm_.mov_rax_mem_rax();  // rax = [src+8] = typeId
    asm_.mov_rcx_rax();      // rcx = typeId
    asm_.mov_rax_mem_rbp(locals["$rec_clone_dst"]);
    asm_.add_rax_imm32(8);
    asm_.mov_mem_rax_rcx();
    
    // Copy each field
    for (size_t i = 0; i < fieldCount; i++) {
        int32_t offset = 16 + static_cast<int32_t>(i * 8);
        
        // Load field from source
        asm_.mov_rax_mem_rbp(locals["$rec_clone_src"]);
        asm_.add_rax_imm32(offset);
        asm_.mov_rax_mem_rax();  // rax = src field value
        
        // Store to destination
        asm_.mov_rcx_mem_rbp(locals["$rec_clone_dst"]);
        asm_.add_rcx_imm32(offset);
        asm_.mov_mem_rcx_rax();
    }
    
    // Return new record pointer
    asm_.mov_rax_mem_rbp(locals["$rec_clone_dst"]);
}

} // namespace tyl
