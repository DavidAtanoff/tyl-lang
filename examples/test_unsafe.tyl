// Test unsafe block enforcement
// This file tests that unsafe operations require unsafe blocks

// Test 1: Safe code should work without unsafe
fn safe_function:
    x = 42
    println("Safe code works: {x}")

// Test 2: Unsafe operations inside unsafe block should work
fn test_unsafe_alloc:
    unsafe:
        p = alloc(64)
        println("Allocated memory at: {p}")
        free(p)
        println("Memory freed")

// Test 3: Pointer operations inside unsafe block
fn test_unsafe_pointers:
    unsafe:
        mut x = 100
        p = &x
        value = *p
        println("Value via pointer: {value}")
        *p = 200
        println("Modified value: {x}")

// Test 4: Pointer arithmetic inside unsafe block
fn test_pointer_arithmetic:
    unsafe:
        buffer = alloc(32)
        p2 = buffer + 8
        println("Pointer arithmetic works")
        free(buffer)

// Main function
fn main:
    safe_function()
    test_unsafe_alloc()
    test_unsafe_pointers()
    test_pointer_arithmetic()
    println("All unsafe tests passed!")

main()
