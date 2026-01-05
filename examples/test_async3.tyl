// Test Async/Spawn functionality - minimal test

// Define function FIRST
fn compute:
    return 42

println("=== Async/Spawn Test 3 ===")

// Test spawn
println("Spawning compute...")
handle = spawn compute()
println("Handle obtained")

// Wait for the thread
result = await handle
println("Result: {result}")

println("=== Done ===")
