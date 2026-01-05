// Tyl Compiler - Native Code Generator Synchronization Expressions
// Handles: Mutex, RWLock, Cond, Semaphore

#include "backend/codegen/codegen_base.h"

namespace tyl {

// Mutex structure layout (allocated on heap):
// Offset 0:  mutex handle (8 bytes) - Windows mutex handle
// Offset 8:  data pointer (8 bytes) - pointer to protected data
// Offset 16: element size (8 bytes) - size of protected data
// Total: 24 bytes for mutex header + element data

void NativeCodeGen::emitMutexCreate(int32_t elementSize) {
    // Allocate mutex structure (24 bytes) + data
    size_t totalSize = 24 + elementSize;
    
    // Allocate memory for mutex
    asm_.mov_rcx_imm64(totalSize);
    emitGCAllocRaw(totalSize);
    // RAX now contains pointer to mutex structure
    
    asm_.push_rax();  // Save mutex pointer
    
    // Create Windows mutex
    asm_.xor_rcx_rcx();  // lpMutexAttributes = NULL
    asm_.xor_rdx_rdx();  // bInitialOwner = FALSE
    asm_.xor_r8_r8();    // lpName = NULL
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("CreateMutexA"));
    asm_.add_rsp_imm32(0x28);
    
    // Store mutex handle at offset 0
    asm_.mov_rcx_rax();  // mutex handle
    asm_.mov_rax_mem_rsp(0);  // mutex pointer
    asm_.mov_mem_rax_rcx(0);  // store handle at offset 0
    
    // Set data pointer (offset 8) - points to offset 24
    asm_.mov_rax_mem_rsp(0);
    asm_.lea_rcx_rax_offset(24);
    asm_.mov_mem_rax_rcx(8);
    
    // Set element size (offset 16)
    asm_.mov_rax_mem_rsp(0);
    asm_.mov_rcx_imm64(elementSize);
    asm_.mov_mem_rax_rcx(16);
    
    // Return mutex pointer
    asm_.pop_rax();
}

void NativeCodeGen::emitMutexLock() {
    // Mutex pointer in RAX
    asm_.push_rax();
    asm_.sub_rsp_imm32(8);  // Align stack
    
    // Wait for mutex
    asm_.mov_rax_mem_rsp(8);  // mutex pointer
    asm_.mov_rcx_mem_rax(0);  // mutex handle
    asm_.mov_rdx_imm64(0xFFFFFFFF);  // INFINITE
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("WaitForSingleObject"));
    asm_.add_rsp_imm32(0x28);
    
    asm_.add_rsp_imm32(16);  // Clean up
}

void NativeCodeGen::emitMutexUnlock() {
    // Mutex pointer in RAX
    asm_.push_rax();
    asm_.sub_rsp_imm32(8);  // Align stack
    
    // Release mutex
    asm_.mov_rax_mem_rsp(8);  // mutex pointer
    asm_.mov_rcx_mem_rax(0);  // mutex handle
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("ReleaseMutex"));
    asm_.add_rsp_imm32(0x28);
    
    asm_.add_rsp_imm32(16);  // Clean up
}

// RWLock structure layout (allocated on heap):
// Offset 0:  SRWLOCK (8 bytes) - Windows SRW lock
// Offset 8:  data pointer (8 bytes) - pointer to protected data
// Offset 16: element size (8 bytes) - size of protected data
// Total: 24 bytes for rwlock header + element data

void NativeCodeGen::emitRWLockCreate(int32_t elementSize) {
    // Allocate rwlock structure (24 bytes) + data
    size_t totalSize = 24 + elementSize;
    
    // Allocate memory for rwlock
    asm_.mov_rcx_imm64(totalSize);
    emitGCAllocRaw(totalSize);
    // RAX now contains pointer to rwlock structure
    
    asm_.push_rax();  // Save rwlock pointer
    
    // Initialize SRW lock (SRWLOCK is initialized to 0, which is SRWLOCK_INIT)
    // The memory is already zeroed by GC allocation, but we call InitializeSRWLock for safety
    asm_.mov_rcx_rax();  // pointer to SRWLOCK
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("InitializeSRWLock"));
    asm_.add_rsp_imm32(0x28);
    
    // Set data pointer (offset 8) - points to offset 24
    asm_.mov_rax_mem_rsp(0);
    asm_.lea_rcx_rax_offset(24);
    asm_.mov_mem_rax_rcx(8);
    
    // Set element size (offset 16)
    asm_.mov_rax_mem_rsp(0);
    asm_.mov_rcx_imm64(elementSize);
    asm_.mov_mem_rax_rcx(16);
    
    // Return rwlock pointer
    asm_.pop_rax();
}

