// Tyl Compiler - Native Code Generator Advanced Concurrency Expressions
// Handles: Future/Promise, Thread Pool, Select, Timeout, Cancellation

#include "backend/codegen/codegen_base.h"

namespace tyl {

// Future structure: mutex(8) + event(8) + value(8) + is_ready(8) + is_error(8) = 40 bytes

void NativeCodeGen::emitFutureCreate(int32_t elementSize) {
    (void)elementSize;
    asm_.mov_rcx_imm64(40);
    emitGCAllocRaw(40);
    asm_.push_rax();
    
    asm_.xor_rcx_rcx();
    asm_.xor_rdx_rdx();
    asm_.xor_r8_r8();
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("CreateMutexA"));
    asm_.add_rsp_imm32(0x28);
    asm_.mov_rcx_rax();
    asm_.mov_rax_mem_rsp(0);
    asm_.mov_mem_rax_rcx(0);
    
    asm_.xor_rcx_rcx();
    asm_.mov_edx_imm32(1);
    asm_.xor_r8_r8();
    asm_.xor_r9_r9();
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("CreateEventA"));
    asm_.add_rsp_imm32(0x28);
    asm_.mov_rcx_rax();
    asm_.mov_rax_mem_rsp(0);
    asm_.mov_mem_rax_rcx(8);
    
    asm_.mov_rax_mem_rsp(0);
    asm_.xor_rcx_rcx();
    asm_.mov_mem_rax_rcx(16);
    asm_.mov_mem_rax_rcx(24);
    asm_.mov_mem_rax_rcx(32);
    asm_.pop_rax();
}

void NativeCodeGen::emitFutureGet() {
    asm_.push_rax();
    asm_.sub_rsp_imm32(8);
    asm_.mov_rax_mem_rsp(8);
    asm_.mov_rcx_mem_rax(8);
    asm_.mov_rdx_imm64(0xFFFFFFFF);
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("WaitForSingleObject"));
    asm_.add_rsp_imm32(0x28);
    asm_.mov_rax_mem_rsp(8);
    asm_.mov_rcx_mem_rax(16);
    asm_.mov_rax_rcx();
    asm_.add_rsp_imm32(16);
}

void NativeCodeGen::emitFutureSet() {
    // Entry: RAX = future pointer, RCX = value to set
    // Stack layout after pushes: RSP+0 = value, RSP+8 = future
    asm_.push_rax();           // push future
    asm_.push_rcx();           // push value
    
    // Lock the mutex: WaitForSingleObject(future->mutex, INFINITE)
    asm_.mov_rax_mem_rsp(8);   // RAX = future
    asm_.mov_rcx_mem_rax(0);   // RCX = future->mutex
    asm_.mov_rdx_imm64(0xFFFFFFFF);  // INFINITE
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("WaitForSingleObject"));
    asm_.add_rsp_imm32(0x28);
    
    // Store value: future->value = value
    asm_.mov_rax_mem_rsp(0);   // RAX = value
    asm_.mov_rcx_rax();        // RCX = value
    asm_.mov_rax_mem_rsp(8);   // RAX = future
    asm_.mov_mem_rax_rcx(16);  // future->value = value
    
    // Set ready flag: future->is_ready = 1
    asm_.mov_rcx_imm64(1);
    asm_.mov_mem_rax_rcx(24);  // future->is_ready = 1
    
    // Signal event: SetEvent(future->event)
    asm_.mov_rcx_mem_rax(8);   // RCX = future->event
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("SetEvent"));
    asm_.add_rsp_imm32(0x28);
    
    // Release mutex: ReleaseMutex(future->mutex)
    asm_.mov_rax_mem_rsp(8);   // RAX = future
    asm_.mov_rcx_mem_rax(0);   // RCX = future->mutex
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("ReleaseMutex"));
    asm_.add_rsp_imm32(0x28);
    
    asm_.add_rsp_imm32(16);    // clean up stack (pop value and future)
}

void NativeCodeGen::emitFutureIsReady() {
    asm_.mov_rcx_mem_rax(24);
    asm_.mov_rax_rcx();
}

void NativeCodeGen::visit(MakeFutureExpr& node) {
    int32_t elemSize = getTypeSize(node.elementType);
    if (elemSize == 0) elemSize = 8;
    emitFutureCreate(elemSize);
}

void NativeCodeGen::visit(FutureGetExpr& node) {
    node.future->accept(*this);
    emitFutureGet();
}

void NativeCodeGen::visit(FutureSetExpr& node) {
    node.value->accept(*this);
    asm_.push_rax();
    node.future->accept(*this);
    asm_.pop_rcx();
    emitFutureSet();
}

