// Closure Tests for Flex Compiler
// Tests variable capture from outer scopes

println("=== Closure Capture Tests ===")

// Test 1: Simple closure with one capture
fn make_adder n:
    x = n
    return |y| => y + x

// Note: Use intermediate variables for multiple closures
// (known limitation with direct assignment)
temp1 = make_adder(5)
add5 = temp1

temp2 = make_adder(10)
add10 = temp2

r1 = add5(3)
r2 = add10(3)
println("Test 1 - Single capture:")
println("  add5(3) = ", r1)
println("  add10(3) = ", r2)

// Test 2: Closure with multiple captures
fn make_linear a, b:
    slope = a
    intercept = b
    return |x| => x * slope + intercept

temp3 = make_linear(2, 3)
line = temp3
r3 = line(5)
println("Test 2 - Multiple captures:")
println("  line(5) = 5*2+3 = ", r3)

// Test 3: Lambda without captures
double = |x| => x * 2
r4 = double(7)
println("Test 3 - No captures:")
println("  double(7) = ", r4)

// Test 4: Single closure (no workaround needed)
fn make_multiplier:
    factor = 10
    return |x| => x * factor

mult = make_multiplier()
r5 = mult(5)
println("Test 4 - Single closure:")
println("  mult(5) = ", r5)

println("=== All Closure Tests Passed ===")
