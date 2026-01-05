// Test type introspection operators: sizeof, alignof, offsetof

// Test sizeof for primitive types
println("=== sizeof tests ===")
println("sizeof(int) = ", sizeof(int))       // Expected: 8
println("sizeof(float) = ", sizeof(float))   // Expected: 8
println("sizeof(bool) = ", sizeof(bool))     // Expected: 1
println("sizeof(str) = ", sizeof(str))       // Expected: 8 (pointer)

// Test alignof for primitive types
println("")
println("=== alignof tests ===")
println("alignof(int) = ", alignof(int))     // Expected: 8
println("alignof(float) = ", alignof(float)) // Expected: 8
println("alignof(bool) = ", alignof(bool))   // Expected: 1

// Define a record type for offsetof testing
record Point:
    x: int
    y: int

record Person:
    name: str
    age: int
    height: float

// Test offsetof
println("")
println("=== offsetof tests ===")
println("offsetof(Point, x) = ", offsetof(Point, x))     // Expected: 0
println("offsetof(Point, y) = ", offsetof(Point, y))     // Expected: 8
println("offsetof(Person, name) = ", offsetof(Person, name))     // Expected: 0
println("offsetof(Person, age) = ", offsetof(Person, age))       // Expected: 8
println("offsetof(Person, height) = ", offsetof(Person, height)) // Expected: 16

// Practical usage example: manual memory layout
println("")
println("=== Practical usage ===")
point_size = sizeof(int) * 2
println("Point struct size (2 ints): ", point_size)

// Verify sizes are compile-time constants by using in expressions
total_size = sizeof(int) + sizeof(float) + sizeof(bool)
println("Total size of int + float + bool: ", total_size)  // Expected: 8 + 8 + 1 = 17

println("")
println("All type introspection tests completed!")
