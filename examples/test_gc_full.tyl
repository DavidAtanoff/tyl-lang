// Test full garbage collection implementation
// GC is automatic by default, like Rust

// Test 1: Basic allocation tracking
println "=== GC Test Suite ==="
println ""

// Check initial state
println "Initial GC stats:"
println "  Allocated bytes: " + str gc_stats()
println "  Collection count: " + str gc_count()
println "  Threshold: " + str gc_threshold()
println ""

// Test 2: Create some allocations
println "Creating allocations..."
let list1 = [1, 2, 3, 4, 5]
let list2 = [10, 20, 30, 40, 50]
let list3 = [100, 200, 300]

println "After creating 3 lists:"
println "  Allocated bytes: " + str gc_stats()
println ""

// Test 3: Manual collection
println "Forcing garbage collection..."
gc_collect()
println "After gc_collect():"
println "  Allocated bytes: " + str gc_stats()
println "  Collection count: " + str gc_count()
println ""

// Test 4: Disable/enable GC
println "Testing GC disable/enable..."
gc_disable()
println "GC disabled"

// Create more allocations (won't trigger auto-collection)
let list4 = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
println "Created list4, allocated: " + str gc_stats()

gc_enable()
println "GC re-enabled"
println ""

// Test 5: Set threshold
println "Setting threshold to 512KB..."
gc_threshold 524288
println "New threshold: " + str gc_threshold()
println ""

// Test 6: Manual memory (not GC managed)
println "Testing manual memory allocation..."
let mem_ptr = alloc 1024
println "Allocated 1024 bytes manually"
println "GC stats (should not include manual alloc): " + str gc_stats()
free mem_ptr
println "Freed manual allocation"
println ""

// Test 7: Function that creates garbage
fn create_garbage n:
    let i = 0
    while i < n:
        let temp = [i, i*2, i*3]
        i = i + 1
    0

println "Creating garbage in function..."
create_garbage 100
println "After creating garbage:"
println "  Allocated bytes: " + str gc_stats()
println "  Collection count: " + str gc_count()
println ""

// Force final collection
println "Final collection..."
gc_collect()
println "Final stats:"
println "  Allocated bytes: " + str gc_stats()
println "  Collection count: " + str gc_count()
println ""

println "=== GC Tests Complete ==="