void NativeCodeGen::emitRWLockReadLock() {
    // RWLock pointer in RAX
    asm_.push_rax();
    asm_.sub_rsp_imm32(8);  // Align stack
    
    // Acquire shared lock
    asm_.mov_rax_mem_rsp(8);  // rwlock pointer (SRWLOCK is at offset 0)
    asm_.mov_rcx_rax();
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("AcquireSRWLockShared"));
    asm_.add_rsp_imm32(0x28);
    
    asm_.add_rsp_imm32(16);  // Clean up
}

void NativeCodeGen::emitRWLockWriteLock() {
    // RWLock pointer in RAX
    asm_.push_rax();
    asm_.sub_rsp_imm32(8);  // Align stack
    
    // Acquire exclusive lock
    asm_.mov_rax_mem_rsp(8);  // rwlock pointer (SRWLOCK is at offset 0)
    asm_.mov_rcx_rax();
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("AcquireSRWLockExclusive"));
    asm_.add_rsp_imm32(0x28);
    
    asm_.add_rsp_imm32(16);  // Clean up
}

void NativeCodeGen::emitRWLockUnlock() {
    // RWLock pointer in RAX
    // Note: We need to track whether we have a read or write lock
    // For simplicity, we'll release exclusive lock (caller must track lock type)
    asm_.push_rax();
    asm_.sub_rsp_imm32(8);  // Align stack
    
    // Release exclusive lock
    asm_.mov_rax_mem_rsp(8);  // rwlock pointer
    asm_.mov_rcx_rax();
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("ReleaseSRWLockExclusive"));
    asm_.add_rsp_imm32(0x28);
    
    asm_.add_rsp_imm32(16);  // Clean up
}

// Condition variable structure layout (allocated on heap):
// Offset 0:  CONDITION_VARIABLE (8 bytes) - Windows condition variable
// Total: 8 bytes

void NativeCodeGen::emitCondCreate() {
    // Allocate condition variable structure (8 bytes)
    asm_.mov_rcx_imm64(8);
    emitGCAllocRaw(8);
    // RAX now contains pointer to condition variable
    
    asm_.push_rax();  // Save cond pointer
    
    // Initialize condition variable
    asm_.mov_rcx_rax();  // pointer to CONDITION_VARIABLE
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("InitializeConditionVariable"));
    asm_.add_rsp_imm32(0x28);
    
    // Return cond pointer
    asm_.pop_rax();
}

void NativeCodeGen::emitCondWait() {
    // Cond pointer in RAX, Mutex pointer in RCX
    asm_.push_rax();  // cond
    asm_.push_rcx();  // mutex
    
    // SleepConditionVariableSRW(ConditionVariable, SRWLock, dwMilliseconds, Flags)
    // We use the mutex's underlying handle - but Windows CV works with SRWLock
    // For compatibility, we'll use SleepConditionVariableSRW with the mutex treated as SRWLock
    asm_.mov_rax_mem_rsp(8);  // cond pointer
    asm_.mov_rcx_rax();
    asm_.mov_rax_mem_rsp(0);  // mutex pointer (use as SRWLock)
    asm_.mov_rdx_rax();
    asm_.mov_r8_imm64(0xFFFFFFFF);  // INFINITE
    asm_.xor_r9_r9();  // Flags = 0 (exclusive mode)
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("SleepConditionVariableSRW"));
    asm_.add_rsp_imm32(0x28);
    
    asm_.add_rsp_imm32(16);  // Clean up
}

