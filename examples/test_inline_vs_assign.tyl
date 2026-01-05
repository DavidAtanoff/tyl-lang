// Test inline call vs assigned call

fn max_val[T] a: T, b: T -> T:
    if a > b:
        return a
    return b

fn main:
    println("Assigned first:")
    r1 = max_val(2.0, 1.0)
    println("r1 = ", r1)
    
    println("\nInline in println:")
    println("max_val(2.0, 1.0) = ", max_val(2.0, 1.0))
    
    return 0
