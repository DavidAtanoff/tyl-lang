// Test pointer operations: difference, casting, void pointer

fn main:
    unsafe:
        // Test pointer difference (byte-level arithmetic)
        int_ptr: *int = new int
        *int_ptr = 100
        
        ptr1 = int_ptr
        ptr2 = ptr1 + 40  // Move 40 bytes forward
        
        // Pointer difference gives byte difference
        diff = ptr2 - ptr1
        println("Pointer difference (bytes): ", diff)  // Should be 40
        
        // Test pointer casting
        *int_ptr = 42
        
        // Cast to void pointer (type erasure)
        void_ptr = int_ptr as *void
        println("Cast to void pointer: OK")
        
        // Cast back to int pointer
        back_ptr = void_ptr as *int
        value = *back_ptr
        println("Value after cast back: ", value)  // Should be 42
        
        // Test int to pointer cast
        addr = int_ptr as int
        println("Pointer as int: OK")
        
        // Test pointer to pointer cast
        float_ptr = int_ptr as *float
        println("Cast to float pointer: OK")
        
    println("All pointer operations completed!")
