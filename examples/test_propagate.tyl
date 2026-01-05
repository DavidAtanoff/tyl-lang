// Test error propagation operator (?)

// Function that returns a Result
fn divide a, b:
    if b == 0:
        return Err(0)
    return Ok(a / b)

// Function that uses ? to propagate errors
fn calculate x:
    // If divide returns Err, this function returns early with that error
    result1 = divide(x, 2)?
    result2 = divide(result1, 2)?
    return Ok(result2)

// Test the ? operator
fn main:
    // Test successful case
    r1 = calculate(100)
    if is_ok(r1):
        println("calculate(100) = ", unwrap(r1))
    else:
        println("calculate(100) failed")
    
    // Test with direct ? usage
    r2 = divide(10, 2)
    if is_ok(r2):
        val = unwrap(r2)
        println("10 / 2 = ", val)
    
    // Test error case
    r3 = divide(10, 0)
    if is_err(r3):
        println("10 / 0 correctly returned error")
    
    // Test unwrap_or
    safe = unwrap_or(divide(10, 0), -1)
    println("unwrap_or(10/0, -1) = ", safe)
    
    println("Done!")

main()
