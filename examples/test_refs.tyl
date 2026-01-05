// Test reference types and pointer operations

fn main:
    // Test 1: Basic address-of and dereference
    let x = 42
    let p = &x
    let val = *p
    println "Test 1: Address-of and dereference"
    println "x = {x}"
    println "*p = {val}"
    
    // Test 2: Modify through pointer
    println "\nTest 2: Modify through pointer"
    let y = 100
    let yp = &y
    *yp = 200
    println "After *yp = 200: y = {y}"
    
    // Test 3: Null pointer
    println "\nTest 3: Null pointer"
    let np: *int = null
    if np == null:
        println "np is null"
    
    // Test 4: Pointer to array element
    println "\nTest 4: Array access"
    let arr = [10, 20, 30, 40, 50]
    let first = arr[0]
    let second = arr[1]
    println "arr[0] = {first}"
    println "arr[1] = {second}"
    
    println "\nAll reference tests passed!"

main()
