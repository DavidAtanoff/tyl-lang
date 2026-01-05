// Test 11: Nested Functions
// Testing: functions defined inside other functions

println("=== Nested Functions Test ===")

fn outer x:
    // Nested function
    fn inner y:
        return y * 2
    
    result = inner(x)
    return result + 10

result1 = outer(5)
println("outer(5) = inner(5) + 10 = 10 + 10 = {result1}")

// Multiple levels of nesting
fn level1 a:
    fn level2 b:
        fn level3 c:
            return c * 3
        return level3(b) + 2
    return level2(a) + 1

result2 = level1(4)
println("level1(4) = {result2}")

println("Test 11 PASSED")
