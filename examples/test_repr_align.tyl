// Test explicit alignment with #[repr(align(N))]

// 16-byte aligned struct (useful for SIMD)
#[repr(align(16))]
record AlignedVec4:
    x: float
    y: float
    z: float
    w: float

// 32-byte aligned struct
#[repr(align(32))]
record CacheLine:
    data: int
    padding: int

// Combined with C layout
#[repr(C)]
#[repr(align(8))]
record AlignedPoint:
    x: i32
    y: i32

fn main:
    println("=== Explicit Alignment Test ===")
    
    // Test AlignedVec4 (16-byte aligned)
    println("")
    println("AlignedVec4 (align(16)):")
    println("  sizeof: ", sizeof(AlignedVec4))
    println("  alignof: ", alignof(AlignedVec4))
    println("  offsetof x: ", offsetof(AlignedVec4, x))
    println("  offsetof y: ", offsetof(AlignedVec4, y))
    println("  offsetof z: ", offsetof(AlignedVec4, z))
    println("  offsetof w: ", offsetof(AlignedVec4, w))
    
    // Test CacheLine (32-byte aligned)
    println("")
    println("CacheLine (align(32)):")
    println("  sizeof: ", sizeof(CacheLine))
    println("  alignof: ", alignof(CacheLine))
    println("  offsetof data: ", offsetof(CacheLine, data))
    println("  offsetof padding: ", offsetof(CacheLine, padding))
    
    // Test AlignedPoint (C layout + 8-byte aligned)
    println("")
    println("AlignedPoint (repr(C) + align(8)):")
    println("  sizeof: ", sizeof(AlignedPoint))
    println("  alignof: ", alignof(AlignedPoint))
    println("  offsetof x: ", offsetof(AlignedPoint, x))
    println("  offsetof y: ", offsetof(AlignedPoint, y))
    
    // Test actual field access
    println("")
    println("=== Field Access Test ===")
    mut v: AlignedVec4
    v.x = 1.0
    v.y = 2.0
    v.z = 3.0
    v.w = 4.0
    println("v.x = ", v.x)
    println("v.y = ", v.y)
    println("v.z = ", v.z)
    println("v.w = ", v.w)
    
    println("")
    println("Explicit alignment test complete!")
