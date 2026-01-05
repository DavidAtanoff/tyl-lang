// Test GC with dynamic allocations
println "=== Test ==="

mut x = 10
let list1 = [x, x+1]
println "After list1: " + str(gc_stats())

x = 20
println "After x=20: " + str(gc_stats())

let list2 = [x, x*2]
println "After list2: " + str(gc_stats())
