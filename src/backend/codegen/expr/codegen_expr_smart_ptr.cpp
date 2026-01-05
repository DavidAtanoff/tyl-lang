// Tyl Compiler - Native Code Generator Smart Pointer Expressions
// Handles: Box, Rc, Arc, Weak, Cell, RefCell creation and operations

#include "backend/codegen/codegen_base.h"

namespace tyl {

// Box::new(value) - Unique ownership heap allocation
// Layout: [value] - just the value on the heap
void NativeCodeGen::visit(MakeBoxExpr& node) {
    // Evaluate the value to box
    node.value->accept(*this);
    asm_.push_rax();  // Save value
    
    // Allocate memory for the boxed value (8 bytes for now - int/ptr size)
    int32_t size = 8;
    if (!node.elementType.empty()) {
        size = getTypeSize(node.elementType);
    }
    
    // Use GC allocation for the box
    emitGCAllocRaw(size);
    
    // Store the value into the allocated memory
    asm_.mov_rcx_rax();  // Box pointer in RCX
    asm_.pop_rax();      // Value in RAX
    asm_.mov_mem_rcx_rax();  // Store value at box pointer
    asm_.mov_rax_rcx();  // Return box pointer in RAX
}

// Rc::new(value) - Reference counted (single-threaded)
// Layout: [refcount: i64][value]
void NativeCodeGen::visit(MakeRcExpr& node) {
    // Evaluate the value
    node.value->accept(*this);
    asm_.push_rax();  // Save value
    
    // Allocate memory: 8 bytes for refcount + value size
    int32_t valueSize = 8;
    if (!node.elementType.empty()) {
        valueSize = getTypeSize(node.elementType);
    }
    int32_t totalSize = 8 + valueSize;  // refcount + value
    
    // Use GC allocation
    emitGCAllocRaw(totalSize);
    
    // Initialize refcount to 1
    asm_.mov_rcx_rax();  // Rc pointer in RCX
    asm_.mov_rax_imm64(1);
    asm_.mov_mem_rcx_rax();  // Store refcount = 1
    
    // Store the value at offset 8
    asm_.pop_rax();  // Value in RAX
    // mov [rcx+8], rax
    asm_.code.push_back(0x48); asm_.code.push_back(0x89);
    asm_.code.push_back(0x41); asm_.code.push_back(0x08);
    
    asm_.mov_rax_rcx();  // Return Rc pointer in RAX
}

// Arc::new(value) - Atomic reference counted (thread-safe)
// Layout: [refcount: atomic i64][value]
void NativeCodeGen::visit(MakeArcExpr& node) {
    // Evaluate the value
    node.value->accept(*this);
    asm_.push_rax();  // Save value
    
    // Allocate memory: 8 bytes for atomic refcount + value size
    int32_t valueSize = 8;
    if (!node.elementType.empty()) {
        valueSize = getTypeSize(node.elementType);
    }
    int32_t totalSize = 8 + valueSize;  // atomic refcount + value
    
    // Use GC allocation
    emitGCAllocRaw(totalSize);
    
    // Initialize atomic refcount to 1 using atomic store
    asm_.mov_rcx_rax();  // Arc pointer in RCX
    asm_.mov_rax_imm64(1);
    // Use xchg for atomic store (implicit lock prefix)
    // xchg [rcx], rax
    asm_.code.push_back(0x48); asm_.code.push_back(0x87);
    asm_.code.push_back(0x01);
    
    // Store the value at offset 8
    asm_.pop_rax();  // Value in RAX
    // mov [rcx+8], rax
    asm_.code.push_back(0x48); asm_.code.push_back(0x89);
    asm_.code.push_back(0x41); asm_.code.push_back(0x08);
    
    asm_.mov_rax_rcx();  // Return Arc pointer in RAX
}

// Weak reference creation (from Rc or Arc)
// Layout: [weak_count: i64][strong_ptr: *Rc/*Arc]
void NativeCodeGen::visit(MakeWeakExpr& node) {
    // Evaluate the source Rc/Arc
    node.source->accept(*this);
    asm_.push_rax();  // Save source pointer
    
    // Allocate memory for weak reference: 8 bytes weak_count + 8 bytes pointer
    emitGCAllocRaw(16);
    
    // Initialize weak_count to 1
    asm_.mov_rcx_rax();  // Weak pointer in RCX
    asm_.mov_rax_imm64(1);
    asm_.mov_mem_rcx_rax();  // Store weak_count = 1
    
    // Store the source pointer at offset 8
    asm_.pop_rax();  // Source Rc/Arc pointer in RAX
    // mov [rcx+8], rax
    asm_.code.push_back(0x48); asm_.code.push_back(0x89);
    asm_.code.push_back(0x41); asm_.code.push_back(0x08);
    
    asm_.mov_rax_rcx();  // Return Weak pointer in RAX
}

// Cell::new(value) - Interior mutability (single-threaded, Copy types)
// Layout: [value] - just the value, but allows mutation through shared reference
void NativeCodeGen::visit(MakeCellExpr& node) {
    // Evaluate the value
    node.value->accept(*this);
    asm_.push_rax();  // Save value
    
    // Allocate memory for the cell value
    int32_t size = 8;
    if (!node.elementType.empty()) {
        size = getTypeSize(node.elementType);
    }
    
    // Use GC allocation
    emitGCAllocRaw(size);
    
    // Store the value
    asm_.mov_rcx_rax();  // Cell pointer in RCX
    asm_.pop_rax();      // Value in RAX
    asm_.mov_mem_rcx_rax();  // Store value
    asm_.mov_rax_rcx();  // Return Cell pointer in RAX
}

// RefCell::new(value) - Runtime borrow checking
// Layout: [borrow_state: i64][value]
// borrow_state: 0 = not borrowed, >0 = shared borrow count, -1 = mutably borrowed
void NativeCodeGen::visit(MakeRefCellExpr& node) {
    // Evaluate the value
    node.value->accept(*this);
    asm_.push_rax();  // Save value
    
    // Allocate memory: 8 bytes for borrow state + value size
    int32_t valueSize = 8;
    if (!node.elementType.empty()) {
        valueSize = getTypeSize(node.elementType);
    }
    int32_t totalSize = 8 + valueSize;  // borrow_state + value
    
    // Use GC allocation
    emitGCAllocRaw(totalSize);
    
    // Initialize borrow_state to 0 (not borrowed)
    asm_.mov_rcx_rax();  // RefCell pointer in RCX
    asm_.xor_rax_rax();  // RAX = 0
    asm_.mov_mem_rcx_rax();  // Store borrow_state = 0
    
    // Store the value at offset 8
    asm_.pop_rax();  // Value in RAX
    // mov [rcx+8], rax
    asm_.code.push_back(0x48); asm_.code.push_back(0x89);
    asm_.code.push_back(0x41); asm_.code.push_back(0x08);
    
    asm_.mov_rax_rcx();  // Return RefCell pointer in RAX
}

// ============================================================================
// Smart Pointer Helper Methods
// ============================================================================

// Box dereference - get the value from a Box
// Input: RAX = Box pointer
// Output: RAX = value
void NativeCodeGen::emitBoxDeref() {
    // Box layout: [value] - value is at offset 0
    asm_.mov_rax_mem_rax();  // Load value from [rax]
}

// Rc dereference - get the value from an Rc
// Input: RAX = Rc pointer
// Output: RAX = value
void NativeCodeGen::emitRcDeref() {
    // Rc layout: [refcount: i64][value] - value is at offset 8
    // mov rax, [rax+8]
    asm_.code.push_back(0x48); asm_.code.push_back(0x8B);
    asm_.code.push_back(0x40); asm_.code.push_back(0x08);
}

// Arc dereference - get the value from an Arc
// Input: RAX = Arc pointer
// Output: RAX = value
void NativeCodeGen::emitArcDeref() {
    // Arc layout: [refcount: atomic i64][value] - value is at offset 8
    // mov rax, [rax+8]
    asm_.code.push_back(0x48); asm_.code.push_back(0x8B);
    asm_.code.push_back(0x40); asm_.code.push_back(0x08);
}

// Rc clone - increment refcount and return same pointer
// Input: RAX = Rc pointer
// Output: RAX = same Rc pointer (with incremented refcount)
void NativeCodeGen::emitRcClone() {
    // Rc layout: [refcount: i64][value]
    // Increment refcount at offset 0
    asm_.mov_rcx_rax();  // Save Rc pointer in RCX
    // inc qword [rcx]
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF);
    asm_.code.push_back(0x01);
    asm_.mov_rax_rcx();  // Return same pointer
}

