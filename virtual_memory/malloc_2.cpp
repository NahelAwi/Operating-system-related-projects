#include <math.h>
#include <string.h>
#include <unistd.h>

#define SIZE_LIMIT pow(10, 8)

// point of interest (checking with tests): should size be the allocation size
// or the overall size (including the metadata), givin that we assume the user asked
// for "size" bytes and we would be giving him less if so.

//Answer: according to the tests, size is what the user wants and does not include meta data

void *smalloc(size_t size);
void *scalloc(size_t num, size_t size);
void sfree(void *p);
void *srealloc(void *oldp, size_t size);
size_t _num_free_blocks();
size_t _num_free_bytes();
size_t _num_allocated_blocks();
size_t _num_allocated_bytes();
size_t _num_meta_data_bytes();
size_t _size_meta_data();

struct MallocMetadata {
    size_t size;
    bool is_free;
    MallocMetadata *next;
    MallocMetadata *prev;
};

struct MemoryList {
    MallocMetadata *head = NULL;

    size_t num_free_blocks = 0;
    size_t num_free_bytes = 0;

    size_t num_allocated_blocks = 0;
    size_t num_allocated_bytes = 0;

    size_t num_meta_data_bytes = 0;
};

static MemoryList Heap;

// New allocation adjustment
void newAllocAdjustment(size_t size) {
    Heap.num_allocated_blocks += 1;
    Heap.num_allocated_bytes += size;
    Heap.num_meta_data_bytes += sizeof(MallocMetadata);
}

// start the memory list
void *firstAllocation(size_t size) {
    void *meta_data_address = sbrk(size + sizeof(MallocMetadata)); // TODO CHECK IF SIZEOF(STRUCT) WORKS PROPERLY
    if (meta_data_address == (void *)-1)                           // PASSED THE TESTS SO APPARENTLY YES
        return NULL;
    void *address = (void*)((MallocMetadata *)meta_data_address + 1);//pointer fix FINAL
    Heap.head = (MallocMetadata *)meta_data_address;
    Heap.head->is_free = false;
    Heap.head->next = NULL;
    Heap.head->prev = NULL;
    Heap.head->size = size;
    newAllocAdjustment(size);
    return address;
}

void *smalloc(size_t size) {
    if (size == 0 || size > SIZE_LIMIT)
        return NULL;

    // if its our first allocation
    if (Heap.head == NULL) {
        return firstAllocation(size);
    }

    // search for a possible memory space
    MallocMetadata *ptr = Heap.head;
    MallocMetadata *prev;
    do {
        if (ptr->is_free && ptr->size >= size) {
            ptr->is_free = false;
            //ptr->size = size; // TODO CHECK IF THERE IS NEED TO CHANGE
                              // THE SIZE INSTEAD OF USING ALL THE ALREADY ALLOCATED SPACE
                              // this option was not covered by the tests!!!!!
            Heap.num_free_blocks -= 1;
            Heap.num_free_bytes -= ptr->size; // size;=>CAUSES FRAGMENTATION AND FREE BYTES NUMBER IS NOT CONSISTENT
            void *address = (void*)(ptr + 1); 
            return address;
        }
        prev = ptr;
        ptr = ptr->next;
    } while (ptr != NULL);

    // if there is no already allocated space, allocate new
    void *meta_data_address = sbrk(size + sizeof(MallocMetadata));
    if (meta_data_address == (void *)-1)
        return NULL;
    void *address = (void*)((MallocMetadata *)meta_data_address + 1);
    MallocMetadata *new_allocation = (MallocMetadata *)meta_data_address;
    new_allocation->is_free = false;
    new_allocation->size = size;
    new_allocation->next = NULL;
    new_allocation->prev = prev;
    prev->next = new_allocation;
    newAllocAdjustment(size);
    return address;
}

void *scalloc(size_t num, size_t size) {
    void *address = smalloc(num * size);
    if (address == NULL)
        return NULL;
    memset(address, 0, num * size);
    return address;
}

void sfree(void *p) {
    if (p == NULL)
        return;
    MallocMetadata *P_meta_data = (MallocMetadata *)p - 1;
    if (P_meta_data->is_free == false) {//this option was not covered by the tests
        P_meta_data->is_free = true;
        Heap.num_free_blocks += 1;
        Heap.num_free_bytes += P_meta_data->size;
    }
    return;
}

void *srealloc(void *oldp, size_t size) {
    if (oldp == NULL)
        return smalloc(size);

    MallocMetadata *oldp_meta_data = (MallocMetadata *)oldp - 1;
    if (oldp_meta_data->size >= size)
        return oldp;

    void *newp = smalloc(size);
    if (newp == NULL)
        return NULL;
    memmove(newp, oldp, oldp_meta_data->size);

    sfree(oldp);
    return newp;
}

size_t _num_free_blocks() {
    return Heap.num_free_blocks;
}

size_t _num_free_bytes() {
    return Heap.num_free_bytes;
}

size_t _num_allocated_blocks() {
    return Heap.num_allocated_blocks;
}

size_t _num_allocated_bytes() {
    return Heap.num_allocated_bytes;
}

size_t _num_meta_data_bytes() {
    return Heap.num_meta_data_bytes;
}

size_t _size_meta_data() {
    return sizeof(MallocMetadata);
}