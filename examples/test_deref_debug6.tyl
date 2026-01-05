// Debug - unsafe block effect

fn test_no_unsafe:
    mut x = 42
    println("no unsafe: x = {x}")

fn test_with_unsafe:
    unsafe:
        mut x = 42
        println("with unsafe: x = {x}")

fn test_unsafe_after:
    mut x = 42
    unsafe:
        println("unsafe after: x = {x}")

fn main:
    test_no_unsafe()
    test_with_unsafe()
    test_unsafe_after()

main()
