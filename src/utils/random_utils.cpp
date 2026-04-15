#include "random_utils.hpp"
#include <cstdint>
#include <cstdio>
#include <print>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace blackbox {
namespace tools {

uint64_t get_true_random() {
#ifdef _WIN32
    uint64_t num;
    if (!BCRYPT_SUCCESS(BCryptGenRandom(NULL, reinterpret_cast<PUCHAR>(&num), (ULONG) sizeof(num),
                                        BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
        std::println(stderr, "Random generation failed");
        return 0;
    }
    return num;
#else
    uint64_t num = 0;
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        perror("open /dev/urandom");
        return 0;
    }

    ssize_t n = read(fd, &num, sizeof(num));
    if (n != sizeof(num)) {
        fprintf(stderr, "Failed to read 8 bytes from /dev/urandom\n");
        close(fd);
        return 0;
    }

    close(fd);
    return num;
#endif
}

} // namespace tools
} // namespace blackbox
