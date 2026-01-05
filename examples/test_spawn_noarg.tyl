// Test spawn with parameterless function
println("=== Spawn No-Arg Test ===")

fn worker:
    println("Worker running!")
    return 42

// Try to spawn a parameterless function
task = spawn worker()
sleep(100)
println("Main done")
