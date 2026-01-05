// Test channels for inter-thread communication

// Create an unbuffered channel
ch = chan[int]

// Create a buffered channel with capacity 10
buffered_ch = chan[int, 10]

// Simple producer function
fn producer x:
    println("Producer sending: ", x)
    ch <- x
    println("Producer sent: ", x)
    return 0

// Simple consumer function  
fn consumer x:
    println("Consumer waiting...")
    value = <- ch
    println("Consumer received: ", value)
    return value

// Test basic channel operations
fn main:
    println("=== Channel Test ===")
    
    // Create a channel
    my_chan = chan[int]
    
    // Spawn producer
    task1 = spawn producer(42)
    
    // Spawn consumer
    task2 = spawn consumer(0)
    
    // Wait for both
    await task1
    result = await task2
    
    println("Final result: ", result)
    println("=== Test Complete ===")
    return 0

main()
