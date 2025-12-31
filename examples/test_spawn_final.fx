// Test spawn with proper function
println("--- Spawn Test ---")

fn worker x:
    return 42

println("Direct call: {worker(0)}")

h = spawn worker(0)
println("Spawned")
sleep(100)
r = await h
println("Result: {r}")
println("Done")
