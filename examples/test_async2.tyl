// Test Async/Spawn functionality - simpler test
println("=== Async/Spawn Test 2 ===")

// Define a simple function to spawn
fn worker:
    return 100

// Test spawn (creates a thread)
println("Spawning worker in thread...")
handle = spawn worker()

// Small delay to let thread start
sleep(100)

// Wait for the thread
println("Awaiting thread...")
thread_result = await handle
println("Thread returned: {thread_result}")

println("=== Test Complete ===")