void NativeCodeGen::emitCondSignal() {
    // Cond pointer in RAX
    asm_.push_rax();
    asm_.sub_rsp_imm32(8);  // Align stack
    
    // Wake one waiter
    asm_.mov_rax_mem_rsp(8);  // cond pointer
    asm_.mov_rcx_rax();
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("WakeConditionVariable"));
    asm_.add_rsp_imm32(0x28);
    
    asm_.add_rsp_imm32(16);  // Clean up
}

void NativeCodeGen::emitCondBroadcast() {
    // Cond pointer in RAX
    asm_.push_rax();
    asm_.sub_rsp_imm32(8);  // Align stack
    
    // Wake all waiters
    asm_.mov_rax_mem_rsp(8);  // cond pointer
    asm_.mov_rcx_rax();
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("WakeAllConditionVariable"));
    asm_.add_rsp_imm32(0x28);
    
    asm_.add_rsp_imm32(16);  // Clean up
}

// Semaphore structure layout (allocated on heap):
// Offset 0:  semaphore handle (8 bytes) - Windows semaphore handle
// Total: 8 bytes

void NativeCodeGen::emitSemaphoreCreate(int64_t initialCount, int64_t maxCount) {
    // Allocate semaphore structure (8 bytes)
    asm_.mov_rcx_imm64(8);
    emitGCAllocRaw(8);
    // RAX now contains pointer to semaphore structure
    
    asm_.push_rax();  // Save semaphore pointer
    
    // Create Windows semaphore
    asm_.xor_rcx_rcx();  // lpSemaphoreAttributes = NULL
    asm_.mov_rdx_imm64(initialCount);  // lInitialCount
    asm_.mov_r8_imm64(maxCount);  // lMaximumCount
    asm_.xor_r9_r9();  // lpName = NULL
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("CreateSemaphoreA"));
    asm_.add_rsp_imm32(0x28);
    
    // Store semaphore handle at offset 0
    asm_.mov_rcx_rax();  // semaphore handle
    asm_.mov_rax_mem_rsp(0);  // semaphore pointer
    asm_.mov_mem_rax_rcx(0);  // store handle at offset 0
    
    // Return semaphore pointer
    asm_.pop_rax();
}

void NativeCodeGen::emitSemaphoreAcquire() {
    // Semaphore pointer in RAX
    asm_.push_rax();
    asm_.sub_rsp_imm32(8);  // Align stack
    
    // Wait for semaphore
    asm_.mov_rax_mem_rsp(8);  // semaphore pointer
    asm_.mov_rcx_mem_rax(0);  // semaphore handle
    asm_.mov_rdx_imm64(0xFFFFFFFF);  // INFINITE
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("WaitForSingleObject"));
    asm_.add_rsp_imm32(0x28);
    
    asm_.add_rsp_imm32(16);  // Clean up
}

void NativeCodeGen::emitSemaphoreRelease() {
    // Semaphore pointer in RAX
    asm_.push_rax();
    asm_.sub_rsp_imm32(8);  // Align stack
    
    // Release semaphore
    asm_.mov_rax_mem_rsp(8);  // semaphore pointer
    asm_.mov_rcx_mem_rax(0);  // semaphore handle
    asm_.mov_rdx_imm64(1);  // lReleaseCount = 1
    asm_.xor_r8_r8();  // lpPreviousCount = NULL
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("ReleaseSemaphore"));
    asm_.add_rsp_imm32(0x28);
    
    asm_.add_rsp_imm32(16);  // Clean up
}

void NativeCodeGen::emitSemaphoreTryAcquire() {
    // Semaphore pointer in RAX
    // Returns 1 if acquired, 0 if not
    asm_.push_rax();
    asm_.sub_rsp_imm32(8);  // Align stack
    
    // Try to acquire semaphore with 0 timeout
    asm_.mov_rax_mem_rsp(8);  // semaphore pointer
    asm_.mov_rcx_mem_rax(0);  // semaphore handle
    asm_.xor_rdx_rdx();  // dwMilliseconds = 0 (no wait)
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("WaitForSingleObject"));
    asm_.add_rsp_imm32(0x28);
    
    // Check result: WAIT_OBJECT_0 (0) = success, WAIT_TIMEOUT (258) = failed
    asm_.test_rax_rax();  // Check if RAX == 0
    std::string successLabel = newLabel("sem_try_success");
    std::string doneLabel = newLabel("sem_try_done");
    asm_.jz_rel32(successLabel);
    
    // Failed - return 0
    asm_.xor_rax_rax();
    asm_.jmp_rel32(doneLabel);
    
    asm_.label(successLabel);
    // Success - return 1
    asm_.mov_rax_imm64(1);
    
    asm_.label(doneLabel);
    asm_.add_rsp_imm32(16);  // Clean up
}

