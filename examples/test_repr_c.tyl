// Test C struct layout
#[repr(C)]
record Point:
    x: int
    y: int

#[repr(C)]
record Color:
    r: u8
    g: u8
    b: u8
    a: u8

fn main:
    mut p: Point
    p.x = 10
    p.y = 20
    println("Point.x = ", p.x)
    println("Point.y = ", p.y)
    
    println("repr(C) records work!")
