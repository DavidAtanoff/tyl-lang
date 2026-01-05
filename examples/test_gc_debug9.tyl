// Test GC with dynamic allocations
println "=== Test ==="

mut x = 10
let list1 = [x, x+1]
println "After list1: " + str(gc_stats())

mut y = 20
println "After y=20: " + str(gc_stats())

let list2 = [y, y*2]
println "After list2: " + str(gc_stats())
