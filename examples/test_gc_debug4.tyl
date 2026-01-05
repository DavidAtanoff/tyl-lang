// Test GC with dynamic allocations
println "=== GC Dynamic Allocation Test ==="

// Initial state
println "Initial: " + str(gc_stats()) + " bytes"

// Create a dynamic list (not all constants)
mut x = 10
let list1 = [x, x+1, x+2, x+3, x+4]
println "After dynamic list1: " + str(gc_stats()) + " bytes"
