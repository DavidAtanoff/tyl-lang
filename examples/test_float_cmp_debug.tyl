// Debug float comparison in generics

fn max_val[T] a: T, b: T -> T:
    if a > b:
        return a
    return b

fn main:
    // Test 1: Direct values
    println("Test 1: max_val(2.0, 1.0)")
    r1 = max_val(2.0, 1.0)
    println("Result: ", r1)
    println("Expected: 2.0")
    
    // Test 2: Reversed
    println("\nTest 2: max_val(1.0, 2.0)")
    r2 = max_val(1.0, 2.0)
    println("Result: ", r2)
    println("Expected: 2.0")
    
    // Test 3: Direct comparison (no generics)
    println("\nTest 3: Direct comparison")
    a = 2.0
    b = 1.0
    if a > b:
        println("2.0 > 1.0: TRUE (correct)")
    else:
        println("2.0 > 1.0: FALSE (WRONG)")
    
    return 0
