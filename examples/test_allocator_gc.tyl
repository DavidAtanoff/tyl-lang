// Test allocator stats with GC-managed allocations

println("Testing allocator stats with GC allocations:")

// GC stats should track list allocations
println("Initial gc_stats: ", gc_stats())

// Create some lists (GC-managed)
list1 = [1, 2, 3, 4, 5]
println("After list1: ", gc_stats())

list2 = [10, 20, 30, 40, 50, 60, 70, 80, 90, 100]
println("After list2: ", gc_stats())

// Create a string (GC-managed)
str1 = "Hello, World! This is a test string."
println("After str1: ", gc_stats())

// Create a record (GC-managed)
rec1 = {x: 10, y: 20, z: 30}
println("After rec1: ", gc_stats())

println("GC collection count: ", gc_count())

// Force a collection
gc_collect()
println("After gc_collect: ", gc_stats())
println("GC collection count: ", gc_count())

println("Allocator test complete!")
