// Debug GC allocation
println "Start"
println "Stats before: " + str(gc_stats())

// First dynamic allocation
mut a = 1
let list1 = [a]
println "After list1 (1 elem): " + str(gc_stats())

// Second dynamic allocation
let list2 = [a, a]
println "After list2 (2 elem): " + str(gc_stats())

// Third dynamic allocation
let list3 = [a, a, a]
println "After list3 (3 elem): " + str(gc_stats())

println "Done"