void NativeCodeGen::visit(FutureIsReadyExpr& node) {
    node.future->accept(*this);
    emitFutureIsReady();
}


// Thread Pool: mutex(8) + event(8) + shutdown(8) + num_workers(8) = 32 bytes

void NativeCodeGen::emitThreadPoolCreate(int64_t numWorkers) {
    asm_.mov_rcx_imm64(32);
    emitGCAllocRaw(32);
    asm_.push_rax();
    
    asm_.xor_rcx_rcx();
    asm_.xor_rdx_rdx();
    asm_.xor_r8_r8();
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("CreateMutexA"));
    asm_.add_rsp_imm32(0x28);
    asm_.mov_rcx_rax();
    asm_.mov_rax_mem_rsp(0);
    asm_.mov_mem_rax_rcx(0);
    
    asm_.xor_rcx_rcx();
    asm_.xor_rdx_rdx();
    asm_.xor_r8_r8();
    asm_.xor_r9_r9();
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("CreateEventA"));
    asm_.add_rsp_imm32(0x28);
    asm_.mov_rcx_rax();
    asm_.mov_rax_mem_rsp(0);
    asm_.mov_mem_rax_rcx(8);
    
    asm_.mov_rax_mem_rsp(0);
    asm_.xor_rcx_rcx();
    asm_.mov_mem_rax_rcx(16);
    asm_.mov_rcx_imm64(numWorkers);
    asm_.mov_mem_rax_rcx(24);
    asm_.pop_rax();
}

void NativeCodeGen::emitThreadPoolSubmit() {
    asm_.push_rax();
    asm_.push_rcx();
    asm_.mov_rax_rcx();
    asm_.sub_rsp_imm32(0x28);
    asm_.call_rax();
    asm_.add_rsp_imm32(0x28);
    asm_.add_rsp_imm32(16);
    asm_.xor_rax_rax();
}

void NativeCodeGen::emitThreadPoolShutdown() {
    asm_.push_rax();
    asm_.sub_rsp_imm32(8);
    asm_.mov_rax_mem_rsp(8);
    asm_.mov_rcx_mem_rax(0);
    asm_.mov_rdx_imm64(0xFFFFFFFF);
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("WaitForSingleObject"));
    asm_.add_rsp_imm32(0x28);
    asm_.mov_rax_mem_rsp(8);
    asm_.mov_rcx_imm64(1);
    asm_.mov_mem_rax_rcx(16);
    asm_.mov_rcx_mem_rax(8);
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("SetEvent"));
    asm_.add_rsp_imm32(0x28);
    asm_.mov_rax_mem_rsp(8);
    asm_.mov_rcx_mem_rax(0);
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("ReleaseMutex"));
    asm_.add_rsp_imm32(0x28);
    asm_.add_rsp_imm32(16);
}

void NativeCodeGen::visit(MakeThreadPoolExpr& node) {
    int64_t numWorkers = 4;
    if (node.numWorkers) {
        if (auto* lit = dynamic_cast<IntegerLiteral*>(node.numWorkers.get())) {
            numWorkers = lit->value;
        }
    }
    emitThreadPoolCreate(numWorkers);
}

void NativeCodeGen::visit(ThreadPoolSubmitExpr& node) {
    node.task->accept(*this);
    asm_.push_rax();
    node.pool->accept(*this);
    asm_.pop_rcx();
    emitThreadPoolSubmit();
}

void NativeCodeGen::visit(ThreadPoolShutdownExpr& node) {
    node.pool->accept(*this);
    emitThreadPoolShutdown();
}

void NativeCodeGen::visit(SelectExpr& node) {
    if (node.cases.empty()) {
        asm_.xor_rax_rax();
        return;
    }
    if (!node.cases[0].isSend) {
        node.cases[0].channel->accept(*this);
        emitChannelRecv();
    } else {
        node.cases[0].value->accept(*this);
        asm_.push_rax();
        node.cases[0].channel->accept(*this);
        asm_.mov_rcx_rax();
        asm_.pop_rax();
        asm_.xchg_rax_rcx();
        emitChannelSend();
    }
    if (node.cases[0].body) {
        node.cases[0].body->accept(*this);
    }
}

void NativeCodeGen::emitChannelRecvTimeout(int64_t timeoutMs) {
    (void)timeoutMs;
    emitChannelRecv();
}

void NativeCodeGen::visit(TimeoutExpr& node) {
    node.operation->accept(*this);
}

