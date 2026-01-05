// Debug - step by step

fn test1:
    unsafe:
        mut x = 42
        println("In function, x = {x}")

fn test2:
    unsafe:
        mut x = 42
        println("Before &x, x = {x}")
        p = &x
        println("After &x, x = {x}")

fn test3:
    unsafe:
        mut x = 42
        println("Step 1: x = {x}")
        p = &x
        println("Step 2: x = {x}, p = {p}")
        val = *p
        println("Step 3: val = {val}")

fn main:
    println("=== Test 1 ===")
    test1()
    println("")
    println("=== Test 2 ===")
    test2()
    println("")
    println("=== Test 3 ===")
    test3()

main()