// AST visitor implementations

void NativeCodeGen::visit(MakeMutexExpr& node) {
    int32_t elemSize = getTypeSize(node.elementType);
    if (elemSize == 0) elemSize = 8;  // Default to 8 bytes
    emitMutexCreate(elemSize);
}

void NativeCodeGen::visit(MakeRWLockExpr& node) {
    int32_t elemSize = getTypeSize(node.elementType);
    if (elemSize == 0) elemSize = 8;  // Default to 8 bytes
    emitRWLockCreate(elemSize);
}

void NativeCodeGen::visit(MakeCondExpr& node) {
    emitCondCreate();
}

void NativeCodeGen::visit(MakeSemaphoreExpr& node) {
    emitSemaphoreCreate(node.initialCount, node.maxCount);
}

void NativeCodeGen::visit(MutexLockExpr& node) {
    node.mutex->accept(*this);
    emitMutexLock();
}

void NativeCodeGen::visit(MutexUnlockExpr& node) {
    node.mutex->accept(*this);
    emitMutexUnlock();
}

void NativeCodeGen::visit(RWLockReadExpr& node) {
    node.rwlock->accept(*this);
    emitRWLockReadLock();
}

void NativeCodeGen::visit(RWLockWriteExpr& node) {
    node.rwlock->accept(*this);
    emitRWLockWriteLock();
}

void NativeCodeGen::visit(RWLockUnlockExpr& node) {
    node.rwlock->accept(*this);
    emitRWLockUnlock();
}

void NativeCodeGen::visit(CondWaitExpr& node) {
    // Evaluate mutex first
    node.mutex->accept(*this);
    asm_.push_rax();  // Save mutex
    
    // Evaluate cond
    node.cond->accept(*this);
    asm_.pop_rcx();  // Mutex in RCX
    
    emitCondWait();
}

void NativeCodeGen::visit(CondSignalExpr& node) {
    node.cond->accept(*this);
    emitCondSignal();
}

void NativeCodeGen::visit(CondBroadcastExpr& node) {
    node.cond->accept(*this);
    emitCondBroadcast();
}

void NativeCodeGen::visit(SemAcquireExpr& node) {
    node.sem->accept(*this);
    emitSemaphoreAcquire();
}

void NativeCodeGen::visit(SemReleaseExpr& node) {
    node.sem->accept(*this);
    emitSemaphoreRelease();
}

void NativeCodeGen::visit(SemTryAcquireExpr& node) {
    node.sem->accept(*this);
    emitSemaphoreTryAcquire();
}

void NativeCodeGen::visit(LockStmt& node) {
    // Evaluate mutex
    node.mutex->accept(*this);
    asm_.push_rax();  // Save mutex pointer
    
    // Lock the mutex
    emitMutexLock();
    
    // Execute the body
    node.body->accept(*this);
    
    // Unlock the mutex
    asm_.pop_rax();  // Restore mutex pointer
    emitMutexUnlock();
}

// Atomic integer structure layout (allocated on heap):
// Offset 0:  value (8 bytes) - the atomic integer value
// Total: 8 bytes
// Note: On x64, aligned 8-byte reads/writes are atomic, and XCHG is always atomic

void NativeCodeGen::emitAtomicCreate(int64_t initialValue) {
    // Allocate atomic structure (8 bytes)
    asm_.mov_rcx_imm64(8);
    emitGCAllocRaw(8);
    // RAX now contains pointer to atomic structure
    
    // Store initial value at offset 0
    asm_.mov_rcx_imm64(initialValue);
    asm_.mov_mem_rax_rcx(0);
    
    // Return atomic pointer (already in RAX)
}

