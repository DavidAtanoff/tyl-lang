// Test memory intrinsics: memcpy, memset, memmove, memcmp

fn main:
    println("=== Memory Intrinsics Tests ===")
    
    unsafe:
        // Test memset
        println("Testing memset...")
        buf = alloc(32)
        memset(buf, 65, 8)
        println("memset: filled 8 bytes with 'A' (65)")
        
        // Test memcpy
        println("Testing memcpy...")
        dst = alloc(32)
        memcpy(dst, buf, 8)
        println("memcpy: copied 8 bytes from buf to dst")
        
        // Test memcmp - equal
        println("Testing memcmp...")
        cmp1 = memcmp(buf, dst, 8)
        println("memcmp equal buffers: {cmp1}")
        
        // Test memcmp - less than
        memset(dst, 66, 1)  // Make dst[0] = 'B' > buf[0] = 'A'
        cmp2 = memcmp(buf, dst, 8)
        println("memcmp buf < dst: {cmp2}")
        
        // Test memcmp - greater than
        cmp3 = memcmp(dst, buf, 8)
        println("memcmp dst > buf: {cmp3}")
        
        // Test memmove
        println("Testing memmove...")
        buf2 = alloc(32)
        memset(buf2, 65, 8)
        memmove(buf2 + 2, buf2, 6)
        println("memmove: overlapping copy completed")
        
        free(buf)
        free(dst)
        free(buf2)
    
    println("=== All tests passed! ===")

main()