// Arc clone - atomically increment refcount and return same pointer
// Input: RAX = Arc pointer
// Output: RAX = same Arc pointer (with atomically incremented refcount)
void NativeCodeGen::emitArcClone() {
    // Arc layout: [refcount: atomic i64][value]
    // Atomic increment refcount at offset 0
    asm_.mov_rcx_rax();  // Save Arc pointer in RCX
    asm_.mov_rax_imm64(1);
    // lock xadd [rcx], rax - atomic fetch-and-add
    asm_.code.push_back(0xF0);  // LOCK prefix
    asm_.code.push_back(0x48); asm_.code.push_back(0x0F);
    asm_.code.push_back(0xC1); asm_.code.push_back(0x01);
    asm_.mov_rax_rcx();  // Return same pointer
}

// Weak upgrade - try to convert Weak to Rc/Arc
// Input: RAX = Weak pointer
// Output: RAX = Rc/Arc pointer or 0 (nil) if deallocated
void NativeCodeGen::emitWeakUpgrade() {
    // Weak layout: [weak_count: i64][strong_ptr: *Rc/*Arc]
    // Load the strong pointer at offset 8
    // mov rax, [rax+8]
    asm_.code.push_back(0x48); asm_.code.push_back(0x8B);
    asm_.code.push_back(0x40); asm_.code.push_back(0x08);
    
    // Check if the strong pointer is nil (deallocated)
    asm_.test_rax_rax();
    std::string nilLabel = newLabel("weak_upgrade_nil");
    std::string endLabel = newLabel("weak_upgrade_end");
    asm_.jz_rel32(nilLabel);
    
    // Not nil - check if refcount > 0
    asm_.mov_rcx_rax();  // Save Rc/Arc pointer
    asm_.mov_rax_mem_rax();  // Load refcount
    asm_.test_rax_rax();
    asm_.jz_rel32(nilLabel);  // If refcount is 0, return nil
    
    // Refcount > 0 - increment it and return the pointer
    // inc qword [rcx]
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF);
    asm_.code.push_back(0x01);
    asm_.mov_rax_rcx();  // Return Rc/Arc pointer
    asm_.jmp_rel32(endLabel);
    
    asm_.label(nilLabel);
    asm_.xor_rax_rax();  // Return nil (0)
    
    asm_.label(endLabel);
}

