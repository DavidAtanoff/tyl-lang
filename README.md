# Flex Programming Language

A modern, statically-typed programming language with Python-like syntax that compiles directly to native machine code. Built from scratch in C++ without LLVM.

```flex
// Hello World
println("Hello, World!")

// Variables
name = "Alice"
mut counter = 0
PI :: 3.14159

// Functions
fn greet person:
    println("Hello, {person}!")

fn square x => x * x

// Lists & Comprehensions
numbers = [1, 2, 3, 4, 5]
squares = [x * x for x in numbers]

// Pattern Matching
match day:
    "Saturday" -> println("Weekend!")
    "Sunday" -> println("Weekend!")
    _ -> println("Weekday")
```

---

## Table of Contents

1. [Quick Start](#quick-start)
2. [Language Syntax](#language-syntax)
3. [Data Types](#data-types)
4. [Variables](#variables)
5. [Functions](#functions)
6. [Control Flow](#control-flow)
7. [Collections](#collections)
8. [Pattern Matching](#pattern-matching)
9. [Error Handling](#error-handling)
10. [Custom Types](#custom-types)
11. [Traits & Generics](#traits--generics)
12. [Modules & Imports](#modules--imports)
13. [Macros](#macros)
14. [Memory Management](#memory-management)
15. [Async/Await](#asyncawait)
16. [External Functions](#external-functions)
17. [Compiler Architecture](#compiler-architecture)
18. [Optimizations](#optimizations)
19. [CLI Reference](#cli-reference)
20. [Implementation Status](#implementation-status)
21. [Roadmap](#roadmap)

---

## Quick Start

### Building the Compiler

```bash
# Clone and build
cmake -B build
cmake --build build --config Release

# The compiler is at build/Release/flex.exe
```

### Running Programs

```bash
# Compile and run (bytecode VM)
flex program.fx

# Compile to native executable
flex -c program.fx -o program.exe

# Run with optimizations
flex -c program.fx -o program.exe -O3

# Show AST
flex -a program.fx

# Show tokens
flex -t program.fx
```

---

## Language Syntax

Flex uses Python-like indentation for blocks. No semicolons, no braces.

```flex
// Single-line comment

/* Multi-line
   comment */

// Blocks are defined by indentation after ':'
if condition:
    do_something()
    do_more()

// Single-expression forms use '=>'
fn double x => x * 2
```

### Keywords

```
fn if elif else for while match return break continue
mut record enum type trait impl use module
true false nil and or not in
async await spawn unsafe extern
```

### Operators (by precedence)

| Precedence | Operators | Description |
|------------|-----------|-------------|
| 1 (highest) | `- ! not * &` | Unary |
| 2 | `* / %` | Multiplicative |
| 3 | `+ -` | Additive |
| 4 | `..` | Range |
| 5 | `< > <= >= == !=` | Comparison |
| 6 | `and or` | Logical |
| 7 | `??` | Null coalescing |
| 8 | `\|>` | Pipe |
| 9 (lowest) | `= += -= *= /=` | Assignment |

### Special Syntax

| Syntax | Meaning |
|--------|---------|
| `::` | Compile-time constant |
| `=>` | Lambda body / single-expression function |
| `->` | Return type / match arm |
| `?` | Nullable type / error propagation |
| `\|x\|` | Lambda parameter delimiters |

---

## Data Types

### Primitive Types

| Type | Size | Description | Example |
|------|------|-------------|---------|
| `int` | 8 bytes | 64-bit signed integer | `42`, `-100` |
| `i8` | 1 byte | 8-bit signed | `127i8` |
| `i16` | 2 bytes | 16-bit signed | `32000i16` |
| `i32` | 4 bytes | 32-bit signed | `100i32` |
| `i64` | 8 bytes | 64-bit signed | `100i64` |
| `u8` | 1 byte | 8-bit unsigned | `255u8` |
| `u16` | 2 bytes | 16-bit unsigned | `65000u16` |
| `u32` | 4 bytes | 32-bit unsigned | `100u32` |
| `u64` | 8 bytes | 64-bit unsigned | `100u64` |
| `float` | 8 bytes | 64-bit IEEE 754 | `3.14`, `-0.5` |
| `f32` | 4 bytes | 32-bit float | `3.14f32` |
| `f64` | 8 bytes | 64-bit float | `3.14f64` |
| `bool` | 1 byte | Boolean | `true`, `false` |
| `str` | varies | UTF-8 string | `"hello"` |
| `nil` | 0 bytes | Null value | `nil` |

### Compound Types

```flex
[T]                 // List of T
{K: V}              // Map from K to V
(T1, T2, T3)        // Tuple
T?                  // Nullable T
Result[T, E]        // Result type
fn(T1, T2) -> R     // Function type
*T                  // Raw pointer (unsafe)
&T                  // Reference
```

### Type Inference

Types are inferred when possible:

```flex
x = 42              // Inferred as int
pi = 3.14           // Inferred as float
name = "Alice"      // Inferred as str
numbers = [1,2,3]   // Inferred as [int]

// Explicit type annotation
age: int = 25
ratio: float = 0.5
items: [str] = []
```

---

## Variables

### Immutable (Default)

Variables are immutable by default. No keyword needed.

```flex
x = 42
name = "Alice"
numbers = [1, 2, 3]

// This would be an error:
// x = 100  // Cannot reassign immutable variable
```

### Mutable

Use `mut` for variables that need to change:

```flex
mut counter = 0
counter += 1
counter = counter * 2

mut items = []
items = push(items, "new item")
```

### Compile-Time Constants

Use `::` for values computed at compile time:

```flex
PI :: 3.14159
MAX_SIZE :: 1024
GREETING :: "Hello"

// Constants can use expressions
DOUBLED :: MAX_SIZE * 2
```

### Compound Assignment

```flex
mut x = 10
x += 5      // x = x + 5
x -= 3      // x = x - 3
x *= 2      // x = x * 2
x /= 4      // x = x / 4
x %= 3      // x = x % 3
```

---

## Functions

### Standard Functions

```flex
// Multi-line function (no parentheses around parameters)
fn greet name:
    println("Hello, {name}!")

// Multiple parameters
fn add a, b:
    return a + b

// With type annotations
fn multiply a: int, b: int -> int:
    return a * b

// No parameters
fn say_hello:
    println("Hello!")
```

### Single-Expression Functions

Use `=>` for functions with a single expression (implicit return):

```flex
fn square x => x * x
fn double x => x * 2
fn max a, b => a if a > b else b
fn is_even n => n % 2 == 0
```

### Default Parameters

```flex
fn greet name, greeting = "Hello":
    println("{greeting}, {name}!")

greet("Alice")              // "Hello, Alice!"
greet("Bob", "Hi")          // "Hi, Bob!"
```

### Named Arguments

```flex
fn create_user name, age, active = true:
    // ...

create_user(name: "Alice", age: 30)
create_user(age: 25, name: "Bob", active: false)
```

### Recursion

```flex
fn factorial n:
    if n <= 1:
        return 1
    return n * factorial(n - 1)

fn fibonacci n:
    if n <= 1:
        return n
    return fibonacci(n - 1) + fibonacci(n - 2)
```

### Nested Functions

```flex
fn outer x:
    fn inner y:
        return x + y
    return inner(10)

result = outer(5)  // 15
```

---

## Lambdas

Lambdas are anonymous functions using `|params| => expression`:

```flex
// Single parameter
double = |x| => x * 2

// Multiple parameters
add = |a, b| => a + b

// No parameters
get_pi = || => 3.14159

// Using lambdas
result = double(21)     // 42
sum = add(3, 4)         // 7
```

### Higher-Order Functions

```flex
numbers = [1, 2, 3, 4, 5]

// Map
doubled = map(numbers, |x| => x * 2)

// Filter
evens = filter(numbers, |x| => x % 2 == 0)

// Reduce
sum = reduce(numbers, 0, |acc, x| => acc + x)
```

### Closures

Lambdas capture variables from their enclosing scope:

```flex
fn make_adder n:
    return |x| => x + n

add_five = make_adder(5)
result = add_five(10)   // 15
```

---

## Control Flow

### If/Elif/Else

```flex
if score >= 90:
    println("A")
elif score >= 80:
    println("B")
elif score >= 70:
    println("C")
else:
    println("F")
```

### Conditional Expression

```flex
result = "positive" if x > 0 else "non-positive"
max_val = a if a > b else b
```

### Ternary Operator

```flex
result = condition ? value_if_true : value_if_false
```

### While Loop

```flex
mut i = 0
while i < 10:
    println(i)
    i += 1
```

### For Loop with Range

```flex
// Exclusive range (0 to 9)
for i in 0..10:
    println(i)

// With step
for i in 0..100 by 10:
    println(i)

// Using range function
for i in range(10):
    println(i)

for i in range(5, 10):
    println(i)
```

### For Loop with Collection

```flex
names = ["Alice", "Bob", "Charlie"]
for name in names:
    println("Hello, {name}!")

numbers = [1, 2, 3, 4, 5]
for n in numbers:
    println(n * 2)
```

### Break and Continue

```flex
for i in 0..100:
    if i == 50:
        break           // Exit loop
    if i % 2 == 0:
        continue        // Skip to next iteration
    println(i)
```

---

## Collections

### Lists

```flex
// Creation
numbers = [1, 2, 3, 4, 5]
empty = []
mixed = [1, "two", 3.0]

// Indexing (0-based)
first = numbers[0]      // 1
third = numbers[2]      // 3

// Length
size = len(numbers)     // 5

// Iteration
for n in numbers:
    println(n)
```

### List Comprehensions

```flex
// Basic comprehension
squares = [x * x for x in 0..10]

// With filter
evens = [x for x in 0..20 if x % 2 == 0]

// Transform and filter
big_squares = [x * x for x in 0..100 if x * x > 50]
```

### List Operations

```flex
numbers = [1, 2, 3]

// Push (returns new list)
numbers = push(numbers, 4)

// Pop (returns last element)
last = pop(numbers)

// Length
size = len(numbers)
```

### Records (Anonymous Structs)

```flex
// Creation
point = {x: 10, y: 20}
person = {name: "Alice", age: 30, active: true}

// Access
println(point.x)        // 10
println(person.name)    // "Alice"
```

### Maps

```flex
// Creation
scores = {"alice": 100, "bob": 85}

// Access
alice_score = scores["alice"]

// Update
scores["charlie"] = 90
```

---

## Pattern Matching

### Basic Match

```flex
match value:
    0 -> println("zero")
    1 -> println("one")
    2 -> println("two")
    _ -> println("other")   // Wildcard (default)
```

### Match with Blocks

```flex
match command:
    "start" ->
        println("Starting...")
        initialize()
    "stop" ->
        println("Stopping...")
        cleanup()
    _ ->
        println("Unknown command")
```

### Match with Guards

```flex
match n:
    x if x < 0 -> println("negative")
    x if x == 0 -> println("zero")
    x if x > 0 -> println("positive")
```

### Match on Types

```flex
match result:
    Ok(value) -> println("Success: {value}")
    Err(msg) -> println("Error: {msg}")
```

---

## Error Handling

### Nullable Types

```flex
// Nullable declaration
value: int? = nil
value = 42

// Null coalescing
safe_value = value ?? 0     // Use 0 if nil

// Check for nil
if value != nil:
    println(value)
```

### Result Type

```flex
fn divide a: int, b: int -> Result[int, str]:
    if b == 0:
        return Err("Division by zero")
    return Ok(a / b)

// Using Result
result = divide(10, 2)
match result:
    Ok(value) -> println("Result: {value}")
    Err(msg) -> println("Error: {msg}")
```

### Error Propagation

Use `?` to propagate errors:

```flex
fn calculate x, y -> Result[int, str]:
    result = divide(x, y)?      // Returns Err if divide fails
    return Ok(result * 2)
```

### Try Expression

```flex
// Try with default
value = try risky_operation() else 0

// Equivalent to
value = risky_operation() ?? 0
```

---

## Custom Types

### Records (Structs)

```flex
record Point:
    x: float
    y: float

record Player:
    name: str
    health: int = 100       // Default value
    score: int = 0

// Creating instances
p = Point{x: 10.0, y: 20.0}
hero = Player{name: "Hero"}

// Accessing fields
println(p.x)
println(hero.health)        // 100 (default)
```

### Enums

```flex
enum Color:
    Red
    Green
    Blue

enum Status:
    Ok = 0
    Error = -1
    Pending = 1

// Using enums
color = Color.Red
status = Status.Ok
```

### Type Aliases

```flex
type UserID = int
type Handler = fn(int) -> bool
type Point2D = {x: float, y: float}

id: UserID = 12345
```

---

## Traits & Generics

### Traits

Traits define shared behavior:

```flex
trait Printable:
    fn to_string self -> str

trait Comparable:
    fn compare self, other -> int

trait Hashable:
    fn hash self -> int
```

### Implementing Traits

```flex
impl Printable for Point:
    fn to_string self -> str:
        return "({self.x}, {self.y})"

impl Comparable for Point:
    fn compare self, other -> int:
        dist_self = self.x * self.x + self.y * self.y
        dist_other = other.x * other.x + other.y * other.y
        return dist_self - dist_other
```

### Generics

```flex
// Generic function
fn swap[T] a: T, b: T -> (T, T):
    return (b, a)

// Generic record
record Pair[A, B]:
    first: A
    second: B

// Generic with trait bounds
fn print_all[T: Printable] items: [T]:
    for item in items:
        println(item.to_string())
```

---

## Modules & Imports

### File Imports

```flex
// math_utils.fx
PI :: 3.14159

fn square x => x * x

fn circle_area r:
    return PI * r * r
```

```flex
// main.fx
use "math_utils.fx"

println(PI)                 // 3.14159
println(square(5))          // 25
println(circle_area(10))    // 314.159
```

### Import with Alias

```flex
use "math_utils.fx" as math

println(math.PI)
println(math.square(5))
```

### Selective Imports

```flex
use "math_utils.fx"::{PI, square}

println(PI)
println(square(5))
```

### Inline Modules

```flex
module math:
    PI :: 3.14159
    
    fn add a, b:
        return a + b
    
    fn multiply a, b:
        return a * b
    
    fn square x:
        return x * x

// Using module functions
result = math.add(5, 3)
squared = math.square(6)
```

---

## Macros

### Expression Macros

```flex
macro debug expr:
    println("DEBUG: {expr}")

macro assert cond:
    if not cond:
        println("Assertion failed!")

// Usage
debug(x + y)
assert(x > 0)
```

### Operator Macros

Define custom infix operators:

```flex
// Power operator
macro infix ** precedence 8:
    mut result = 1
    for i in 0..right:
        result *= left
    return result

// Usage
x = 2 ** 10     // 1024
```

### Syntax Macros (DSL Support)

Create embedded domain-specific languages:

```flex
// Define SQL DSL
syntax sql:
    => db.query(content)

// Usage - raw content is captured
result = sql:
    SELECT * FROM users
    WHERE age > 21
    ORDER BY name

// Define HTML DSL
syntax html:
    => render_html(content)

page = html:
    <div class="container">
        <h1>Hello World</h1>
    </div>
```

### Layers

Group related macros that can be enabled/disabled:

```flex
// Define a debug layer
layer debug:
    macro log msg:
        println("[LOG] {msg}")
    
    macro trace fn_name:
        println("ENTER: {fn_name}")
    
    macro assert cond, msg:
        if not cond:
            println("ASSERT FAILED: {msg}")

// Enable the layer
use layer debug

// Now macros are available
log("Starting application")
trace("main")
assert(x > 0, "x must be positive")
```

---

## Memory Management

### Safe Mode (Default)

By default, Flex uses automatic memory management:

```flex
// Memory is automatically managed
person = {name: "Alice", age: 30}
numbers = [1, 2, 3, 4, 5]

// No manual cleanup needed
```

### Unsafe Mode

For low-level control, use `unsafe` blocks:

```flex
unsafe:
    // Raw pointer allocation
    ptr = new int
    *ptr = 42
    value = *ptr
    delete ptr

unsafe:
    // Array allocation
    arr = new [int; 100]
    arr[0] = 1
    arr[1] = 2
    delete arr
```

### Pointer Operations

```flex
unsafe:
    x = 42
    ptr = &x            // Address-of
    value = *ptr        // Dereference
    
    // Pointer arithmetic
    arr = new [int; 10]
    ptr = &arr[0]
    next = ptr + 1      // Points to arr[1]
```

### Memory Functions

```flex
unsafe:
    // Allocate raw memory
    buffer = alloc(1024)
    
    // Zero memory
    zero(buffer, 1024)
    
    // Copy memory
    copy(dest, src, size)
    
    // Free memory
    free(buffer)
```

---

## Async/Await

### Async Functions

```flex
async fn fetch_data url:
    response = await http_get(url)
    return response.body

async fn process_all urls:
    mut results = []
    for url in urls:
        data = await fetch_data(url)
        results = push(results, data)
    return results
```

### Spawn

Run tasks concurrently:

```flex
async fn main:
    // Spawn concurrent tasks
    task1 = spawn fetch_data("http://api1.com")
    task2 = spawn fetch_data("http://api2.com")
    task3 = spawn fetch_data("http://api3.com")
    
    // Wait for all
    result1 = await task1
    result2 = await task2
    result3 = await task3
```

### Parallel Processing

```flex
async fn parallel_map items, fn:
    tasks = [spawn fn(item) for item in items]
    return [await task for task in tasks]
```

> **Note**: Functions used with `spawn` must have at least one parameter (parser limitation). Use a dummy parameter if needed: `fn worker x: return 42`

---

## External Functions

### FFI with C Libraries

```flex
extern "C" from "kernel32.dll":
    fn GetTickCount64() -> int
    fn Sleep(ms: int)
    fn GetComputerNameA(buffer: *u8, size: *int) -> bool

// Usage
start = GetTickCount64()
Sleep(1000)
elapsed = GetTickCount64() - start
println("Elapsed: {elapsed}ms")
```

### Calling Conventions

```flex
// Windows x64 calling convention (default)
extern "C" from "user32.dll":
    fn MessageBoxA(hwnd: int, text: *u8, caption: *u8, type: int) -> int

// Stdcall convention
extern "stdcall" from "kernel32.dll":
    fn ExitProcess(code: int)
```

### Library Loading

```flex
// System libraries
extern "C" from "msvcrt.dll":
    fn printf(fmt: *u8, ...) -> int
    fn malloc(size: int) -> *u8
    fn free(ptr: *u8)

// Custom DLLs
extern "C" from "mylib.dll":
    fn custom_function(x: int) -> int
```

---

## Built-in Functions

### I/O Functions

| Function | Description | Example |
|----------|-------------|---------|
| `print(...)` | Print without newline | `print("Hello")` |
| `println(...)` | Print with newline | `println("Hello")` |

### String Functions

| Function | Description | Example |
|----------|-------------|---------|
| `len(s)` | String length | `len("hello")` → `5` |
| `str(x)` | Convert to string | `str(42)` → `"42"` |
| `upper(s)` | Uppercase | `upper("hi")` → `"HI"` |
| `contains(s, sub)` | Check substring | `contains("hello", "ell")` → `true` |

### Collection Functions

| Function | Description | Example |
|----------|-------------|---------|
| `len(list)` | List length | `len([1,2,3])` → `3` |
| `push(list, x)` | Append element | `push([1,2], 3)` → `[1,2,3]` |
| `pop(list)` | Remove last | `pop([1,2,3])` → `3` |
| `range(n)` | Range 0 to n-1 | `range(5)` → `0,1,2,3,4` |
| `range(a, b)` | Range a to b-1 | `range(2, 5)` → `2,3,4` |

### System Functions

| Function | Description | Example |
|----------|-------------|---------|
| `platform()` | OS name | `"windows"` |
| `arch()` | CPU architecture | `"x64"` |
| `hostname()` | Computer name | `"DESKTOP-XXX"` |
| `username()` | Current user | `"Alice"` |
| `cpu_count()` | Number of CPUs | `8` |

### Time Functions

| Function | Description | Example |
|----------|-------------|---------|
| `sleep(ms)` | Sleep milliseconds | `sleep(1000)` |
| `now()` | Time in seconds | `1234567` |
| `now_ms()` | Time in milliseconds | `1234567890` |
| `year()` | Current year | `2024` |
| `month()` | Current month (1-12) | `12` |
| `day()` | Current day (1-31) | `30` |
| `hour()` | Current hour (0-23) | `14` |
| `minute()` | Current minute (0-59) | `30` |
| `second()` | Current second (0-59) | `45` |

---

## Compiler Architecture

### Compilation Pipeline

```
Source Code (.fx)
       │
       ▼
┌─────────────────┐
│     Lexer       │  Tokenization with indentation handling
│  (frontend/)    │  Converts source to tokens
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│     Parser      │  Recursive descent parser
│  (frontend/)    │  Builds Abstract Syntax Tree
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ Macro Expander  │  Expands macros and syntax macros
│  (semantic/)    │  Transforms DSL blocks
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  Type Checker   │  Type inference and validation
│  (semantic/)    │  Symbol table management
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│   Optimizer     │  Multi-pass AST optimization
│  (semantic/)    │  SSA, DCE, inlining, etc.
└────────┬────────┘
         │
         ├──────────────────┐
         ▼                  ▼
┌─────────────────┐  ┌─────────────────┐
│ Native Codegen  │  │ Bytecode Comp.  │
│  (backend/)     │  │  (backend/)     │
│  x64 machine    │  │  Stack-based    │
└────────┬────────┘  └────────┬────────┘
         │                    │
         ▼                    ▼
┌─────────────────┐  ┌─────────────────┐
│  PE Generator   │  │       VM        │
│  (backend/)     │  │  (backend/)     │
│  Windows .exe   │  │  Interpreter    │
└────────┬────────┘  └─────────────────┘
         │
         ▼
    Executable (.exe)
```

### Source Directory Structure

```
src/
├── main.cpp                    # Entry point, CLI handling
├── common/                     # Shared utilities
│   ├── common.h               # Common includes
│   ├── diagnostics.h          # Error reporting
│   ├── errors.h               # Error types
│   ├── platform.h             # Platform detection
│   └── types.h                # Basic type definitions
│
├── frontend/                   # Parsing & AST
│   ├── ast/                   # Abstract Syntax Tree
│   │   └── ast.h              # All AST node definitions
│   ├── lexer/                 # Tokenization
│   │   ├── lexer.h            # Lexer interface
│   │   └── lexer.cpp          # Token generation
│   ├── parser/                # Parsing
│   │   ├── parser_base.h      # Parser interface
│   │   ├── parser_core.cpp    # Core parsing logic
│   │   ├── parser_expr.cpp    # Expression parsing
│   │   ├── parser_stmt.cpp    # Statement parsing
│   │   └── parser_decl.cpp    # Declaration parsing
│   ├── token/                 # Token definitions
│   │   └── token.h            # Token types
│   └── macro/                 # Macro parsing
│       └── macro_parser.h     # Macro syntax
│
├── semantic/                   # Analysis & Optimization
│   ├── checker/               # Type checking
│   │   ├── type_checker.h     # Type checker interface
│   │   ├── checker_core.cpp   # Core type logic
│   │   ├── checker_expr.cpp   # Expression types
│   │   ├── checker_stmt.cpp   # Statement types
│   │   └── checker_decl.cpp   # Declaration types
│   ├── types/                 # Type system
│   │   └── types.h            # Type definitions
│   ├── symbols/               # Symbol table
│   │   ├── symbol_table.h     # Symbol management
│   │   └── symbol_table.cpp   # Scope handling
│   ├── optimizer/             # Optimization passes
│   │   ├── optimizer.h        # Optimizer interface
│   │   ├── constant_folding.* # Constant folding
│   │   ├── constant_propagation.* # Value propagation
│   │   ├── dead_code.*        # Dead code elimination
│   │   ├── inlining.*         # Function inlining
│   │   ├── tail_call.*        # Tail call optimization
│   │   ├── ctfe.*             # Compile-time evaluation
│   │   ├── ssa.*              # SSA form conversion
│   │   ├── loop_optimizer.*   # Loop optimizations
│   │   ├── cse.*              # Common subexpr elimination
│   │   ├── gvn.*              # Global value numbering
│   │   ├── algebraic.*        # Algebraic simplification
│   │   ├── pgo.*              # Profile-guided optimization
│   │   └── instruction_scheduler.* # Instruction scheduling
│   ├── expander/              # Macro expansion
│   │   └── macro_expander.*   # Macro processing
│   └── module/                # Module system
│       └── module_system.*    # Import/export handling
│
├── backend/                    # Code Generation
│   ├── codegen/               # Native x64 codegen
│   │   ├── native_codegen.h   # Codegen interface
│   │   ├── codegen_core.cpp   # Core generation
│   │   ├── codegen_expr.cpp   # Expression codegen
│   │   ├── codegen_stmt.cpp   # Statement codegen
│   │   ├── codegen_decl.cpp   # Declaration codegen
│   │   ├── codegen_call.cpp   # Function calls
│   │   ├── codegen_builtins.cpp # Built-in functions
│   │   ├── register_allocator.* # Register allocation
│   │   ├── global_register_allocator.* # Global reg alloc
│   │   └── vectorizer.*       # SIMD vectorization
│   ├── x64/                   # x64 assembly
│   │   ├── x64_assembler.h    # Assembler interface
│   │   └── pe_generator.*     # PE executable generation
│   ├── bytecode/              # Bytecode compiler
│   │   ├── compiler.h         # Bytecode compiler
│   │   └── compiler_*.cpp     # Compilation passes
│   ├── vm/                    # Virtual machine
│   │   ├── vm.h               # VM interface
│   │   └── vm.cpp             # Bytecode interpreter
│   ├── linker/                # Object file linking
│   │   └── linker.*           # Link multiple .o files
│   └── object/                # Object file format
│       └── object_file.*      # .o file generation
│
├── stdlib/                     # Standard library
│   ├── flex_stdlib.h          # Stdlib interface
│   ├── stdlib_core.cpp        # Core functions
│   ├── io/                    # I/O functions
│   ├── string/                # String functions
│   ├── math/                  # Math functions
│   ├── list/                  # List functions
│   ├── map/                   # Map functions
│   ├── time/                  # Time functions
│   ├── system/                # System functions
│   └── json/                  # JSON parsing
│
└── cli/                        # CLI utilities
    ├── ast_printer.h          # AST visualization
    └── ast_printer.cpp        # Pretty printing
```

---

## Optimizations

### Optimization Levels

| Level | Description | Use Case |
|-------|-------------|----------|
| `-O0` | No optimization | Debugging, fastest compile |
| `-O1` | Basic optimizations | Quick builds with some optimization |
| `-O2` | Standard optimizations | Production builds (default) |
| `-O3` | Aggressive optimizations | Maximum performance |
| `-Os` | Optimize for size | Embedded, size-constrained |
| `-Oz` | Aggressive size optimization | Minimal binary size |
| `-Ofast` | Maximum + unsafe optimizations | Benchmarks, non-critical code |

### AST-Level Optimizations (Implemented ✓)

| Pass | Description | Level |
|------|-------------|-------|
| **Constant Folding** | `2 + 2` → `4` at compile time | O1+ |
| **Constant Propagation** | Track and substitute known values | O1+ |
| **Dead Code Elimination** | Remove unreachable code | O1+ |
| **Function Inlining** | Replace calls with function body | O2+ |
| **Tail Call Optimization** | Convert tail recursion to loops | O2+ |
| **CTFE** | Compile-Time Function Evaluation | O2+ |
| **Tree Shaking** | Remove unused functions | O2+ |
| **Dead Store Elimination** | Remove redundant assignments | O2+ |
| **Block Flattening** | Simplify nested control flow | O2+ |
| **Common Subexpression Elimination** | Reuse computed values | O2+ |
| **Global Value Numbering** | Advanced CSE across blocks | O3+ |
| **Algebraic Simplification** | `x * 1` → `x`, `x + 0` → `x` | O1+ |
| **Loop Optimization** | Unrolling, invariant hoisting | O3+ |
| **SSA Conversion** | Static Single Assignment form | O3+ |
| **Instruction Scheduling** | Reorder for better pipelining | O3+ |
| **Profile-Guided Optimization** | Use runtime profiles | O3+ |

### Code Generation Optimizations (Implemented ✓)

| Optimization | Description | Level |
|--------------|-------------|-------|
| **Register Allocation** | Local variables in registers (RBX, R12-R15) | O1+ |
| **Global Register Allocation** | Top-level variables in callee-saved regs | O2+ |
| **Leaf Function Optimization** | Minimal stack frames for non-calling functions | O2+ |
| **Strength Reduction** | `x * 2` → `x << 1`, `x * 3` → `lea` | O1+ |
| **Peephole Optimization** | Pattern-based instruction improvement | O1+ |
| **Instruction Selection** | Optimal encoding (`xor rax, rax` for zero) | O1+ |
| **SIMD Vectorization** | Auto-vectorize loops with SSE2/AVX | O3+ |
| **Shared Runtime Routines** | Deduplicate common code (itoa, etc.) | Os+ |
| **Stdout Handle Caching** | Cache console handle in register | O1+ |
| **Stack Frame Optimization** | Pre-calculate and allocate once | O1+ |

### Optimization Examples

```flex
// Before optimization
fn example:
    x = 2 + 3           // Constant folding
    y = x * 2           // Constant propagation
    z = y + 0           // Algebraic simplification
    if false:           // Dead code elimination
        unreachable()
    return z

// After optimization (O2)
fn example:
    return 10           // All computed at compile time
```

```flex
// Tail call optimization
fn factorial n, acc = 1:
    if n <= 1:
        return acc
    return factorial(n - 1, n * acc)  // Converted to loop

// Becomes equivalent to:
fn factorial n, acc = 1:
    while n > 1:
        acc = n * acc
        n = n - 1
    return acc
```

---

## CLI Reference

### Basic Usage

```bash
flex [options] <file.fx>
```

### Options

| Option | Long Form | Description |
|--------|-----------|-------------|
| `-r` | `--run` | Run program (default, uses bytecode VM) |
| `-c` | `--compile` | Compile to native executable (.exe) |
| `-S` | `--obj` | Compile to object file (.o) |
| `-o <file>` | | Output file name |
| `-l <file.o>` | | Link object file |
| | `--link` | Link mode (combine .o files) |
| `-t` | `--tokens` | Print tokens |
| `-a` | `--ast` | Print AST |
| `-s` | `--asm` | Print generated assembly |
| `-b` | `--bytecode` | Print bytecode |
| `-d` | `--debug` | Debug mode (trace execution) |
| `-v` | `--verbose` | Verbose output |
| | `--no-typecheck` | Skip type checking |
| | `--map` | Generate map file |
| `-h` | `--help` | Show help |

### Optimization Options

| Option | Description |
|--------|-------------|
| `-O0` | No optimization (fastest compile, debug friendly) |
| `-O1` | Basic optimizations |
| `-O2` | Standard optimizations (default) |
| `-O3` | Aggressive optimizations |
| `-Os` | Optimize for size |
| `-Oz` | Aggressive size optimization |
| `-Ofast` | Maximum optimization (includes unsafe opts) |

### Examples

```bash
# Run a program (bytecode VM)
flex program.fx

# Compile to executable
flex -c program.fx -o program.exe

# Compile with maximum optimization
flex -c program.fx -o program.exe -O3

# Show AST
flex -a program.fx

# Show tokens
flex -t program.fx

# Show generated assembly
flex -c program.fx -o program.exe -s

# Compile without type checking (faster, less safe)
flex -c program.fx -o program.exe --no-typecheck

# Verbose compilation
flex -c program.fx -o program.exe -v

# Link multiple object files
flex --link file1.o file2.o -o program.exe

# Interactive REPL
flex
```

---

## Implementation Status

### Complete ✅

| Feature | Status | Notes |
|---------|--------|-------|
| Lexer | ✅ Complete | Indentation-aware tokenization |
| Parser | ✅ Complete | Recursive descent, all syntax |
| Type Checker | ✅ Complete | Inference, generics support |
| Bytecode Compiler | ✅ Complete | Stack-based bytecode |
| VM Interpreter | ✅ Complete | Full bytecode execution |
| Native x64 Codegen | ✅ Complete | Direct machine code |
| PE Generator | ✅ Complete | Windows executable output |
| All Optimization Passes | ✅ Complete | O0-Ofast levels |
| Register Allocation | ✅ Complete | Local + global |
| SIMD Vectorization | ✅ Complete | SSE2/AVX support |
| Variables | ✅ Complete | Immutable, mutable, constants |
| Functions | ✅ Complete | Regular, single-expr, nested |
| Lambdas | ✅ Complete | Definition and calling |
| Lists | ✅ Complete | Creation, indexing, iteration |
| List Comprehensions | ✅ Complete | With filters |
| Control Flow | ✅ Complete | if/elif/else, while, for |
| Match Statements | ✅ Complete | With wildcard pattern |
| String Interpolation | ✅ Complete | `"Hello, {name}!"` |
| Records | ✅ Complete | Anonymous structs |
| Enums | ✅ Complete | With values |
| Modules | ✅ Complete | Inline modules |
| File Imports | ✅ Complete | `use "file.fx"` |
| Extern Functions | ✅ Complete | Windows DLL calls |
| Built-in Functions | ✅ Complete | print, len, time, etc. |

### Partial / Syntax Only ⚠️

| Feature | Status | Notes |
|---------|--------|-------|
| Async/Await | ✅ Working | spawn creates threads, await waits and gets result |
| Traits | ✅ Working | Syntax + type checking + vtable generation |
| Generics | ⚠️ Partial | Type erasure approach, no full monomorphization |
| Macros | ✅ Working | Full expression/statement cloning in expander |
| Syntax Macros | ⚠️ Partial | Parsing works, transform incomplete |
| Pattern Matching | ✅ Working | Literal, wildcard, and variable binding patterns |
| push/pop/len | ✅ Working | Runtime list operations with dynamic sizing |
| Try/Else | ✅ Working | Nil-coalescing pattern for error handling |

### Not Implemented ❌

| Feature | Status | Notes |
|---------|--------|-------|
| Garbage Collection | ❌ | Manual memory only |
| Debug Information | ❌ | No DWARF/PDB |
| Cross-Platform | ❌ | Windows x64 only |
| ARM64 Backend | ❌ | Planned |
| WebAssembly Backend | ❌ | Planned |
| Language Server | ❌ | Planned |
| Debugger | ❌ | Planned |
| Formatter | ❌ | Planned |

---

## Roadmap

### Phase 1: Core Compiler Completion (Current)

- [x] Lexer with indentation handling
- [x] Recursive descent parser
- [x] Type inference system
- [x] Bytecode compiler & VM
- [x] Native x64 code generation
- [x] PE executable generation
- [x] All optimization passes (O0-Ofast)
- [x] Register allocation
- [x] SIMD vectorization
- [x] Lambda codegen
- [x] List operations
- [ ] Fix module type checking
- [ ] Fix single-param function edge cases
- [ ] Pattern destructuring (`let (a, b) = tuple`)
- [ ] Complete macro execution

### Phase 2: Multi-Platform Support

- [ ] **ARM64 Backend**
  - [ ] ARM64 assembler
  - [ ] ARM64 instruction selection
  - [ ] ARM64 register allocation
  - [ ] macOS/Linux ARM64 executable format

- [ ] **Linux x64 Backend**
  - [ ] ELF executable generation
  - [ ] System call interface
  - [ ] Linux-specific built-ins

- [ ] **macOS x64 Backend**
  - [ ] Mach-O executable generation
  - [ ] macOS system calls
  - [ ] Code signing support

### Phase 3: WebAssembly Target

- [ ] **WASM Backend**
  - [ ] WASM binary format generation
  - [ ] WASM type system mapping
  - [ ] Memory management for WASM
  - [ ] JavaScript interop
  - [ ] Browser runtime support

- [ ] **WASI Support**
  - [ ] WASI system interface
  - [ ] File I/O for WASM
  - [ ] Network support

### Phase 4: Developer Tools

- [ ] **Language Server Protocol (LSP)**
  - [ ] Diagnostics (errors, warnings)
  - [ ] Go to definition
  - [ ] Find references
  - [ ] Hover information
  - [ ] Code completion
  - [ ] Rename symbol
  - [ ] Code actions

- [ ] **Debugger**
  - [ ] Debug information generation (DWARF/PDB)
  - [ ] Breakpoints
  - [ ] Step execution
  - [ ] Variable inspection
  - [ ] Call stack viewing
  - [ ] DAP (Debug Adapter Protocol) support

- [ ] **Formatter**
  - [ ] Consistent indentation
  - [ ] Line length limits
  - [ ] Import sorting
  - [ ] Configurable style

- [ ] **Linter**
  - [ ] Unused variable detection
  - [ ] Unreachable code warnings
  - [ ] Style enforcement
  - [ ] Security checks

### Phase 5: Runtime & Ecosystem

- [ ] **JIT Compiler**
  - [ ] Hot code detection
  - [ ] Runtime compilation
  - [ ] Deoptimization support
  - [ ] Game engine integration

- [ ] **Garbage Collector**
  - [ ] Generational GC
  - [ ] Concurrent collection
  - [ ] Low-latency mode

- [ ] **Standard Library Expansion**
  - [ ] Networking (HTTP, TCP, UDP)
  - [ ] File system operations
  - [ ] JSON/YAML/TOML parsing
  - [ ] Regular expressions
  - [ ] Cryptography
  - [ ] Database drivers
  - [ ] GUI bindings

- [ ] **Package Manager**
  - [ ] Package registry
  - [ ] Dependency resolution
  - [ ] Version management
  - [ ] Build system integration

### Phase 6: Advanced Features

- [ ] **Full Async Runtime**
  - [ ] Event loop
  - [ ] Async I/O
  - [ ] Task scheduling
  - [ ] Cancellation support

- [ ] **Complete Macro System**
  - [ ] Procedural macros
  - [ ] Derive macros
  - [ ] Attribute macros
  - [ ] Compile-time reflection

- [ ] **Advanced Type System**
  - [ ] Higher-kinded types
  - [ ] Associated types
  - [ ] Type classes
  - [ ] Dependent types (limited)

---

## Goals

### Ultimate Vision

Flex aims to be a **universal programming language** that:

1. **Compiles everywhere**: x64, ARM64, WASM, and future architectures
2. **Runs fast**: Native performance comparable to C/C++
3. **Stays simple**: Python-like syntax that's easy to read and write
4. **Adapts to change**: Syntax macros allow language evolution without breaking code
5. **Supports all use cases**: Systems programming, web development, game engines, scripting

### Design Principles

- **Simplicity over complexity**: Easy to learn, hard to misuse
- **Performance by default**: Zero-cost abstractions
- **Safety without ceremony**: Safe by default, unsafe when needed
- **Extensibility**: Macros and DSLs for domain-specific needs
- **Tooling first**: Great IDE support, debugging, and profiling

---

## Contributing

Contributions are welcome! Areas that need help:

1. **Backend development**: ARM64, Linux, macOS, WASM
2. **Tooling**: LSP, debugger, formatter
3. **Standard library**: More built-in functions
4. **Documentation**: Examples, tutorials, guides
5. **Testing**: More test cases, fuzzing

---

## License

MIT License

---

*Flex: Write less. Run fast.*
