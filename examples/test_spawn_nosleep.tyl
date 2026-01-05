// Test spawn without sleep
fn worker:
    return 42

println("Testing spawn without sleep...")
result = worker()
println("Direct: {result}")

handle = spawn worker()
println("Spawned")

final = await handle
println("Result: {final}")

println("Done")
