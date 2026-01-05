# Flex Language Roadmap

## Vision
Flex aims to be the definitive low-level systems language - combining the performance of C, the safety features of Rust, and syntax cleaner than all of them.

---

## Current Status: v0.9 (Feature Complete Syntax)

All 34 syntax features implemented. Native x64 Windows PE generation working.

---

## Phase 1: Polish & Enhancement (v1.0)

### 1.1 Fix Existing Features ✅

| Feature | Current State | Target State |
|---------|---------------|--------------|
| `with` blocks | No cleanup call | Auto-call `.close()` or `.__del__()` on scope exit |✅
| `is` type check | Always returns true | Runtime type info (RTTI) for dynamic checks |✅
| `comptime` | Runs at runtime | True compile-time function evaluation (CTFE) |✅
| `_` placeholder | Only in match arms | Support `_ * 2`, `_.field`, `f(_)` everywhere |✅
| Refinement types | Runtime checks only | Compile-time verification where possible |✅

### 1.2 Additional Numeric Types ✅

**Implemented:**
- ✅ Extended integer types: `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`
- ✅ Extended float types: `f16`, `f32`, `f64`, `f128`
- ✅ Complex number types: `c64`, `c128` with `complex()`, `real()`, `imag()` builtins
- ✅ Numeric literal suffixes (`42i32`, `3.14f64`, etc.)
- ✅ Arbitrary precision types (type system): `BigInt`, `BigFloat`, `Decimal`, `Rational`
- ✅ Fixed-point type (type system): `Fixed[N, F]`
- ✅ SIMD vector types (type system): `Vec2[T]`, `Vec3[T]`, `Vec4[T]`
- ✅ SIMD matrix types (type system): `Mat2[T]`, `Mat3[T]`, `Mat4[T]`
- ✅ BigInt runtime operations: `bigint()`, `bigint_add()`, `bigint_to_int()`
- ✅ Rational runtime operations: `rational()`, `rational_add()`, `rational_to_float()`
- ✅ Fixed-point runtime operations: `fixed()`, `fixed_add()`, `fixed_sub()`, `fixed_mul()`, `fixed_to_float()`
- ✅ Vec3 runtime operations: `vec3()`, `vec3_add()`, `vec3_dot()`, `vec3_length()`

### 1.3 String Enhancements ✅

**Implemented:**
- ✅ Character type (`char`) - Unicode scalar value (32-bit)
- ✅ Character literals: `'A'`, `'\n'`, `'\t'`, `'\\'`, `'\''`
- ✅ Hex escape in chars: `'\x41'`
- ✅ Unicode escape in chars: `'\u{1F600}'`
- ✅ Byte strings: `b"hello"`, `b"\x00\xFF"`
- ✅ Raw byte strings: `br"\x00\xFF"` (no escape processing)
- ✅ String slicing: `"hello"[1..4]` returns `str_view`
- ✅ Inclusive string slicing: `"hello"[1..=4]`

```tyl
// Character type
char                    // Unicode scalar value (32-bit)
c = 'A'
c = '\n'
c = '\u{1F600}'         // Emoji

// String views (non-owning)
str_view                // Borrowed string slice
s: str_view = "hello"[1..4]   // Exclusive range
s: str_view = "hello"[1..=4]  // Inclusive range

// Byte strings
b"hello"                // [u8] literal
br"\x00\xFF"            // Raw byte string
```

---

## Phase 2: Memory Safety (v1.1)

### 2.1 Ownership System ✅

**Implemented:**
- ✅ Move semantics for non-Copy types (lists, strings, maps, records)
- ✅ Copy semantics for primitives (int, float, bool, char, pointers)
- ✅ Use-after-move detection at compile time
- ✅ Ownership restoration on reassignment
- ✅ `.clone()` method for deep copy (lists, records)
- ✅ Proper list structure with headers for constant and GC-allocated lists
- ✅ Move semantics codegen for list variable assignments

```tyl
// Owned values (default) - single owner, auto-freed
fn process data: [int]:
    // data is owned, freed when function returns
    transform(data)

// Move semantics
a = [1, 2, 3]
b = a                   // a is moved to b, a is invalid
// println(a)           // Compile error: a was moved

// Explicit copy
a = [1, 2, 3]
b = a.clone()           // Deep copy
println(a)              // OK: a still valid

// Reassignment restores ownership
mut e = [7, 8, 9]
f = e                   // e is moved to f
e = [10, 11, 12]        // e is valid again with new list
```