void NativeCodeGen::visit(ChanRecvTimeoutExpr& node) {
    int64_t timeoutMs = 1000;
    if (auto* lit = dynamic_cast<IntegerLiteral*>(node.timeoutMs.get())) {
        timeoutMs = lit->value;
    }
    node.channel->accept(*this);
    emitChannelRecvTimeout(timeoutMs);
}

void NativeCodeGen::visit(ChanSendTimeoutExpr& node) {
    node.value->accept(*this);
    asm_.push_rax();
    node.channel->accept(*this);
    asm_.mov_rcx_rax();
    asm_.pop_rax();
    asm_.xchg_rax_rcx();
    emitChannelSend();
}


// CancelToken: cancelled(8) + event(8) = 16 bytes

void NativeCodeGen::emitCancelTokenCreate() {
    asm_.mov_rcx_imm64(16);
    emitGCAllocRaw(16);
    asm_.push_rax();
    asm_.xor_rcx_rcx();
    asm_.mov_mem_rax_rcx(0);
    
    asm_.xor_rcx_rcx();
    asm_.mov_edx_imm32(1);
    asm_.xor_r8_r8();
    asm_.xor_r9_r9();
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("CreateEventA"));
    asm_.add_rsp_imm32(0x28);
    asm_.mov_rcx_rax();
    asm_.mov_rax_mem_rsp(0);
    asm_.mov_mem_rax_rcx(8);
    asm_.pop_rax();
}

void NativeCodeGen::emitCancel() {
    asm_.push_rax();
    asm_.sub_rsp_imm32(8);
    asm_.mov_rax_mem_rsp(8);
    asm_.mov_rcx_imm64(1);
    asm_.mov_mem_rax_rcx(0);
    asm_.mov_rcx_mem_rax(8);
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("SetEvent"));
    asm_.add_rsp_imm32(0x28);
    asm_.add_rsp_imm32(16);
}

void NativeCodeGen::emitIsCancelled() {
    asm_.mov_rcx_mem_rax(0);
    asm_.mov_rax_rcx();
}

void NativeCodeGen::visit(MakeCancelTokenExpr& node) {
    (void)node;
    emitCancelTokenCreate();
}

void NativeCodeGen::visit(CancelExpr& node) {
    node.token->accept(*this);
    emitCancel();
}

void NativeCodeGen::visit(IsCancelledExpr& node) {
    node.token->accept(*this);
    emitIsCancelled();
}

// ============================================================================
// Async Runtime - Event Loop and Task Management
// ============================================================================

// AsyncRuntime structure:
// - mutex(8): protects task queue
// - event(8): signals new tasks available
// - shutdown(8): shutdown flag
// - num_workers(8): number of worker threads
// - task_queue_head(8): head of task linked list
// - task_queue_tail(8): tail of task linked list
// - active_tasks(8): count of active tasks
// Total: 56 bytes

void NativeCodeGen::emitAsyncRuntimeInit(int64_t numWorkers) {
    // Allocate runtime structure
    asm_.mov_rcx_imm64(56);
    emitGCAllocRaw(56);
    asm_.push_rax();
    
    // Create mutex for task queue protection
    asm_.xor_rcx_rcx();
    asm_.xor_rdx_rdx();
    asm_.xor_r8_r8();
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("CreateMutexA"));
    asm_.add_rsp_imm32(0x28);
    asm_.mov_rcx_rax();
    asm_.mov_rax_mem_rsp(0);
    asm_.mov_mem_rax_rcx(0);  // runtime->mutex
    
    // Create event for task notification (auto-reset)
    asm_.xor_rcx_rcx();
    asm_.xor_rdx_rdx();  // auto-reset event
    asm_.xor_r8_r8();
    asm_.xor_r9_r9();
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("CreateEventA"));
    asm_.add_rsp_imm32(0x28);
    asm_.mov_rcx_rax();
    asm_.mov_rax_mem_rsp(0);
    asm_.mov_mem_rax_rcx(8);  // runtime->event
    
    // Initialize other fields
    asm_.mov_rax_mem_rsp(0);
    asm_.xor_rcx_rcx();
    asm_.mov_mem_rax_rcx(16);  // runtime->shutdown = 0
    asm_.mov_rcx_imm64(numWorkers);
    asm_.mov_mem_rax_rcx(24);  // runtime->num_workers
    asm_.xor_rcx_rcx();
    asm_.mov_mem_rax_rcx(32);  // runtime->task_queue_head = null
    asm_.mov_mem_rax_rcx(40);  // runtime->task_queue_tail = null
    asm_.mov_mem_rax_rcx(48);  // runtime->active_tasks = 0
    
    asm_.pop_rax();
}

