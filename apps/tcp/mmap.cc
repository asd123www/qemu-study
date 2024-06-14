#include <iostream>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

int main() {
    // Get the system page size (usually 4096 bytes on many systems)
    size_t pagesize = sysconf(_SC_PAGESIZE);
    size_t total_size = pagesize * 10000;

    // Allocate one page of memory
    void* addr = mmap(nullptr, total_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (addr == MAP_FAILED) {
        std::cerr << "Memory allocation failed." << std::endl;
        return 1;
    }

    // Write data into the allocated memory
    char* data = static_cast<char*>(addr);
    for (int i = 0; i < total_size; i++) data[i] = rand() % 26 + 'a';


    while (1) {
        puts("waiting.");
        sleep(5);
    }

    // Clean up: unmap the memory
    if (munmap(addr, pagesize) == -1) {
        std::cerr << "Memory deallocation failed." << std::endl;
        return 1;
    }
    
    return 0;
}