**Implemented:**
- ✅ Drop trait for custom cleanup - automatic drop calls at scope exit
- ✅ Drop calls in reverse declaration order
- ✅ Types with Drop trait forced to stack allocation for proper cleanup
- ✅ Move semantics integration with Drop (skip drop for moved values)
- ✅ Type propagation for moved record variables with Drop trait

### 2.2 Borrow Checker ✅

**Implemented:**
- ✅ BorrowExpr AST node for safe borrows (`&` and `&mut`)
- ✅ Immutable borrow syntax: `&x`
- ✅ Mutable borrow syntax: `&mut x`
- ✅ Reference parameter types: `fn foo(x: &[int])`, `fn bar(x: &mut [int])`
- ✅ Borrow tracking in type checker
- ✅ Error: cannot borrow immutable variable as mutable
- ✅ Error: cannot borrow as mutable more than once
- ✅ Error: cannot borrow as mutable while borrowed as immutable
- ✅ Codegen for BorrowExpr (generates address-of code)
- ✅ Type compatibility for reference types in function calls
- ✅ Lifetime annotations parsing: `&'a T`, `&'a mut T`
- ✅ Lifetime parameters in functions: `fn longest['a](a: &'a str, b: &'a str) -> &'a str`
- ✅ Lifetime tracking infrastructure in ownership system
- ✅ Auto-dereference on return: returning a borrow parameter with non-reference return type automatically dereferences

```tyl
// Immutable borrows (multiple allowed)
fn print_all items: &[int] -> int:
    for item in items:
        println(item)
    return len(items)

// Mutable borrow (exclusive)
fn add_one items: &mut [int] -> int:
    items[1] = 100
    return items[1]

// Auto-dereference on return
fn get_val(x: &int) -> int:
    return x              // Automatically dereferences, returns value not address

// Borrow rules enforced at compile time
mut data = [1, 2, 3]
ref1 = &data            // OK: immutable borrow
ref2 = &data            // OK: multiple immutable borrows
// mut_ref = &mut data  // ERROR: can't mut borrow while immutably borrowed

// Lifetime annotations
fn longest['a](a: &'a str, b: &'a str) -> &'a str:
    if len(a) > len(b):
        return a
    return b
```

**Implemented:**
- ✅ Lifetimes (inferred where possible)
- ✅ Lifetime annotations for complex patterns: `fn longest['a](a: &'a str, b: &'a str) -> &'a str`
- ✅ Lifetime parameter parsing in function declarations
- ✅ Lifetime tracking in ownership system
- ✅ Lifetime constraint enforcement during type checking
- ✅ Lifetime elision rules for common patterns (single input, &self methods)

### 2.3 Smart Pointers ✅

**Implemented:**
- ✅ `Box[T]` - Unique ownership heap allocation with `Box(value)` or `Box[T](value)`
- ✅ `Rc[T]` - Reference counted (single-threaded) with `Rc(value)` or `Rc[T](value)`
- ✅ `Arc[T]` - Atomic reference counted (thread-safe) with `Arc(value)` or `Arc[T](value)`
- ✅ `Weak[T]` - Non-owning weak references with `.downgrade()` method
- ✅ `Cell[T]` - Interior mutability for Copy types with `Cell(value)` or `Cell[T](value)`
- ✅ `RefCell[T]` - Runtime borrow checking with `RefCell(value)` or `RefCell[T](value)`
- ✅ `Mutex[T]` - Thread-safe (already implemented)
- ✅ `RwLock[T]` - Reader-writer lock (already implemented)

