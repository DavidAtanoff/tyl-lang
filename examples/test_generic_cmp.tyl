// Debug generic comparison issue

fn max_val[T] a: T, b: T -> T:
    if a > b:
        return a
    return b

fn main:
    // Non-generic float comparison (should work)
    println("-- Direct float comparison --")
    x = 3.14
    y = 2.71
    if x > y:
        println("3.14 > 2.71: true (correct)")
    else:
        println("3.14 > 2.71: false (WRONG)")
    
    // Generic float comparison
    println("-- Generic float comparison --")
    m = max_val(3.14, 2.71)
    println("max_val(3.14, 2.71) = ", m)
    
    // Try with variables
    println("-- Generic with variables --")
    a = 3.14
    b = 2.71
    m2 = max_val(a, b)
    println("max_val(a=3.14, b=2.71) = ", m2)
    
    // Integer comparison for reference
    println("-- Generic int comparison --")
    m3 = max_val(10, 5)
    println("max_val(10, 5) = ", m3)
    
    // More float tests
    println("-- More float tests --")
    println("max_val(1.0, 2.0) = ", max_val(1.0, 2.0))
    println("max_val(2.0, 1.0) = ", max_val(2.0, 1.0))
    println("max_val(-1.0, 1.0) = ", max_val(-1.0, 1.0))
    
    return 0
