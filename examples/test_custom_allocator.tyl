// Test custom allocators
// Demonstrates the custom allocator API

// Query initial allocator stats
println("Initial allocator stats:")
println("  Total allocated: ", allocator_stats())

// Allocate some memory using default allocator
unsafe:
    ptr1 = alloc(1024)
    ptr2 = alloc(2048)
    
    println("After allocations:")
    println("  Total allocated: ", allocator_stats())
    
    // Free memory
    free(ptr1)
    free(ptr2)
    
    println("After frees:")
    println("  Total allocated: ", allocator_stats())

// Note: Custom allocator functions would be defined like this:
// fn my_alloc size, alignment:
//     // Custom allocation logic
//     return alloc(size)
// 
// fn my_free ptr, size:
//     // Custom deallocation logic
//     free(ptr)
//
// unsafe:
//     set_allocator(&my_alloc, &my_free)
//     // ... use custom allocator ...
//     reset_allocator()

println("Custom allocator test complete!")
