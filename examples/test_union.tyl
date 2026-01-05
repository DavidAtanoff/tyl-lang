// Test union types - overlapping memory layout

// Basic union with different types
union IntOrFloat:
    i: int
    f: float

// Union with different sized fields
union Value:
    byte_val: u8
    short_val: i16
    int_val: i32
    long_val: int

// C-compatible union
#[repr(C)]
union CUnion:
    a: i32
    b: float

fn main:
    println("=== Union Type Test ===")
    println("")
    
    // Test sizeof - should be max of field sizes
    println("IntOrFloat:")
    println("  sizeof: ", sizeof(IntOrFloat))
    println("  alignof: ", alignof(IntOrFloat))
    println("  offsetof i: ", offsetof(IntOrFloat, i))
    println("  offsetof f: ", offsetof(IntOrFloat, f))
    println("")
    
    println("Value:")
    println("  sizeof: ", sizeof(Value))
    println("  alignof: ", alignof(Value))
    println("  offsetof byte_val: ", offsetof(Value, byte_val))
    println("  offsetof short_val: ", offsetof(Value, short_val))
    println("  offsetof int_val: ", offsetof(Value, int_val))
    println("  offsetof long_val: ", offsetof(Value, long_val))
    println("")
    
    println("CUnion:")
    println("  sizeof: ", sizeof(CUnion))
    println("  alignof: ", alignof(CUnion))
    println("  offsetof a: ", offsetof(CUnion, a))
    println("  offsetof b: ", offsetof(CUnion, b))
    println("")
    
    // Test field access - writing to one field affects the other
    println("=== Field Access Test ===")
    mut u: IntOrFloat
    u.f = 1.5
    println("After setting u.f = 1.5:")
    println("u.f = ", u.f)
    println("u.i = ", u.i)  // Should show the bit pattern of 1.5
    println("")
    
    u.i = 42
    println("After setting u.i = 42:")
    println("u.i = ", u.i)
    println("")
    
    println("Union test complete!")