void NativeCodeGen::emitAtomicLoad(MemoryOrder order) {
    // Atomic pointer in RAX
    // On x64, aligned 8-byte MOV is atomic
    asm_.mov_rax_mem_rax();  // Load value atomically (rax = [rax])
    
    // Emit memory fence based on ordering
    if (order == MemoryOrder::Acquire || order == MemoryOrder::AcqRel || order == MemoryOrder::SeqCst) {
        // MFENCE for acquire/seqcst semantics
        // 0F AE F0 = mfence
        asm_.code.push_back(0x0F);
        asm_.code.push_back(0xAE);
        asm_.code.push_back(0xF0);
    }
}

void NativeCodeGen::emitAtomicStore(MemoryOrder order) {
    // Atomic pointer in RAX, value in RCX
    // Emit memory fence based on ordering
    if (order == MemoryOrder::Release || order == MemoryOrder::AcqRel || order == MemoryOrder::SeqCst) {
        // MFENCE for release/seqcst semantics
        asm_.code.push_back(0x0F);
        asm_.code.push_back(0xAE);
        asm_.code.push_back(0xF0);
    }
    // On x64, aligned 8-byte MOV is atomic
    asm_.mov_mem_rax_rcx();  // Store value atomically ([rax] = rcx)
}

void NativeCodeGen::emitAtomicSwap(MemoryOrder order) {
    // Atomic pointer in RAX, new value in RCX
    // XCHG is always atomic on x64 (implicit LOCK prefix)
    // xchg [rax], rcx - exchanges value at [rax] with rcx
    // Result (old value) ends up in RCX, then we move to RAX
    
    // 48 87 08 = xchg [rax], rcx
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x87);
    asm_.code.push_back(0x08);
    
    // Move old value from RCX to RAX
    asm_.mov_rax_rcx();
    
    // XCHG has implicit full barrier, but add fence for SeqCst
    if (order == MemoryOrder::SeqCst) {
        asm_.code.push_back(0x0F);
        asm_.code.push_back(0xAE);
        asm_.code.push_back(0xF0);
    }
}

void NativeCodeGen::emitAtomicCas(MemoryOrder successOrder, MemoryOrder failureOrder) {
    // Input: RAX = atomic pointer, RCX = expected, RDX = desired
    // Returns 1 if successful (value was expected), 0 otherwise
    // CMPXCHG [mem], reg: compares [mem] with RAX, if equal stores reg to [mem]
    
    // Save atomic pointer to a temp location
    // We need: RAX = expected, RCX = atomic pointer, RDX = desired
    // Currently: RAX = atomic pointer, RCX = expected, RDX = desired
    
    // Swap RAX and RCX: RAX should have expected, RCX should have atomic pointer
    asm_.push_rax();          // Save atomic pointer
    asm_.mov_rax_rcx();       // RAX = expected
    asm_.pop_rcx();           // RCX = atomic pointer
    
    // Now: RAX = expected, RCX = atomic pointer, RDX = desired
    // LOCK CMPXCHG [rcx], rdx
    // If [rcx] == rax, then [rcx] = rdx and ZF=1
    // If [rcx] != rax, then rax = [rcx] and ZF=0
    asm_.code.push_back(0xF0);  // LOCK prefix
    asm_.code.push_back(0x48);  // REX.W
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0xB1);
    asm_.code.push_back(0x11);  // ModRM: [rcx], rdx
    
    // Set result based on ZF: 1 if equal (success), 0 if not equal (failure)
    // setz al; movzx rax, al
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0x94);
    asm_.code.push_back(0xC0);  // setz al
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0xB6);
    asm_.code.push_back(0xC0);  // movzx rax, al
    
    // Memory fence for SeqCst
    if (successOrder == MemoryOrder::SeqCst || failureOrder == MemoryOrder::SeqCst) {
        asm_.code.push_back(0x0F);
        asm_.code.push_back(0xAE);
        asm_.code.push_back(0xF0);
    }
}

