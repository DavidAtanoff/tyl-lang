// Debug - force stack allocation by using many variables

fn test_force_stack:
    // Use more than 5 variables to force some to stack
    mut a = 1
    mut b = 2
    mut c = 3
    mut d = 4
    mut e = 5
    mut f = 6  // This should spill to stack
    
    // Use all variables to extend their live ranges
    sum = a + b + c + d + e + f
    println("sum = {sum}")
    println("a={a} b={b} c={c} d={d} e={e} f={f}")

fn main:
    test_force_stack()

main()
