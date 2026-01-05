// Test macro layers

// Define a debug layer with macros
layer debug:
    macro log msg:
        println("[LOG] ", msg)
    
    macro trace fn_name:
        println("ENTER: ", fn_name)
    
    macro assert cond msg:
        if not cond:
            println("ASSERT FAILED: ", msg)

// Enable the debug layer
use layer "debug"

// Now the macros are available
log("Starting application")
trace("main")

x = 10
assert(x > 0, "x must be positive")
assert(x < 5, "x must be less than 5")

println("Layer test complete!")
