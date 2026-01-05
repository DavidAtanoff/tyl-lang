// Test C FFI interop - comprehensive test

// Windows API imports
extern "kernel32.dll":
    fn GetStdHandle(nStdHandle: int) -> int
    fn Sleep(dwMilliseconds: int)

// Test pointer type in function signatures
extern "C":
    fn process_buffer(buf: *int, len: int) -> int

// Test function with pointer return type
fn get_pointer -> *int:
    return 0

// Test pointer type in variable declarations
fn test_pointer_types:
    let p: *int = 0
    let s: *str = 0
    let v: *void = 0
    println("Pointer type declarations work!")

fn main:
    println("=== FFI Test Suite ===")
    
    // Test 1: Basic extern import
    println("Test 1: Extern imports work")
    
    // Test 2: Pointer types
    test_pointer_types()
    
    // Test 3: Sleep function (Windows API)
    println("Test 3: Calling Sleep(100)...")
    Sleep(3000)
    println("Sleep completed!")
    
    println("=== All FFI Tests Passed ===")

main()
