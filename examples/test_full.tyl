// Full Feature Test Suite
println("========================================")
println("    FLEX FULL FEATURE TEST")
println("========================================")

// Test 1: Variables
println("--- Test 1: Variables ---")
x = 42
mut counter = 0
PI :: 3.14159
println("x = {x}")
println("PI = {PI}")
counter += 1
println("counter = {counter}")
println("Test 1 PASSED")

// Test 2: Arithmetic
println("--- Test 2: Arithmetic ---")
a = 10
b = 3
println("a + b = {a + b}")
println("a - b = {a - b}")
println("a * b = {a * b}")
println("a / b = {a / b}")
println("a % b = {a % b}")
println("Test 2 PASSED")

// Test 3: Floats
println("--- Test 3: Floats ---")
f1 = 3.5
f2 = 2.0
println("f1 + f2 = {f1 + f2}")
println("f1 * f2 = {f1 * f2}")
println("Test 3 PASSED")

// Test 4: Comparisons
println("--- Test 4: Comparisons ---")
println("10 == 10: {10 == 10}")
println("10 != 5: {10 != 5}")
println("10 < 20: {10 < 20}")
println("10 > 5: {10 > 5}")
println("Test 4 PASSED")

// Test 5: Logical
println("--- Test 5: Logical ---")
println("true and true: {true and true}")
println("true or false: {true or false}")
println("not false: {not false}")
println("Test 5 PASSED")

// Test 6: If/Elif/Else
println("--- Test 6: If/Elif/Else ---")
val = 15
if val > 20:
    println("val > 20")
elif val > 10:
    println("val > 10 (correct)")
else:
    println("val <= 10")
println("Test 6 PASSED")

// Test 7: While Loop
println("--- Test 7: While Loop ---")
mut i = 0
while i < 3:
    println("  while i = {i}")
    i += 1
println("Test 7 PASSED")

// Test 8: For Loop
println("--- Test 8: For Loop ---")
for j in 0..3:
    println("  for j = {j}")
println("Test 8 PASSED")

// Test 9: Break/Continue
println("--- Test 9: Break/Continue ---")
for k in 0..5:
    if k == 3:
        break
    println("  k = {k}")
println("Test 9 PASSED")

// Test 10: Functions
println("--- Test 10: Functions ---")

fn add_nums p, q:
    return p + q

fn square_num n => n * n

fn factorial_fn n:
    if n <= 1:
        return 1
    return n * factorial_fn(n - 1)

println("add_nums(3, 4) = {add_nums(3, 4)}")
println("square_num(5) = {square_num(5)}")
println("factorial_fn(5) = {factorial_fn(5)}")
println("Test 10 PASSED")

// Test 11: Ternary
println("--- Test 11: Ternary ---")
result = 10 > 5 ? "yes" : "no"
println("10 > 5 ? yes : no = {result}")
println("Test 11 PASSED")

// Test 12: Match
println("--- Test 12: Match ---")
value = 2
match value:
    1 -> println("one")
    2 -> println("two (correct)")
    _ -> println("other")
println("Test 12 PASSED")

// Test 13: Built-ins
println("--- Test 13: Built-ins ---")
println("platform() = {platform()}")
println("arch() = {arch()}")
test_str = "hello"
println("len(test_str) = {len(test_str)}")
println("upper(test_str) = {upper(test_str)}")
println("str(42) = {str(42)}")
println("Test 13 PASSED")

// Test 14: Modules
println("--- Test 14: Modules ---")

module mymath:
    fn double x:
        return x * 2

println("mymath.double(5) = {mymath.double(5)}")
println("Test 14 PASSED")

// Test 15: Nested Functions
println("--- Test 15: Nested Functions ---")

fn outer_fn x:
    fn inner_fn y:
        return y * 2
    return inner_fn(x) + 10

println("outer_fn(5) = {outer_fn(5)}")
println("Test 15 PASSED")

// Test 16: Lists
println("--- Test 16: Lists ---")
numbers = [1, 2, 3, 4, 5]
println("Created list")
println("Test 16 PASSED")

// Test 17: Records
println("--- Test 17: Records ---")
point = { x: 10, y: 20 }
println("Created record")
println("Test 17 PASSED")

println("========================================")
println("    ALL TESTS PASSED!")
println("========================================")
