fn identity[T] x: T -> T:
    return x

fn pair_first[A, B] a: A, b: B -> A:
    return a

fn pair_second[A, B] a: A, b: B -> B:
    return b

fn max_val[T] a: T, b: T -> T:
    if a > b:
        return a
    return b

fn main:
    println("=== Generics Full Test ===")
    
    println("-- Integer Tests --")
    println("identity(42) = ", identity(42))

    println("-- Float Tests --")
    println("identity(3.14) = ", identity(3.14))
    println("identity(2.718) = ", identity(2.718))

    println("-- String Tests --")
    println("identity(hello world) = ", identity("hello world"))

    println("-- Multiple Type Param Tests --")
    println("pair_first(100, text) = ", pair_first(100, "text"))
    println("pair_second(100, text) = ", pair_second(100, "text"))
    println("pair_first(1.5, 999) = ", pair_first(1.5, 999))

    println("-- Generic Comparison Tests --")
    println("max_val(5, 10) = ", max_val(5, 10))
    println("max_val(3.14, 2.71) = ", max_val(3.14, 2.71))

    println("-- Chained Generic Calls --")
    println("identity(identity(identity(42))) = ", identity(identity(identity(42))))

    println("-- Generic with Expressions --")
    println("identity(10 + 20) = ", identity(10 + 20))
    println("identity(3.14 * 2.0) = ", identity(3.14 * 2.0))

    println("=== All Tests Complete ===")
    return 0
