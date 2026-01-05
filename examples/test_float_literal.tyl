// Test float literal detection in generic calls

fn max_val[T] a: T, b: T -> T:
    if a > b:
        return a
    return b

fn main:
    // These should all call max_val$float
    println("Test with literals:")
    r1 = max_val(1.0, 2.0)
    println("max_val(1.0, 2.0) = ", r1)
    
    r2 = max_val(2.0, 1.0)
    println("max_val(2.0, 1.0) = ", r2)
    
    // Test with variables
    println("\nTest with variables:")
    a = 2.0
    b = 1.0
    r3 = max_val(a, b)
    println("max_val(a=2.0, b=1.0) = ", r3)
    
    return 0
