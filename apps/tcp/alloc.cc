#include <iostream>
#include <random>
#include <memory>
#include <sys/mman.h>
#include <unistd.h>

int main() {
    size_t num_bytes = 1024;  // Number of bytes to allocate
    std::cin>>num_bytes;
    printf("Allocate %ld bytes.", num_bytes);

    // Allocate memory
    std::unique_ptr<uint8_t[]> buffer(new uint8_t[num_bytes]);

    // Random number generation setup
    std::random_device rd;  // Non-deterministic generator
    std::mt19937 gen(rd()); // Standard mersenne_twister_engine seeded with rd()
    std::uniform_int_distribution<> distrib(0, 255); // Range for bytes

    // Write random bytes into the allocated memory
    for (size_t i = 0; i < num_bytes; ++i) {
        buffer[i] = static_cast<uint8_t>(distrib(gen));
    }

    puts("Finish writing.");

    while (1) {
        puts("Waiting.");
        sleep(5);
    }

    // No need to manually delete, unique_ptr handles it
    return 0;
}