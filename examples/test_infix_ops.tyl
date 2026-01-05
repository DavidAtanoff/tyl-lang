// Test multiple infix operators

// Power operator
fn power base, exp:
    if exp <= 0:
        return 1
    return base * power(base, exp - 1)

macro infix "**" 60 a b: power(a, b)

// Modulo with different precedence
macro infix "%%" 55 a b: a - (a / b) * b

println("=== Multiple Infix Operators Test ===")

// Test power
x = 2 ** 4
println("2 ** 4 = {x}")

// Test custom modulo
y = 17 %% 5
println("17 %% 5 = {y}")

// Test mixed
z = 2 ** 3 %% 5
println("2 ** 3 %% 5 = {z}")

println("Test PASSED")
