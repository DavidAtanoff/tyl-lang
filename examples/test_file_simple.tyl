// Simple file I/O test
fn main:
    println("Testing file open...")
    
    // Try to open a file for writing
    h = open("test_simple.txt", "w")
    println("Handle: ", h)
    
    if h != -1:
        println("File opened successfully!")
        close(h)
        println("File closed")
    else:
        println("Failed to open file")
    
    println("Done!")
    return 0
