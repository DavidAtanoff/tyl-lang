// Test infix macro - power operator **

// Define the power operator macro using recursion
fn power base, exp:
    if exp <= 0:
        return 1
    return base * power(base, exp - 1)

macro infix "**" 60 a b: power(a, b)

// Test the power operator
println("=== Infix Macro Test ===")

x = 2 ** 3
println("2 ** 3 = {x}")

y = 3 ** 4
println("3 ** 4 = {y}")

z = 5 ** 2
println("5 ** 2 = {z}")

// Test with expressions
a = (2 + 1) ** 2
println("(2 + 1) ** 2 = {a}")

// Test chaining (right-to-left)
b = 2 ** 2 ** 2
println("2 ** 2 ** 2 = {b}")

println("Test PASSED")
