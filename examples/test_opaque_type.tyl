// Test opaque types for FFI
type Handle = opaque
type FILE = opaque

fn main:
    // Opaque types can be used as pointer types
    let h: *Handle = null
    let f: *FILE = null
    
    println("Opaque types work!")
    
    // Test that we can pass opaque pointers around
    if h == null:
        println("Handle is null")
    
    if f == null:
        println("FILE is null")
