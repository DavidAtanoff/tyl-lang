// Test file I/O operations

// Test 1: Write to a file
println("Test 1: Writing to file...")
handle = open("test_output.txt", "w")
if handle != -1:
    bytes = write(handle, "Hello from Flex!\n")
    println("Wrote bytes: ", bytes)
    close(handle)
    println("File written successfully")
else:
    println("Failed to open file for writing")

// Test 2: Read from the file we just wrote
println("")
println("Test 2: Reading from file...")
handle = open("test_output.txt", "r")
if handle != -1:
    size = file_size(handle)
    println("File size: ", size)
    content = read(handle, size)
    println("Content: ", content)
    close(handle)
else:
    println("Failed to open file for reading")

// Test 3: Append to file
println("")
println("Test 3: Appending to file...")
handle = open("test_output.txt", "a")
if handle != -1:
    write(handle, "Appended line!\n")
    close(handle)
    println("Append successful")
else:
    println("Failed to open file for appending")

// Test 4: Read again to verify append
println("")
println("Test 4: Verifying append...")
handle = open("test_output.txt", "r")
if handle != -1:
    size = file_size(handle)
    content = read(handle, size)
    println("Final content:")
    println(content)
    close(handle)
else:
    println("Failed to open file")

println("")
println("File I/O tests complete!")
