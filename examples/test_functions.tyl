// Test 5: Functions
// Testing: function declaration, parameters, return, recursion

println("=== Functions Test ===")

// Simple function
fn greet:
    println("Hello from greet!")

// Function with parameters
fn add a, b:
    return a + b

// Single-expression function
fn square x => x * x

// Function with multiple statements
fn factorial n:
    if n <= 1:
        return 1
    return n * factorial(n - 1)

// Function calling other functions
fn compute x:
    doubled = add(x, x)
    squared = square(x)
    return doubled + squared

// Test calls
greet()

result1 = add(3, 4)
println("add(3, 4) = {result1}")

result2 = square(5)
println("square(5) = {result2}")

result3 = factorial(5)
println("factorial(5) = {result3}")

result4 = compute(3)
println("compute(3) = add(3,3) + square(3) = 6 + 9 = {result4}")

// Nested function calls
nested = add(square(2), square(3))
println("add(square(2), square(3)) = 4 + 9 = {nested}")

println("Test 5 PASSED")
