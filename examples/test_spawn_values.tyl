// Test different return values
fn ret_1:
    return 1

fn ret_10:
    return 10

fn ret_100:
    return 100

fn ret_255:
    return 255

println("Testing different return values...")

h1 = spawn ret_1()
r1 = await h1
println("Expected 1, got: {r1}")

h10 = spawn ret_10()
r10 = await h10
println("Expected 10, got: {r10}")

h100 = spawn ret_100()
r100 = await h100
println("Expected 100, got: {r100}")

h255 = spawn ret_255()
r255 = await h255
println("Expected 255, got: {r255}")

println("Done")
