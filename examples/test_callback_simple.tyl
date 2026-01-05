// Simple test for callback and calling convention attributes

// Define a callback function with cdecl calling convention
#[cdecl]
fn my_callback x: int -> int:
    println("Callback called with: ", x)
    return x * 2

fn main:
    println("Testing callback...")
    
    // Direct call to verify the function works
    result = my_callback(10)
    println("Direct result: ", result)
    
    println("Test complete!")
    return 0
