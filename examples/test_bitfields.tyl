// Test bitfields implementation

// Define a record with bitfields
#[repr(C)]
record Flags:
    a: i32 : 4    // 4 bits (0-15)
    b: i32 : 4    // 4 bits (0-15)
    c: i32 : 8    // 8 bits (0-255)
    d: i32 : 16   // 16 bits

// Test basic bitfield operations
fn main:
    mut f = Flags{a: 0, b: 0, c: 0, d: 0}
    
    // Set bitfield values
    f.a = 5
    f.b = 10
    f.c = 200
    f.d = 1000
    
    // Read and print bitfield values
    println("Bitfield test:")
    print("a = ")
    println(f.a)
    print("b = ")
    println(f.b)
    print("c = ")
    println(f.c)
    print("d = ")
    println(f.d)
    
    // Test masking (a should be masked to 4 bits)
    f.a = 255  // Should be masked to 15 (0xF)
    print("a after 255 = ")
    println(f.a)
    
    println("Done!")

main()
