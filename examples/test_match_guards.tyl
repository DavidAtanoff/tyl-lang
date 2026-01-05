// Test Match with Guards
println("=== Match Guards Test ===")

// Test 1: Basic guards with variable binding
n = 15
println("Test 1 - n = 15:")
match n:
    x if x < 0 -> println("  negative")
    x if x == 0 -> println("  zero")
    x if x > 0 -> println("  positive")

// Test 2: Negative number
n2 = -5
println("Test 2 - n2 = -5:")
match n2:
    x if x < 0 -> println("  negative")
    x if x == 0 -> println("  zero")
    x if x > 0 -> println("  positive")

// Test 3: Zero
n3 = 0
println("Test 3 - n3 = 0:")
match n3:
    x if x < 0 -> println("  negative")
    x if x == 0 -> println("  zero")
    x if x > 0 -> println("  positive")

// Test 4: Guards with ranges
score = 85
println("Test 4 - score = 85:")
match score:
    s if s >= 90 -> println("  Grade: A")
    s if s >= 80 -> println("  Grade: B")
    s if s >= 70 -> println("  Grade: C")
    s if s >= 60 -> println("  Grade: D")
    _ -> println("  Grade: F")

// Test 5: Guards with literal patterns
value = 42
println("Test 5 - value = 42:")
match value:
    42 -> println("  The answer!")
    x if x > 100 -> println("  Big number")
    _ -> println("  Something else")

println("=== All Match Guards Tests Passed ===")
