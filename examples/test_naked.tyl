// Test naked function with inline assembly

#[naked]
fn naked_return_42 -> int:
    unsafe:
        asm! { "mov rax, 42", "ret" }

fn main:
    println("Testing naked function...")
    
    result = naked_return_42()
    println("Naked function returned: ", result)
    
    println("Test complete!")
    return 0
