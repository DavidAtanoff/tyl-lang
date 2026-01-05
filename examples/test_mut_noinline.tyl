// Test with a function that can't be inlined (has side effects)

fn test:
    mut x = 42
    print(x)
    println("")
    // Add more statements to prevent inlining
    mut y = 100
    print(y)
    println("")
    mut z = 200
    print(z)
    println("")

fn main:
    test()

main()
