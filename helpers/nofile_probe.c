#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <unistd.h>

int main(void)
{
    struct rlimit limit;
    if (getrlimit(RLIMIT_NOFILE, &limit) == -1) {
        perror("getrlimit");
        return 1;
    }

    printf("RLIMIT_NOFILE inside helper: soft=");
    if (limit.rlim_cur == RLIM_INFINITY) {
        printf("unlimited");
    } else {
        printf("%llu", (unsigned long long)limit.rlim_cur);
    }
    printf(" hard=");
    if (limit.rlim_max == RLIM_INFINITY) {
        printf("unlimited");
    } else {
        printf("%llu", (unsigned long long)limit.rlim_max);
    }
    putchar('\n');

    size_t capacity = 32;
    size_t count = 0;
    int *descriptors = malloc(capacity * sizeof(*descriptors));
    if (descriptors == NULL) {
        perror("malloc");
        return 1;
    }

    for (;;) {
        int descriptor = open("/dev/null", O_RDONLY | O_CLOEXEC);
        if (descriptor == -1) {
            if (errno != EMFILE) {
                perror("open /dev/null");
                for (size_t i = 0; i < count; ++i) {
                    close(descriptors[i]);
                }
                free(descriptors);
                return 1;
            }
            break;
        }

        if (count == capacity) {
            size_t new_capacity = capacity * 2;
            int *new_descriptors = realloc(descriptors, new_capacity * sizeof(*new_descriptors));
            if (new_descriptors == NULL) {
                perror("realloc");
                close(descriptor);
                for (size_t i = 0; i < count; ++i) {
                    close(descriptors[i]);
                }
                free(descriptors);
                return 1;
            }
            descriptors = new_descriptors;
            capacity = new_capacity;
        }
        descriptors[count++] = descriptor;
    }

    printf("Additional descriptors opened before EMFILE: %zu\n", count);
    puts("open() failed with EMFILE as expected");
    for (size_t i = 0; i < count; ++i) {
        close(descriptors[i]);
    }
    free(descriptors);
    return 0;
}
