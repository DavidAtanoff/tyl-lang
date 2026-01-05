// Test full monomorphization
// Demonstrates specialized code generation for generic functions

// Generic identity function
fn identity[T] x: T -> T:
    return x

// Generic max function
fn max[T] a: T, b: T -> T:
    if a > b:
        return a
    return b

// Test with different types - each generates specialized code

// Integer instantiation
println("Testing with integers:")
i1 = identity(42)
println("  identity(42) = ", i1)

i2 = max(10, 20)
println("  max(10, 20) = ", i2)

i3 = max(100, 50)
println("  max(100, 50) = ", i3)

// Float instantiation
println("Testing with floats:")
f1 = identity(3.14)
println("  identity(3.14) = ", f1)

f2 = max(2.5, 1.5)
println("  max(2.5, 1.5) = ", f2)

println("Monomorphization test complete!")