```tyl
// Unique ownership (heap)
Box[T]                  // Single owner, heap allocated
ptr = Box(42)           // Type inferred
ptr = Box[int](42)      // Explicit type
value = ptr.get()       // Get value

// Reference counted
Rc[T]                   // Single-threaded ref counting
shared = Rc(100)
shared2 = shared.clone()
count = shared.strong_count()

Arc[T]                  // Atomic ref counting (thread-safe)
atomic = Arc(200)
atomic2 = atomic.clone()

// Weak references
Weak[T]                 // Non-owning, doesn't prevent deallocation
weak = shared.downgrade()
upgraded = weak.upgrade()

// Interior mutability
Cell[T]                 // Single-threaded mutable container
cell = Cell(10)
cell.set(20)
val = cell.get()

RefCell[T]              // Runtime borrow checking
refcell = RefCell(30)
refcell.set(40)
val = refcell.get()

Mutex[T]                // Thread-safe (already implemented)
RwLock[T]               // Reader-writer lock (already implemented)
```

---

## Phase 3: Advanced Type System (v1.2)

### 3.1 Dependent Types ✅

**Implemented:**
- ✅ Dependent type declarations: `type Vector[T, N: int] = [T; N]`
- ✅ Multi-dimensional fixed arrays: `[[int; 2]; 2]`
- ✅ Nested array access: `mat[1][1]`, `mat[2][2]`
- ✅ 1-based indexing for all arrays (fixed and dynamic)
- ✅ Proper element size calculation for nested arrays
- ✅ Type propagation for array slices

```tyl
// Types that depend on values
type Vector[T, N: int] = [T; N]

fn dot[T: Numeric, N: int] a: Vector[T, N], b: Vector[T, N] -> T:
    sum = 0
    for i in 0..N:
        sum += a[i] * b[i]
    return sum

// Compile-time dimension checking
v1: Vector[f64, 3] = [1.0, 2.0, 3.0]
v2: Vector[f64, 3] = [4.0, 5.0, 6.0]
result = dot(v1, v2)    // OK: same dimensions

v3: Vector[f64, 4] = [1.0, 2.0, 3.0, 4.0]
// dot(v1, v3)          // ERROR: dimension mismatch (3 vs 4)

// Proof-carrying code
type NonEmpty[T] = [T] where len(_) > 0

fn head[T] list: NonEmpty[T] -> T:
    return list[0]      // Safe: guaranteed non-empty
```

### 3.2 Algebraic Effects ✅

**Implemented:**
- ✅ Effect declaration syntax: `effect Error[E]:`
- ✅ Effect operations: `fn raise e: E -> never`
- ✅ Perform effect operations: `perform Error.raise("message")`
- ✅ Handle expressions with pattern matching: `handle expr: Effect.op(x) => body`
- ✅ Resume expressions for continuations: `resume(value)`
- ✅ Effect type tracking in type system
- ✅ Effect handler registration and lookup
- ✅ Continuation-based effect handling (stack-based)
- ✅ Multiple effect handlers in single handle block
- ✅ Effect type parameters: `effect State[S]:`
- ✅ Function calls inside handle blocks (fixed: optimizer now tracks calls in HandleExpr)
- ✅ Handler code generation with proper stack frames
- ✅ Effect runtime initialization with global handler stack

```tyl
// Define effects
effect Error[E]:
    fn raise e: E -> never

effect State[S]:
    fn get -> S
    fn put s: S

effect Async:
    fn await[T] future: Future[T] -> T

// Use effects in functions
fn divide a: int, b: int -> int with Error[str]:
    if b == 0:
        perform Error.raise("division by zero")
    return a / b

fn counter -> int with State[int]:
    n = perform State.get()
    perform State.put(n + 1)
    return n

// Handle effects - function calls work correctly inside handle blocks
fn get_value -> int:
    return 42

result = handle get_value():
    Error.raise(e) => 
        println("Error: {e}")
        0

// Compose effects
fn complex_op -> int with Error[str], State[int], Async:
    data = perform Async.await(fetch_data())
    count = perform State.get()
    if data.invalid:
        perform Error.raise("invalid data")
    perform State.put(count + 1)
    return process(data)
```

### 3.3 Higher-Kinded Types

```tyl
// Type constructors as parameters
trait Functor[F[_]]:
    fn map[A, B] fa: F[A], f: fn(A) -> B -> F[B]

trait Monad[M[_]]: Functor[M]:
    fn pure[A] a: A -> M[A]
    fn flatMap[A, B] ma: M[A], f: fn(A) -> M[B] -> M[B]

// Implement for Option
impl Functor for Option:
    fn map[A, B] fa: Option[A], f: fn(A) -> B -> Option[B]:
        match fa:
            Some(a) => Some(f(a))
            None => None

// Generic over any Monad
fn sequence[M[_]: Monad, A] list: [M[A]] -> M[[A]]:
    ...
```

