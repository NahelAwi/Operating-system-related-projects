#include <math.h>
#include <unistd.h>

#define SIZE_LIMIT pow(10, 8)

void *smalloc(size_t size) {
    if (size == 0 || size > SIZE_LIMIT)
        return NULL;
    void *address = sbrk(size);
    if (address == (void *)-1)
        return NULL;
    else
        return address;
}