// Simple monomorphization test

fn identity[T] x: T -> T:
    return x

// Test integer
a = identity(42)
println("identity(42) = ", a)

// Test float
b = identity(3.14)
println("identity(3.14) = ", b)

// Test another integer to verify same specialization is reused
c = identity(100)
println("identity(100) = ", c)

println("Done!")