### 3.4 Type Classes / Concepts

```tyl
// Constrained generics
concept Numeric[T]:
    fn add(T, T) -> T
    fn mul(T, T) -> T
    fn zero() -> T
    fn one() -> T

concept Orderable[T]:
    fn compare(T, T) -> Ordering

// Use in functions
fn sum[T: Numeric] items: [T] -> T:
    result = T.zero()
    for item in items:
        result = result.add(item)
    return result

fn sort[T: Orderable] items: &mut [T]:
    ...
```

---

## Phase 4: Compile-Time Execution (v1.3)

### 4.1 True CTFE (Compile-Time Function Evaluation)

```tyl
// Compile-time computation
comptime fn factorial n: int -> int:
    if n <= 1: return 1
    return n * factorial(n - 1)

// Evaluated at compile time, result embedded in binary
FACT_20 :: factorial(20)

// Compile-time loops
comptime:
    CRC_TABLE: [u32; 256] = []
    for i in 0..256:
        crc = i
        for _ in 0..8:
            if crc & 1:
                crc = (crc >> 1) ^ 0xEDB88320
            else:
                crc >>= 1
        CRC_TABLE[i] = crc

// Compile-time type generation
comptime fn make_tuple_type n: int -> Type:
    fields = []
    for i in 0..n:
        fields.push(("_{i}", int))
    return record_type(fields)

Tuple5 :: make_tuple_type(5)
```

### 4.2 Compile-Time Reflection

```tyl
// Introspect types at compile time
comptime fn fields_of[T] -> [(str, Type)]:
    return T.__fields__

comptime fn methods_of[T] -> [str]:
    return T.__methods__

// Generate code based on type structure
comptime fn derive_debug[T] -> fn(T) -> str:
    code = "fn debug(self: T) -> str:\n"
    code += "    return \"{T.__name__} {\"\n"
    for (name, _) in fields_of[T]():
        code += "        + \"{name}: \" + str(self.{name}) + \", \"\n"
    code += "    + \"}\"\n"
    return compile(code)

@derive(Debug, Clone, Eq)
record Point:
    x: int
    y: int
```

### 4.3 Compile-Time Assertions

```tyl
comptime assert sizeof(Header) == 16, "Header must be 16 bytes"
comptime assert alignof(Data) >= 8, "Data must be 8-byte aligned"
comptime assert is_pod[MyStruct], "MyStruct must be POD"

// Static bounds checking
fn get[T, N: int] arr: [T; N], idx: int -> T
    where idx >= 0 and idx < N:
    return arr[idx]
```

---

## Phase 5: LLVM Backend (v2.0)

### 5.1 LLVM IR Generation Integration

Add more stuff to custom x64 codegen with LLVM IR Src for:
- Cross-platform support (x86, ARM, RISC-V, WebAssembly)
- Battle-tested optimization passes
- Debug info generation (DWARF)
- Profile-guided optimization (PGO)

### 5.2 Optimization Passes

| Pass | Description |
|------|-------------|
| mem2reg | Promote memory to registers (SSA) |
| instcombine | Algebraic simplification |
| gvn | Global value numbering |
| licm | Loop-invariant code motion |
| loop-unroll | Loop unrolling |
| loop-vectorize | Auto-vectorization |
| inline | Function inlining |
| dce | Dead code elimination |
| dse | Dead store elimination |
| sccp | Sparse conditional constant propagation |
| tailcallelim | Tail call optimization |
| reassociate | Reassociate expressions |
| simplifycfg | Control flow simplification |

### 5.3 Optimization Levels

```bash
flex build                  # -O0: No optimization (fast compile)
flex build --release        # -O2: Standard optimization
flex build --release-fast   # -O3: Aggressive optimization
flex build --release-small  # -Os: Size optimization
flex build --release-lto    # -O2 + LTO: Link-time optimization
```

### 5.4 Target Platforms

