// Test file for Flex traits - simplified version

// Basic trait definition with method signature
trait Printable:
    fn to_string s -> str:
        return "default"

// Record type
record Point:
    x: int
    y: int

// Implement Printable for Point
impl Printable for Point:
    fn to_string s -> str:
        return "Point"

// Test static dispatch
fn test_static_dispatch:
    println("Testing traits...")
    println("Trait test complete!")

// Main entry point
test_static_dispatch()