void NativeCodeGen::emitAtomicAdd(MemoryOrder order) {
    // Atomic pointer in RAX, value in RCX
    // Returns old value in RAX
    // Uses LOCK XADD instruction
    
    // LOCK XADD [rax], rcx - atomically adds rcx to [rax], old value goes to rcx
    // F0 48 0F C1 08 = lock xadd [rax], rcx
    asm_.code.push_back(0xF0);  // LOCK prefix
    asm_.code.push_back(0x48);  // REX.W
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0xC1);
    asm_.code.push_back(0x08);  // ModRM: [rax], rcx
    
    // Move old value from RCX to RAX
    asm_.mov_rax_rcx();
    
    // Memory fence for SeqCst
    if (order == MemoryOrder::SeqCst) {
        asm_.code.push_back(0x0F);
        asm_.code.push_back(0xAE);
        asm_.code.push_back(0xF0);
    }
}

void NativeCodeGen::emitAtomicSub(MemoryOrder order) {
    // Atomic pointer in RAX, value in RCX
    // Returns old value in RAX
    // Negate RCX and use XADD
    
    // neg rcx
    asm_.code.push_back(0x48);
    asm_.code.push_back(0xF7);
    asm_.code.push_back(0xD9);
    
    // LOCK XADD [rax], rcx
    asm_.code.push_back(0xF0);
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0xC1);
    asm_.code.push_back(0x08);
    
    // Move old value from RCX to RAX
    asm_.mov_rax_rcx();
    
    if (order == MemoryOrder::SeqCst) {
        asm_.code.push_back(0x0F);
        asm_.code.push_back(0xAE);
        asm_.code.push_back(0xF0);
    }
}

void NativeCodeGen::emitAtomicAnd(MemoryOrder order) {
    // Atomic pointer in RAX, value in RCX
    // Returns old value in RAX
    // Use CAS loop since there's no LOCK AND that returns old value
    
    std::string loopLabel = newLabel("atomic_and_loop");
    
    // Save atomic pointer and mask value
    asm_.push_rax();  // [rsp+8] = atomic pointer
    asm_.push_rcx();  // [rsp+0] = mask value
    
    asm_.label(loopLabel);
    // Load current value from atomic
    asm_.mov_rax_mem_rsp(8);  // rax = atomic pointer
    asm_.mov_rax_mem_rax();   // rax = current value
    asm_.mov_rdx_rax();       // rdx = expected (save current)
    
    // Compute desired = current AND mask
    asm_.mov_rax_mem_rsp(0);  // rax = mask
    asm_.mov_rcx_rax();       // rcx = mask
    asm_.mov_rax_rdx();       // rax = current
    asm_.and_rax_rcx();       // rax = current AND mask (desired)
    asm_.push_rax();          // [rsp+0] = desired, [rsp+8] = mask, [rsp+16] = atomic ptr
    
    // Setup for CMPXCHG: rax = expected, rcx = atomic ptr, rdx = desired
    asm_.mov_rax_rdx();       // rax = expected
    asm_.mov_rax_mem_rsp(16); // rax = atomic pointer
    asm_.mov_rcx_rax();       // rcx = atomic pointer
    asm_.mov_rax_rdx();       // rax = expected (for cmpxchg comparison)
    asm_.mov_rax_mem_rsp(0);  // rax = desired
    asm_.mov_rdx_rax();       // rdx = desired
    asm_.pop_rax();           // pop desired, restore stack
    asm_.push_rdx();          // save desired again
    asm_.mov_rax_mem_rsp(16); // rax = atomic pointer
    asm_.mov_rcx_rax();       // rcx = atomic pointer
    asm_.mov_rax_mem_rsp(8);  // rax = atomic pointer (reload)
    asm_.mov_rax_mem_rax();   // rax = current value (expected)
    asm_.pop_rdx();           // rdx = desired
    
    // LOCK CMPXCHG [rcx], rdx
    asm_.code.push_back(0xF0);  // LOCK
    asm_.code.push_back(0x48);  // REX.W
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0xB1);
    asm_.code.push_back(0x11);  // ModRM: [rcx], rdx
    
    // If ZF=0 (failed), retry
    asm_.jnz_rel32(loopLabel);
    
    // Success - rax contains the old value (from cmpxchg)
    asm_.add_rsp_imm32(16);   // clean up stack
    
    if (order == MemoryOrder::SeqCst) {
        asm_.code.push_back(0x0F);
        asm_.code.push_back(0xAE);
        asm_.code.push_back(0xF0);  // mfence
    }
}