| Platform | Architecture | Status |
|----------|--------------|--------|
| Windows | x86-64 | ✅ Current (native) |
| Windows | x86-64 | 🔄 LLVM backend |
| Windows | ARM64 | 📋 Planned |
| Linux | x86-64 | 📋 Planned |
| Linux | ARM64 | 📋 Planned |
| macOS | x86-64 | 📋 Planned |
| macOS | ARM64 (M1/M2) | 📋 Planned |
| WebAssembly | wasm32 | 📋 Planned |
| Embedded | ARM Cortex-M | 📋 Planned |
| Embedded | RISC-V | 📋 Planned |

---

## Phase 6: Developer Tools (v2.1)

### 6.1 Formatter

```bash
flex fmt                    # Format current directory
flex fmt src/               # Format specific directory
flex fmt --check            # Check without modifying
flex fmt --config flex.toml # Custom config
```

Configuration:
```toml
[format]
indent = 4
max_line_length = 100
brace_style = "same_line"   # or "next_line"
trailing_comma = true
sort_imports = true
```

### 6.2 Package Manager

```bash
flex init my_project        # Create new project
flex add json               # Add dependency
flex add http --features tls
flex remove json
flex update                 # Update dependencies
flex publish                # Publish to registry
flex search "http client"   # Search packages
```

Manifest (flex.toml):
```toml
[package]
name = "my_project"
version = "1.0.0"
authors = ["Name <email>"]
license = "MIT"
repository = "https://github.com/user/project"

[dependencies]
json = "2.1.0"
http = { version = "1.0", features = ["tls", "http2"] }
local_lib = { path = "../local" }
git_lib = { git = "https://github.com/user/lib", branch = "main" }

[dev-dependencies]
test_utils = "1.0"

[build]
target = "x86_64-windows"
opt_level = 2

[features]
default = ["std"]
async = ["runtime"]
no_std = []
```

### 6.3 Build System

```bash
flex build                  # Debug build
flex build --release        # Release build
flex build --target linux-x64
flex build --features "async,tls"
flex clean                  # Clean build artifacts
flex run                    # Build and run
flex run --release
flex run -- arg1 arg2       # Pass args to program
```

### 6.4 REPL

```bash
flex repl                   # Interactive mode

>>> x = 42
42
>>> x * 2
84
>>> fn double n => n * 2
<function double>
>>> double(21)
42
>>> :type x
int
>>> :help
Commands:
  :help     Show this help
  :type X   Show type of expression
  :ast X    Show AST of expression
  :quit     Exit REPL
```

### 6.5 Language Server (LSP)

Features:
- Syntax highlighting
- Error diagnostics
- Auto-completion
- Go to definition
- Find references
- Rename symbol
- Hover documentation
- Code actions (quick fixes)
- Formatting
- Inlay hints

### 6.6 Debugger Integration

```bash
flex build --debug          # Include debug symbols
flex debug ./program        # Launch with debugger
```

Features:
- Breakpoints
- Step in/over/out
- Variable inspection
- Call stack
- Watch expressions
- Memory view
- Conditional breakpoints

### 6.7 Testing Framework

```tyl
// In tests/test_math.tyl
use testing

@test
fn test_addition:
    assert_eq(1 + 1, 2)
    assert_eq(add(2, 3), 5)

@test
fn test_division:
    assert_eq(10 / 2, 5)
    assert_panic(|| divide(1, 0))

@test
@ignore("not implemented yet")
fn test_future_feature:
    ...

@test
@benchmark
fn bench_sort:
    data = random_list(10000)
    sort(data)
```

```bash
flex test                   # Run all tests
flex test test_math         # Run specific test file
flex test --filter "sort"   # Filter by name
flex test --benchmark       # Run benchmarks
flex test --coverage        # Generate coverage report
```

### 6.8 Documentation Generator

```tyl
/// Calculate the factorial of a number.
/// 
/// # Arguments
/// * `n` - The number to calculate factorial for
/// 
/// # Returns
/// The factorial of n
/// 
/// # Examples
/// ```
/// assert_eq(factorial(5), 120)
/// assert_eq(factorial(0), 1)
/// ```
/// 
/// # Panics
/// Panics if n is negative
fn factorial n: int -> int:
    require n >= 0, "n must be non-negative"
    if n <= 1: return 1
    return n * factorial(n - 1)
