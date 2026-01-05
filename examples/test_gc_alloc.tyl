// Test GC allocation tracking
println "=== GC Allocation Test ==="

// Initial state
println "Initial stats:"
println "  Allocated: " + str(gc_stats())
println "  Threshold: " + str(gc_threshold())
println "  Count: " + str(gc_count())

// Create some lists (should be tracked)
println ""
println "Creating lists..."
let list1 = [1, 2, 3, 4, 5]
println "After list1: " + str(gc_stats())

let list2 = [10, 20, 30, 40, 50, 60, 70, 80, 90, 100]
println "After list2: " + str(gc_stats())

// Force collection
println ""
println "Forcing collection..."
gc_collect()
println "After gc_collect:"
println "  Allocated: " + str(gc_stats())
println "  Count: " + str(gc_count())

println ""
println "Done"
