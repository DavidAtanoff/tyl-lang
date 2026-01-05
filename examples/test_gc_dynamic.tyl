// Test GC with dynamic allocations
println "=== GC Dynamic Allocation Test ==="

// Initial state
println "Initial: " + str(gc_stats()) + " bytes"

// Create a dynamic list (not all constants)
mut x = 10
let list1 = [x, x+1, x+2, x+3, x+4]
println "After dynamic list1: " + str(gc_stats()) + " bytes"

// Create another dynamic list
x = 20
let list2 = [x, x*2, x*3]
println "After dynamic list2: " + str(gc_stats()) + " bytes"

// Force collection (lists are still referenced, should not be freed)
gc_collect()
println "After gc_collect (lists still live): " + str(gc_stats()) + " bytes"
println "Collection count: " + str(gc_count())

println ""
println "Done"