```

```bash
flex doc                    # Generate documentation
flex doc --open             # Generate and open in browser
flex doc --json             # Output as JSON
```

---

## Phase 7: Standard Library (v2.2)

### Core Modules

```
std/
├── core/           # Primitives, traits, memory
│   ├── types.tyl    # Primitive types
│   ├── traits.tyl   # Core traits (Copy, Clone, Drop, etc.)
│   ├── mem.tyl      # Memory operations
│   └── ptr.tyl      # Pointer utilities
├── collections/    # Data structures
│   ├── list.tyl     # Dynamic array
│   ├── map.tyl      # Hash map
│   ├── set.tyl      # Hash set
│   ├── deque.tyl    # Double-ended queue
│   ├── heap.tyl     # Binary heap
│   └── btree.tyl    # B-tree map/set
├── io/             # Input/output
│   ├── file.tyl     # File operations
│   ├── stream.tyl   # Stream traits
│   ├── buffer.tyl   # Buffered I/O
│   └── path.tyl     # Path manipulation
├── net/            # Networking
│   ├── tcp.tyl      # TCP sockets
│   ├── udp.tyl      # UDP sockets
│   ├── http.tyl     # HTTP client/server
│   └── tls.tyl      # TLS/SSL
├── sync/           # Synchronization
│   ├── mutex.tyl    # Mutex
│   ├── rwlock.tyl   # Read-write lock
│   ├── channel.tyl  # Channels
│   ├── atomic.tyl   # Atomics
│   └── barrier.tyl  # Barrier
├── async/          # Async runtime
│   ├── task.tyl     # Task/Future
│   ├── executor.tyl # Task executor
│   └── timer.tyl    # Async timers
├── text/           # Text processing
│   ├── string.tyl   # String utilities
│   ├── regex.tyl    # Regular expressions
│   ├── unicode.tyl  # Unicode utilities
│   └── fmt.tyl      # Formatting
├── math/           # Mathematics
│   ├── basic.tyl    # Basic math functions
│   ├── trig.tyl     # Trigonometry
│   ├── random.tyl   # Random numbers
│   ├── bigint.tyl   # Arbitrary precision
│   └── complex.tyl  # Complex numbers
├── time/           # Date and time
│   ├── instant.tyl  # Monotonic time
│   ├── datetime.tyl # Calendar time
│   └── duration.tyl # Time spans
├── os/             # OS interface
│   ├── env.tyl      # Environment variables
│   ├── process.tyl  # Process management
│   ├── fs.tyl       # Filesystem
│   └── signal.tyl   # Signal handling
├── encoding/       # Data encoding
│   ├── json.tyl     # JSON
│   ├── xml.tyl      # XML
│   ├── base64.tyl   # Base64
│   └── binary.tyl   # Binary formats
└── testing/        # Testing utilities
    ├── assert.tyl   # Assertions
    ├── mock.tyl     # Mocking
    └── bench.tyl    # Benchmarking
```

---

## Timeline

| Phase | Version | Target | Focus |
|-------|---------|--------|-------|
| 1 | v1.0 | Q2 2026 | Polish existing features |
| 2 | v1.1 | Q3 2026 | Memory safety (ownership/borrowing) |
| 3 | v1.2 | Q4 2026 | Advanced type system |
| 4 | v1.3 | Q1 2027 | Compile-time execution |
| 5 | v2.0 | Q2 2027 | LLVM backend, cross-platform |
| 6 | v2.1 | Q3 2027 | Developer tools |
| 7 | v2.2 | Q4 2027 | Standard library |

---

## Design Principles

1. **Zero-cost abstractions** - High-level features compile to optimal code
2. **Explicit over implicit** - No hidden allocations, copies, or control flow
3. **Compile-time over runtime** - Catch errors early, optimize aggressively
4. **Minimal syntax, maximum power** - Every character earns its place
5. **Interop first** - Easy FFI with C, system libraries, and existing code
6. **Predictable performance** - No GC pauses, no hidden costs

---

## Contributing

See CONTRIBUTING.md for guidelines on:
- Code style
- Testing requirements
- Pull request process
- Issue reporting
