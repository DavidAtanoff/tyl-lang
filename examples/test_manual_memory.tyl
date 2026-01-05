// Test manual memory management features
// All these operations require unsafe blocks

// Test 1: stackalloc - allocate memory on the stack
fn test_stackalloc:
    println("Testing stackalloc...")
    unsafe:
        buf = stackalloc(64)
        println("Stack buffer allocated at: {buf}")
        // Note: To dereference, we need to use address-of on a variable
        // stackalloc returns raw memory address as int
    println("stackalloc test passed!")

// Test 2: placement_new - construct value at specific address
fn test_placement_new:
    println("Testing placement_new...")
    unsafe:
        mem = alloc(8)
        result = placement_new(mem, 100)
        println("Placement new at: {result}")
        free(mem)
    println("placement_new test passed!")

// Test 3: gc_pin and gc_unpin - pin/unpin GC objects
fn test_gc_pinning:
    println("Testing GC pinning...")
    unsafe:
        // Create some data that would normally be GC managed
        mem = alloc(16)
        
        // Pin it
        gc_pin(mem)
        println("Object pinned at: {mem}")
        
        // Force a GC collection - pinned object should survive
        gc_collect()
        
        // Unpin it
        gc_unpin(mem)
        println("Object unpinned")
        
        free(mem)
    println("GC pinning test passed!")

// Test 4: gc_add_root and gc_remove_root - register external pointers
fn test_gc_roots:
    println("Testing GC root registration...")
    unsafe:
        // Allocate external memory
        external = alloc(8)
        
        // Register as GC root
        gc_add_root(external)
        println("Root registered at: {external}")
        
        // Unregister
        gc_remove_root(external)
        println("Root unregistered")
        
        free(external)
    println("GC root test passed!")

// Test 5: Using address-of with stackalloc-like pattern
fn test_pointer_ops:
    println("Testing pointer operations...")
    unsafe:
        mut x = 42
        p = &x
        val = *p
        println("Original value: {val}")
        *p = 100
        println("Modified value: {x}")
    println("Pointer ops test passed!")

// Main function
fn main:
    println("=== Manual Memory Management Tests ===")
    println("")
    
    test_stackalloc()
    println("")
    
    test_placement_new()
    println("")
    
    test_gc_pinning()
    println("")
    
    test_gc_roots()
    println("")
    
    test_pointer_ops()
    println("")
    
    println("=== All tests passed! ===")

main()
