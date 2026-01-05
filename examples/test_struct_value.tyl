// Test struct-by-value pass and return

// Small struct that fits in registers (16 bytes with int)
#[repr(C)]
record Point:
    x: int
    y: int

// Function that takes struct by value (currently passed as pointer)
fn print_point p: Point:
    print("Point(")
    print(p.x)
    print(", ")
    print(p.y)
    println(")")

// Function that returns a struct
fn make_point x: int, y: int -> Point:
    return Point{x: x, y: y}

fn main:
    // Create a point using record literal syntax
    p1 = Point{x: 10, y: 20}
    print_point(p1)
    
    // Create another point using function
    p2 = make_point(30, 40)
    print_point(p2)
    
    // Modify and print
    mut p3 = Point{x: 0, y: 0}
    p3.x = 100
    p3.y = 200
    print_point(p3)
    
    println("Struct-by-value test complete!")

main()
