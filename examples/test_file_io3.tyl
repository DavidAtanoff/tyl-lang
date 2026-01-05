// File I/O test with if block - debug

handle = open("test3.txt", "w")
write(handle, "Hello!")
close(handle)

handle = open("test3.txt", "r")
println("Handle: ", handle)

if handle != -1:
    println("Inside if block")
    content = read(handle, 10)
    println("After read, content is: ", content)
    close(handle)
else:
    println("Failed to open")

println("Done")
