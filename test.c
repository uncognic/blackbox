#include <stdio.h>
#include <stdint.h>
#include "src/source/source.h"

int main(void) {
    uint64_t result = bb_add(2, 2);

    if (result != 4) {
        fprintf(stderr, "FAIL: expected 4, got %llu\n",
                (unsigned long long)result);
        return 1;
    }

    printf("PASS: bb_add(2, 2) = %llu\n",
           (unsigned long long)result);
    return 0;
}