// Weak downgrade - create a Weak reference from Rc/Arc
// Input: RAX = Rc/Arc pointer
// Output: RAX = Weak pointer
void NativeCodeGen::emitWeakDowngrade(bool isAtomic) {
    asm_.push_rax();  // Save Rc/Arc pointer
    
    // Allocate memory for Weak: 8 bytes weak_count + 8 bytes pointer
    emitGCAllocRaw(16);
    
    // Initialize weak_count to 1
    asm_.mov_rcx_rax();  // Weak pointer in RCX
    asm_.mov_rax_imm64(1);
    asm_.mov_mem_rcx_rax();  // Store weak_count = 1
    
    // Store the source pointer at offset 8
    asm_.pop_rax();  // Source Rc/Arc pointer in RAX
    // mov [rcx+8], rax
    asm_.code.push_back(0x48); asm_.code.push_back(0x89);
    asm_.code.push_back(0x41); asm_.code.push_back(0x08);
    
    asm_.mov_rax_rcx();  // Return Weak pointer in RAX
}

// Cell get - get a copy of the value
// Input: RAX = Cell pointer
// Output: RAX = value
void NativeCodeGen::emitCellGet() {
    // Cell layout: [value] - value is at offset 0
    asm_.mov_rax_mem_rax();  // Load value from [rax]
}

// Cell set - set the value
// Input: RAX = Cell pointer, RCX = new value
// Output: none
void NativeCodeGen::emitCellSet() {
    // Cell layout: [value] - value is at offset 0
    // mov [rax], rcx
    asm_.code.push_back(0x48); asm_.code.push_back(0x89);
    asm_.code.push_back(0x08);
}

// RefCell borrow - get an immutable reference
// Input: RAX = RefCell pointer
// Output: RAX = pointer to value (at offset 8)
void NativeCodeGen::emitRefCellBorrow() {
    // RefCell layout: [borrow_state: i64][value]
    // borrow_state: 0 = not borrowed, >0 = shared borrow count, -1 = mutably borrowed
    
    asm_.mov_rcx_rax();  // Save RefCell pointer in RCX
    
    // Check borrow state
    asm_.mov_rax_mem_rcx();  // Load borrow_state
    
    // If borrow_state == -1, panic (already mutably borrowed)
    asm_.cmp_rax_imm8(-1);
    std::string panicLabel = newLabel("refcell_borrow_panic");
    std::string okLabel = newLabel("refcell_borrow_ok");
    asm_.je_rel32(panicLabel);
    
    // Increment borrow count
    // inc qword [rcx]
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF);
    asm_.code.push_back(0x01);
    
    // Return pointer to value (offset 8)
    // lea rax, [rcx+8]
    asm_.code.push_back(0x48); asm_.code.push_back(0x8D);
    asm_.code.push_back(0x41); asm_.code.push_back(0x08);
    asm_.jmp_rel32(okLabel);
    
    // Panic: already mutably borrowed
    asm_.label(panicLabel);
    // For now, just return nil to indicate error
    // In a full implementation, this would call a panic function
    asm_.xor_rax_rax();
    
    asm_.label(okLabel);
}

