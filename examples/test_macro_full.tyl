// Test basic macro functionality

// Simple expression macro - should double a value
macro double x:
    x * 2

// Test expression macro
a = double(21)
println("double(21) = ", a)

// Macro with println
macro debug msg:
    println("DEBUG: ", msg)

debug("Hello from macro")

// Macro that uses conditionals - parameters are space-separated
macro max2 a b:
    if a > b:
        return a
    else:
        return b

c = max2(10, 20)
println("max2(10, 20) = ", c)

// Macro with arithmetic
macro square x:
    x * x

d = square(7)
println("square(7) = ", d)

// Nested macro calls
e = double(square(3))
println("double(square(3)) = ", e)

// Macro with multiple uses
f = square(2) + square(3) + square(4)
println("square(2) + square(3) + square(4) = ", f)

// Macro in a loop
mut sum = 0
for i in 1..5:
    sum += square(i)
println("Sum of squares 1-4 = ", sum)

println("Macro test complete!")
