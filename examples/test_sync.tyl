// Test synchronization primitives

fn main:
    // Test Mutex creation
    let m: Mutex[i32] = Mutex[i32]
    print("Mutex created")
    
    // Test lock block
    lock m:
        print("Inside lock block")
    
    print("After lock block")
    
    // Test Semaphore
    let sem: Semaphore = Semaphore(1, 10)
    print("Semaphore created")
    
    // Test Condition variable
    let cond: Cond = Cond()
    print("Condition variable created")
    
    // Test RWLock
    let rw: RWLock[i32] = RWLock[i32]
    print("RWLock created")
    
    print("All sync primitives created successfully!")
    return 0

main()