void NativeCodeGen::emitAsyncRuntimeRun() {
    // Simple synchronous event loop - process all queued tasks
    // In a full implementation, this would spawn worker threads
    // For now, we just return (tasks are executed inline when spawned)
    asm_.xor_rax_rax();
}

void NativeCodeGen::emitAsyncRuntimeShutdown() {
    // Set shutdown flag and signal all workers
    asm_.push_rax();
    asm_.sub_rsp_imm32(8);
    
    // Lock mutex
    asm_.mov_rax_mem_rsp(8);
    asm_.mov_rcx_mem_rax(0);  // mutex
    asm_.mov_rdx_imm64(0xFFFFFFFF);
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("WaitForSingleObject"));
    asm_.add_rsp_imm32(0x28);
    
    // Set shutdown flag
    asm_.mov_rax_mem_rsp(8);
    asm_.mov_rcx_imm64(1);
    asm_.mov_mem_rax_rcx(16);  // runtime->shutdown = 1
    
    // Signal event to wake up any waiting workers
    asm_.mov_rcx_mem_rax(8);  // event
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("SetEvent"));
    asm_.add_rsp_imm32(0x28);
    
    // Release mutex
    asm_.mov_rax_mem_rsp(8);
    asm_.mov_rcx_mem_rax(0);  // mutex
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("ReleaseMutex"));
    asm_.add_rsp_imm32(0x28);
    
    asm_.add_rsp_imm32(16);
}

void NativeCodeGen::emitAsyncSpawn() {
    // For now, execute the task synchronously and wrap result in a future
    // RAX = task function pointer
    // In a full implementation, this would queue the task for async execution
    
    asm_.push_rax();  // save task
    
    // Create a future for the result
    emitFutureCreate(8);
    asm_.push_rax();  // save future
    
    // Execute the task
    asm_.mov_rax_mem_rsp(8);  // get task
    asm_.sub_rsp_imm32(0x28);
    asm_.call_rax();
    asm_.add_rsp_imm32(0x28);
    
    // Store result in future
    asm_.mov_rcx_rax();  // RCX = result
    asm_.mov_rax_mem_rsp(0);  // RAX = future
    emitFutureSet();
    
    // Return the future
    asm_.mov_rax_mem_rsp(0);
    asm_.add_rsp_imm32(16);
}

void NativeCodeGen::emitAsyncSleep(int64_t durationMs) {
    // Call Windows Sleep function
    asm_.mov_rcx_imm64(durationMs);
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("Sleep"));
    asm_.add_rsp_imm32(0x28);
    asm_.xor_rax_rax();
}

void NativeCodeGen::emitAsyncYield() {
    // Yield to other threads using SwitchToThread or Sleep(0)
    asm_.xor_rcx_rcx();  // Sleep(0) yields to other threads
    asm_.sub_rsp_imm32(0x28);
    asm_.call_mem_rip(pe_.getImportRVA("Sleep"));
    asm_.add_rsp_imm32(0x28);
    asm_.xor_rax_rax();
}

void NativeCodeGen::visit(AsyncRuntimeInitExpr& node) {
    int64_t numWorkers = 4;  // default
    if (node.numWorkers) {
        if (auto* lit = dynamic_cast<IntegerLiteral*>(node.numWorkers.get())) {
            numWorkers = lit->value;
        }
    }
    emitAsyncRuntimeInit(numWorkers);
}

void NativeCodeGen::visit(AsyncRuntimeRunExpr& node) {
    (void)node;
    emitAsyncRuntimeRun();
}

void NativeCodeGen::visit(AsyncRuntimeShutdownExpr& node) {
    (void)node;
    emitAsyncRuntimeShutdown();
}

void NativeCodeGen::visit(AsyncSpawnExpr& node) {
    node.task->accept(*this);
    emitAsyncSpawn();
}

void NativeCodeGen::visit(AsyncSleepExpr& node) {
    int64_t durationMs = 1000;  // default 1 second
    if (auto* lit = dynamic_cast<IntegerLiteral*>(node.durationMs.get())) {
        durationMs = lit->value;
    } else {
        // Dynamic duration - evaluate expression
        node.durationMs->accept(*this);
        asm_.mov_rcx_rax();
        asm_.sub_rsp_imm32(0x28);
        asm_.call_mem_rip(pe_.getImportRVA("Sleep"));
        asm_.add_rsp_imm32(0x28);
        asm_.xor_rax_rax();
        return;
    }
    emitAsyncSleep(durationMs);
}

void NativeCodeGen::visit(AsyncYieldExpr& node) {
    (void)node;
    emitAsyncYield();
}

} // namespace tyl