void NativeCodeGen::emitAtomicOr(MemoryOrder order) {
    // Atomic pointer in RAX, value in RCX
    // Returns old value in RAX
    // Use CAS loop
    
    std::string loopLabel = newLabel("atomic_or_loop");
    
    asm_.push_rax();  // [rsp+8] = atomic pointer
    asm_.push_rcx();  // [rsp+0] = or value
    
    asm_.label(loopLabel);
    // Load current value
    asm_.mov_rax_mem_rsp(8);  // rax = atomic pointer
    asm_.mov_rax_mem_rax();   // rax = current value
    asm_.mov_rdx_rax();       // rdx = expected
    
    // Compute desired = current OR value
    asm_.mov_rax_mem_rsp(0);  // rax = or value
    asm_.mov_rcx_rax();       // rcx = or value
    asm_.mov_rax_rdx();       // rax = current
    asm_.or_rax_rcx();        // rax = current OR value (desired)
    asm_.push_rax();          // save desired
    
    // Setup for CMPXCHG
    asm_.mov_rax_mem_rsp(16); // rax = atomic pointer
    asm_.mov_rcx_rax();       // rcx = atomic pointer
    asm_.mov_rax_mem_rax();   // rax = current (expected)
    asm_.pop_rdx();           // rdx = desired
    
    // LOCK CMPXCHG [rcx], rdx
    asm_.code.push_back(0xF0);
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0xB1);
    asm_.code.push_back(0x11);
    
    asm_.jnz_rel32(loopLabel);
    
    asm_.add_rsp_imm32(16);
    
    if (order == MemoryOrder::SeqCst) {
        asm_.code.push_back(0x0F);
        asm_.code.push_back(0xAE);
        asm_.code.push_back(0xF0);
    }
}

void NativeCodeGen::emitAtomicXor(MemoryOrder order) {
    // Atomic pointer in RAX, value in RCX
    // Returns old value in RAX
    // Use CAS loop
    
    std::string loopLabel = newLabel("atomic_xor_loop");
    
    asm_.push_rax();  // [rsp+8] = atomic pointer
    asm_.push_rcx();  // [rsp+0] = xor value
    
    asm_.label(loopLabel);
    // Load current value
    asm_.mov_rax_mem_rsp(8);  // rax = atomic pointer
    asm_.mov_rax_mem_rax();   // rax = current value
    asm_.mov_rdx_rax();       // rdx = expected
    
    // Compute desired = current XOR value
    asm_.mov_rax_mem_rsp(0);  // rax = xor value
    asm_.mov_rcx_rax();       // rcx = xor value
    asm_.mov_rax_rdx();       // rax = current
    // xor rax, rcx
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x31);
    asm_.code.push_back(0xC8);  // xor rax, rcx
    asm_.push_rax();          // save desired
    
    // Setup for CMPXCHG
    asm_.mov_rax_mem_rsp(16); // rax = atomic pointer
    asm_.mov_rcx_rax();       // rcx = atomic pointer
    asm_.mov_rax_mem_rax();   // rax = current (expected)
    asm_.pop_rdx();           // rdx = desired
    
    // LOCK CMPXCHG [rcx], rdx
    asm_.code.push_back(0xF0);
    asm_.code.push_back(0x48);
    asm_.code.push_back(0x0F);
    asm_.code.push_back(0xB1);
    asm_.code.push_back(0x11);
    
    asm_.jnz_rel32(loopLabel);
    
    asm_.add_rsp_imm32(16);
    
    if (order == MemoryOrder::SeqCst) {
        asm_.code.push_back(0x0F);
        asm_.code.push_back(0xAE);
        asm_.code.push_back(0xF0);
    }
}

void NativeCodeGen::emitMemoryFence(MemoryOrder order) {
    switch (order) {
        case MemoryOrder::Relaxed:
            // No fence needed
            break;
        case MemoryOrder::Acquire:
        case MemoryOrder::Release:
        case MemoryOrder::AcqRel:
            // LFENCE for acquire, SFENCE for release
            // For simplicity, use MFENCE for all
            asm_.code.push_back(0x0F);
            asm_.code.push_back(0xAE);
            asm_.code.push_back(0xF0);
            break;
        case MemoryOrder::SeqCst:
            // Full memory fence
            asm_.code.push_back(0x0F);
            asm_.code.push_back(0xAE);
            asm_.code.push_back(0xF0);
            break;
    }
}

