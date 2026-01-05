// Test callback to C and calling convention attributes

// Define a callback function with cdecl calling convention
// This function can be passed to C code and called back
#[cdecl]
fn my_callback x: int -> int:
    println("Callback called with: ", x)
    return x * 2

// Test function that takes a function pointer and calls it
fn call_with_value fptr: *fn(int) -> int, value: int -> int:
    return fptr(value)

// Main function to test the callback mechanism
fn main:
    println("Testing callback to C...")
    
    // Get the address of the callback function
    // Since my_callback has #[cdecl], &my_callback returns the trampoline address
    unsafe:
        callback_ptr = &my_callback
        
        // Call through the function pointer
        result = call_with_value(callback_ptr, 21)
        println("Result: ", result)
        
        // Direct call to verify the function works
        direct_result = my_callback(10)
        println("Direct result: ", direct_result)
    
    println("Callback test complete!")
    return 0
