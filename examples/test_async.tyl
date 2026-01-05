// Test Async/Spawn functionality
println("=== Async/Spawn Test ===")

// Define a simple function to spawn
fn worker:
    println("Worker started")
    return 42

// Test synchronous call first
println("Calling worker synchronously:")
result = worker()
println("Worker returned: {result}")

// Test spawn (creates a thread)
println("\nSpawning worker in thread:")
handle = spawn worker()
println("Got thread handle")

// Wait for the thread
println("Awaiting thread...")
thread_result = await handle
println("Thread returned: {thread_result}")

println("\n=== Async Test Complete ===")