// RefCell borrow_mut - get a mutable reference
// Input: RAX = RefCell pointer
// Output: RAX = pointer to value (at offset 8)
void NativeCodeGen::emitRefCellBorrowMut() {
    // RefCell layout: [borrow_state: i64][value]
    // borrow_state: 0 = not borrowed, >0 = shared borrow count, -1 = mutably borrowed
    
    asm_.mov_rcx_rax();  // Save RefCell pointer in RCX
    
    // Check borrow state - must be 0 (not borrowed)
    asm_.mov_rax_mem_rcx();  // Load borrow_state
    asm_.test_rax_rax();
    std::string panicLabel = newLabel("refcell_borrow_mut_panic");
    std::string okLabel = newLabel("refcell_borrow_mut_ok");
    asm_.jnz_rel32(panicLabel);  // If not 0, panic
    
    // Set borrow_state to -1 (mutably borrowed)
    asm_.mov_rax_imm64(-1);
    asm_.mov_mem_rcx_rax();
    
    // Return pointer to value (offset 8)
    // lea rax, [rcx+8]
    asm_.code.push_back(0x48); asm_.code.push_back(0x8D);
    asm_.code.push_back(0x41); asm_.code.push_back(0x08);
    asm_.jmp_rel32(okLabel);
    
    // Panic: already borrowed
    asm_.label(panicLabel);
    // For now, just return nil to indicate error
    asm_.xor_rax_rax();
    
    asm_.label(okLabel);
}

// RefCell release - release a borrow
// Input: RAX = RefCell pointer
// Output: none
void NativeCodeGen::emitRefCellRelease() {
    // RefCell layout: [borrow_state: i64][value]
    asm_.mov_rcx_rax();  // Save RefCell pointer in RCX
    asm_.mov_rax_mem_rcx();  // Load borrow_state
    
    // If borrow_state == -1, set to 0 (release mutable borrow)
    asm_.cmp_rax_imm8(-1);
    std::string sharedLabel = newLabel("refcell_release_shared");
    std::string endLabel = newLabel("refcell_release_end");
    asm_.jne_rel32(sharedLabel);
    
    // Release mutable borrow
    asm_.xor_rax_rax();
    asm_.mov_mem_rcx_rax();
    asm_.jmp_rel32(endLabel);
    
    // Release shared borrow (decrement count)
    asm_.label(sharedLabel);
    // dec qword [rcx]
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF);
    asm_.code.push_back(0x09);
    
    asm_.label(endLabel);
}

// Box drop - deallocate a Box
// Input: RAX = Box pointer
// Output: none
void NativeCodeGen::emitBoxDrop() {
    // For GC-allocated memory, we don't need to explicitly free
    // The GC will collect it when there are no more references
    // This is a no-op for now
}

// Rc drop - decrement refcount and deallocate if 0
// Input: RAX = Rc pointer
// Output: none
void NativeCodeGen::emitRcDrop() {
    // Rc layout: [refcount: i64][value]
    asm_.mov_rcx_rax();  // Save Rc pointer in RCX
    
    // Decrement refcount
    // dec qword [rcx]
    asm_.code.push_back(0x48); asm_.code.push_back(0xFF);
    asm_.code.push_back(0x09);
    
    // Check if refcount is now 0
    asm_.mov_rax_mem_rcx();  // Load new refcount
    asm_.test_rax_rax();
    std::string endLabel = newLabel("rc_drop_end");
    asm_.jnz_rel32(endLabel);  // If not 0, don't deallocate
    
    // Refcount is 0 - GC will collect it
    // For now, we don't explicitly free (GC handles it)
    
    asm_.label(endLabel);
}

// Arc drop - atomically decrement refcount and deallocate if 0
// Input: RAX = Arc pointer
// Output: none
void NativeCodeGen::emitArcDrop() {
    // Arc layout: [refcount: atomic i64][value]
    asm_.mov_rcx_rax();  // Save Arc pointer in RCX
    
    // Atomic decrement refcount
    asm_.mov_rax_imm64(-1);
    // lock xadd [rcx], rax - atomic fetch-and-add (-1)
    asm_.code.push_back(0xF0);  // LOCK prefix
    asm_.code.push_back(0x48); asm_.code.push_back(0x0F);
    asm_.code.push_back(0xC1); asm_.code.push_back(0x01);
    
    // RAX now contains the old refcount
    // If old refcount was 1, it's now 0 and we should deallocate
    asm_.cmp_rax_imm8(1);
    std::string endLabel = newLabel("arc_drop_end");
    asm_.jne_rel32(endLabel);  // If old count wasn't 1, don't deallocate
    
    // Old refcount was 1, now 0 - GC will collect it
    // For now, we don't explicitly free (GC handles it)
    
    asm_.label(endLabel);
}

} // namespace tyl
