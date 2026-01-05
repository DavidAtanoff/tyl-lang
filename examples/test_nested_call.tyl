// Test nested generic call detection

fn identity[T] x: T -> T:
    return x

fn wrapper a: float -> float:
    return a

fn main:
    // Direct call - should work
    println("Direct: ", identity(3.14))
    
    // Nested in wrapper - should work
    println("Nested: ", wrapper(identity(3.14)))
    
    return 0
