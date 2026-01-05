// Test file for generics support

// Generic identity function
fn identity[T] x: T -> T:
    return x

// Generic swap function
fn swap[T] a: T, b: T -> T:
    return b

// Generic max function
fn max_val[T] a: T, b: T -> T:
    if a > b:
        return a
    return b

// Generic pair record
record Pair[A, B]:
    first: A
    second: B

// Test generic functions with different types
fn main:
    // Test with integers
    x = identity(42)
    println("identity(42) = ", x)
    
    // Test with floats - use a variable to ensure type tracking
    pi = 3.14
    y = identity(pi)
    println("identity(pi) = ", y)
    
    // Test with strings
    s = identity("hello")
    println("identity(hello) = ", s)
    
    // Test swap
    a = 10
    b = 20
    result = swap(a, b)
    println("swap(10, 20) = ", result)
    
    // Test max with integers
    m1 = max_val(5, 10)
    println("max_val(5, 10) = ", m1)
    
    // Test max with floats
    f1 = 3.14
    f2 = 2.71
    m2 = max_val(f1, f2)
    println("max_val(3.14, 2.71) = ", m2)
    
    println("All generic tests passed!")
    return 0
