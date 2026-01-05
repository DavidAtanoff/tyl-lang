// File I/O test without if blocks

// Write to file
handle = open("test2.txt", "w")
write(handle, "Test content here!")
close(handle)

// Read from file
handle = open("test2.txt", "r")
size = file_size(handle)
println("Size: ", size)
content = read(handle, size)
println("Read content: ", content)
close(handle)

println("Test complete!")
