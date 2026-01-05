// Debug pointer dereference issue - with modification

fn test_pointer_ops:
    println("Testing pointer operations...")
    unsafe:
        mut x = 42
        p = &x
        val = *p
        println("Original value: {val}")
        *p = 100
        println("Modified value: {x}")
    println("Pointer ops test passed!")

fn main:
    test_pointer_ops()

main()