void NativeCodeGen::visit(MakeAtomicExpr& node) {
    // Evaluate initial value
    if (node.initialValue) {
        node.initialValue->accept(*this);
        asm_.push_rax();  // Save initial value
        
        // Allocate atomic structure (8 bytes)
        asm_.mov_rcx_imm64(8);
        emitGCAllocRaw(8);
        // RAX now contains pointer to atomic structure
        
        // Store initial value at offset 0
        asm_.pop_rcx();  // Restore initial value
        asm_.mov_mem_rax_rcx(0);
    } else {
        // No initial value, default to 0
        emitAtomicCreate(0);
    }
    
    // Track the variable type if this is being assigned
    // (handled by VarDecl visitor)
}

void NativeCodeGen::visit(AtomicLoadExpr& node) {
    node.atomic->accept(*this);
    emitAtomicLoad(node.order);
}

void NativeCodeGen::visit(AtomicStoreExpr& node) {
    // Evaluate value first
    node.value->accept(*this);
    asm_.push_rax();  // Save value
    
    // Evaluate atomic pointer
    node.atomic->accept(*this);
    
    // Restore value to RCX
    asm_.pop_rcx();
    
    emitAtomicStore(node.order);
}

void NativeCodeGen::visit(AtomicSwapExpr& node) {
    // Evaluate new value first
    node.value->accept(*this);
    asm_.push_rax();  // Save new value
    
    // Evaluate atomic pointer
    node.atomic->accept(*this);
    
    // Restore new value to RCX
    asm_.pop_rcx();
    
    emitAtomicSwap(node.order);
}

void NativeCodeGen::visit(AtomicCasExpr& node) {
    // Evaluate desired value first
    node.desired->accept(*this);
    asm_.push_rax();  // Save desired
    
    // Evaluate expected value
    node.expected->accept(*this);
    asm_.push_rax();  // Save expected
    
    // Evaluate atomic pointer
    node.atomic->accept(*this);
    
    // Restore expected to RCX, desired to RDX
    asm_.pop_rcx();  // expected
    asm_.pop_rdx();  // desired
    
    emitAtomicCas(node.successOrder, node.failureOrder);
}

void NativeCodeGen::visit(AtomicAddExpr& node) {
    // Evaluate value first
    node.value->accept(*this);
    asm_.push_rax();  // Save value
    
    // Evaluate atomic pointer
    node.atomic->accept(*this);
    
    // Restore value to RCX
    asm_.pop_rcx();
    
    emitAtomicAdd(node.order);
}

void NativeCodeGen::visit(AtomicSubExpr& node) {
    // Evaluate value first
    node.value->accept(*this);
    asm_.push_rax();  // Save value
    
    // Evaluate atomic pointer
    node.atomic->accept(*this);
    
    // Restore value to RCX
    asm_.pop_rcx();
    
    emitAtomicSub(node.order);
}

void NativeCodeGen::visit(AtomicAndExpr& node) {
    // Evaluate value first
    node.value->accept(*this);
    asm_.push_rax();  // Save value
    
    // Evaluate atomic pointer
    node.atomic->accept(*this);
    
    // Restore value to RCX
    asm_.pop_rcx();
    
    emitAtomicAnd(node.order);
}

void NativeCodeGen::visit(AtomicOrExpr& node) {
    // Evaluate value first
    node.value->accept(*this);
    asm_.push_rax();  // Save value
    
    // Evaluate atomic pointer
    node.atomic->accept(*this);
    
    // Restore value to RCX
    asm_.pop_rcx();
    
    emitAtomicOr(node.order);
}

void NativeCodeGen::visit(AtomicXorExpr& node) {
    // Evaluate value first
    node.value->accept(*this);
    asm_.push_rax();  // Save value
    
    // Evaluate atomic pointer
    node.atomic->accept(*this);
    
    // Restore value to RCX
    asm_.pop_rcx();
    
    emitAtomicXor(node.order);
}

} // namespace tyl
