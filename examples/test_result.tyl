// Test Result Types: Ok, Err, is_ok, is_err, unwrap, unwrap_or

println("=== Result Type Tests ===")

// Test 1: Basic Ok and Err
ok_val = Ok(42)
err_val = Err(0)

println("Test 1 - Basic Ok/Err:")
println("  Ok(42) created")
println("  Err(0) created")

// Test 2: is_ok and is_err
println("Test 2 - is_ok/is_err:")
r1 = is_ok(ok_val)
r2 = is_err(ok_val)
r3 = is_ok(err_val)
r4 = is_err(err_val)
println("  is_ok(Ok(42)) = ", r1)
println("  is_err(Ok(42)) = ", r2)
println("  is_ok(Err(0)) = ", r3)
println("  is_err(Err(0)) = ", r4)

// Test 3: unwrap
println("Test 3 - unwrap:")
r5 = unwrap(ok_val)
r6 = unwrap(err_val)
println("  unwrap(Ok(42)) = ", r5)
println("  unwrap(Err(0)) = ", r6)

// Test 4: unwrap_or
println("Test 4 - unwrap_or:")
r7 = unwrap_or(ok_val, 100)
r8 = unwrap_or(err_val, 100)
println("  unwrap_or(Ok(42), 100) = ", r7)
println("  unwrap_or(Err(0), 100) = ", r8)

// Test 5: Function returning Result
fn divide a, b:
    if b == 0:
        return Err(0)
    return Ok(a / b)

println("Test 5 - Function returning Result:")
res1 = divide(10, 2)
res2 = divide(10, 0)
println("  divide(10, 2) is_ok = ", is_ok(res1))
println("  divide(10, 2) unwrap = ", unwrap(res1))
println("  divide(10, 0) is_err = ", is_err(res2))
println("  divide(10, 0) unwrap_or(0) = ", unwrap_or(res2, 0))

// Test 6: Ok with different values
println("Test 6 - Various Ok values:")
ok0 = Ok(0)
ok1 = Ok(1)
ok100 = Ok(100)
println("  unwrap(Ok(0)) = ", unwrap(ok0))
println("  unwrap(Ok(1)) = ", unwrap(ok1))
println("  unwrap(Ok(100)) = ", unwrap(ok100))

println("=== All Result Tests Passed ===")
