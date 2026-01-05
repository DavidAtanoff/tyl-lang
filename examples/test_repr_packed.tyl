// Test packed struct layout
// #[repr(packed)] removes all padding between fields

#[repr(packed)]
record PackedInts:
    a: int     // 8 bytes at offset 0
    b: int     // 8 bytes at offset 8
    c: int     // 8 bytes at offset 16

// Compare with default layout (should be same for int fields)
record DefaultInts:
    a: int     // 8 bytes at offset 0
    b: int     // 8 bytes at offset 8
    c: int     // 8 bytes at offset 16

fn main:
    // Test packed struct offsets
    println("=== Packed Struct Test ===")
    println("PackedInts field offsets:")
    println("  a: ", offsetof(PackedInts, a))
    println("  b: ", offsetof(PackedInts, b))
    println("  c: ", offsetof(PackedInts, c))
    
    // Test default struct offsets
    println("")
    println("=== Default Struct Test ===")
    println("DefaultInts field offsets:")
    println("  a: ", offsetof(DefaultInts, a))
    println("  b: ", offsetof(DefaultInts, b))
    println("  c: ", offsetof(DefaultInts, c))
    
    // Test actual field access with packed struct
    println("")
    println("=== Packed Struct Field Access ===")
    mut p: PackedInts
    p.a = 100
    p.b = 200
    p.c = 300
    
    println("p.a = ", p.a)
    println("p.b = ", p.b)
    println("p.c = ", p.c)
    
    println("")
    println("Packed struct test complete!")
