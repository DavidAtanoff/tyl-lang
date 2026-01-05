// Test 8: Modules
// Testing: module declaration, module function calls

println("=== Modules Test ===")

// Define a math module
module math:
    fn add a, b:
        return a + b
    
    fn multiply a, b:
        return a * b
    
    fn square x:
        return x * x

// Use module functions
result1 = math.add(5, 3)
println("math.add(5, 3) = {result1}")

result2 = math.multiply(4, 7)
println("math.multiply(4, 7) = {result2}")

result3 = math.square(6)
println("math.square(6) = {result3}")

// Nested module calls
result4 = math.add(math.square(2), math.square(3))
println("math.add(math.square(2), math.square(3)) = {result4}")

println("Test 8 PASSED")
