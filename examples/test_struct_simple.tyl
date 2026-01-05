// Simple struct test

#[repr(C)]
record Point:
    x: int
    y: int

fn main:
    // Create a point
    mut p: Point
    p.x = 10
    p.y = 20
    
    // Access fields directly
    println("Direct access:")
    println(p.x)
    println(p.y)
    
    // Create with literal
    p2 = Point{x: 30, y: 40}
    println("Literal access:")
    println(p2.x)
    println(p2.y)

main()
