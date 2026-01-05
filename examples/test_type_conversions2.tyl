// Additional type conversion tests - edge cases

// Test int() with runtime values
println("=== Runtime int() tests ===")
s = "999"
println("int(s) where s='999': ", int(s))

// Test float() with runtime values
println("")
println("=== Runtime float() tests ===")
n = 100
f = float(n)
println("float(100) = ", f)

// Test float arithmetic after conversion
f1 = float(10)
f2 = float(3)
println("float(10) / float(3) = ", f1 / f2)

// Test bool() with runtime values
println("")
println("=== Runtime bool() tests ===")
x = 0
println("bool(x) where x=0: ", bool(x))
x = 42
println("bool(x) where x=42: ", bool(x))

// Test chained conversions
println("")
println("=== Chained conversions ===")
result = int(str(42))
println("int(str(42)) = ", result)

// Test float to int truncation
println("")
println("=== Float to int truncation ===")
println("int(9.9) = ", int(9.9))
println("int(-9.9) = ", int(-9.9))
println("int(0.5) = ", int(0.5))
println("int(-0.5) = ", int(-0.5))

println("")
println("All edge case tests completed!")
