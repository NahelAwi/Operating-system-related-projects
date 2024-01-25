#include <assert.h>
#include <math.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define SIZE_LIMIT 1e8 // pow(10, 8)
#define SPLIT_THRESHOLD 128
#define MMAP_THRESHOLD (128 * 1024)

// point of interest (checking with tests): should size be the allocation size
// or the overall size (including the metadata), givin that we assume the user asked
// for "size" bytes and we would be giving him less if so.

// Answer: according to the tests, size is what the user wants and does not include meta data

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

static const int32_t MAIN_COOKIE = rand();

struct MallocMetadata {
    int32_t cookie;
    size_t size;
    bool is_free;
    MallocMetadata *next;
    MallocMetadata *prev;
};

struct MemoryList {
    MallocMetadata *head = NULL;       // head of the sorted list (smallest block)
    MallocMetadata *firsthead = NULL;  // first block in the heap
    MallocMetadata *wilderness = NULL; // final block in the heap

    MallocMetadata *mmhead = NULL;

    size_t num_free_blocks = 0;
    size_t num_free_bytes = 0;

    size_t num_allocated_blocks = 0;
    size_t num_allocated_bytes = 0;

    size_t num_meta_data_bytes = 0;
};

static MemoryList Heap; // although named heap it consists of all memory allocated

// Validate that the cookie did not change. exit if does.
// Must be use before every metadata access.
// From piazza: You are allowed to check once before the first access to the metadata (assume when your code runs,
//              there are no buffer overflows). You should check for corruption for each metadata block you access.
void validateCookie(MallocMetadata *alloc) {
    if (alloc != NULL && alloc->cookie != MAIN_COOKIE) {
        exit(0xdeadbeef);
    }
}

// New allocation adjustment
void newAllocAdjustment(size_t size) {
    Heap.num_allocated_blocks += 1;
    Heap.num_allocated_bytes += size;
    Heap.num_meta_data_bytes += sizeof(MallocMetadata);
}

// start the memory list
void *firstAllocation(size_t size) {
    void *meta_data_address = sbrk(size + sizeof(MallocMetadata)); 
    if (meta_data_address == (void *)-1)                           
        return NULL;
    void *address = (void *)((MallocMetadata *)meta_data_address + 1); // pointer fix FINAL
    Heap.head = (MallocMetadata *)meta_data_address;
    Heap.head->cookie = MAIN_COOKIE;
    Heap.head->is_free = false;
    Heap.head->next = NULL;
    Heap.head->prev = NULL;
    Heap.head->size = size;
    Heap.wilderness = Heap.head;
    Heap.firsthead = Heap.head;
    newAllocAdjustment(size);
    return address;
}

// List Insert
void HeapListInsert(MallocMetadata *new_alloc) {
    MallocMetadata *ptr = Heap.head;
    if (ptr != NULL) {
        validateCookie(ptr);
        while (ptr->next != NULL) {
            validateCookie(new_alloc);
            if (new_alloc->size >= ptr->size && new_alloc->size < ptr->next->size) { 
                if (new_alloc->size == ptr->size) {
                    if (new_alloc > ptr) {
                        MallocMetadata *next = ptr->next;
                        new_alloc->next = next;
                        new_alloc->prev = ptr;
                        ptr->next = new_alloc;
                        next->prev = new_alloc;
                    } else {
                        if (ptr == Heap.head) {
                            Heap.head = new_alloc;
                            new_alloc->next = ptr;
                            new_alloc->prev = NULL;
                            ptr->prev = new_alloc;
                        } else {
                            MallocMetadata *prev = ptr->prev;
                            new_alloc->next = ptr;
                            new_alloc->prev = prev;
                            ptr->prev = new_alloc;
                            prev->next = new_alloc;
                        }
                    }
                } else {
                    MallocMetadata *next = ptr->next;
                    new_alloc->next = next;
                    new_alloc->prev = ptr;
                    ptr->next = new_alloc;
                    next->prev = new_alloc;
                }
                return;
            }
            ptr = ptr->next;
            validateCookie(ptr);
        }
        if (ptr->size <= new_alloc->size) {
            if (ptr->size == new_alloc->size) {
                if (new_alloc > ptr) {
                    ptr->next = new_alloc;
                    new_alloc->prev = ptr;
                    new_alloc->next = NULL;
                } else {
                    if (ptr == Heap.head) {
                        Heap.head = new_alloc;
                        new_alloc->next = ptr;
                        new_alloc->prev = NULL;
                        ptr->prev = new_alloc;
                    } else {
                        MallocMetadata *prev = ptr->prev;
                        new_alloc->next = ptr;
                        new_alloc->prev = prev;
                        ptr->prev = new_alloc;
                        prev->next = new_alloc;
                    }
                }
            } else {
                ptr->next = new_alloc;
                new_alloc->prev = ptr;
                new_alloc->next = NULL;
            }
        } else {
            //assert(ptr == Heap.head); ////////////
            ptr->prev = new_alloc;
            new_alloc->next = ptr;
            new_alloc->prev = NULL;
            Heap.head = new_alloc;
        }
    } else {
        Heap.head = new_alloc;
        new_alloc->next = NULL;
        new_alloc->prev = NULL;
    }
}

// List Remove
void HeapListRemove(MallocMetadata *block) {
    validateCookie(block);
    validateCookie(Heap.head);
    if (block == Heap.head) {
        Heap.head = block->next;
        if (Heap.head != NULL)
            Heap.head->prev = NULL;

    } else {
        if (block->next != NULL) {
            block->next->prev = block->prev;
            validateCookie(block->next);
        }
        validateCookie(block->prev);
        block->prev->next = block->next;
    } // THe only case in which block->prev is Null is when its the head
}

// splits a block and adjusts parameters in case it is possible
void split(MallocMetadata *request, size_t new_size) {
    validateCookie(request);
    if ((int)request->size - (int)new_size - (int)sizeof(MallocMetadata) >= SPLIT_THRESHOLD) {
        void *new_block = (void *)(request);
        new_block = (void *)((char *)new_block + new_size + sizeof(MallocMetadata));
        MallocMetadata *new_block_data = (MallocMetadata *)new_block;
        if (request == Heap.wilderness) {
            Heap.wilderness = new_block_data;
        }

        new_block_data->cookie = MAIN_COOKIE;
        new_block_data->is_free = true;
        new_block_data->size = request->size - new_size - sizeof(MallocMetadata);

        if (request->is_free == false) {
            Heap.num_free_blocks += 1;
            Heap.num_free_bytes += (new_block_data->size);
        } else {
            Heap.num_free_bytes -= (new_size + sizeof(MallocMetadata));
        }

        request->is_free = false;
        request->size = new_size;

        Heap.num_allocated_blocks += 1;
        Heap.num_allocated_bytes -= sizeof(MallocMetadata);
        Heap.num_meta_data_bytes += sizeof(MallocMetadata);

        HeapListRemove(request);
        HeapListInsert(request);
        HeapListInsert(new_block_data);

    } else {
        if (request->is_free == true) {
            request->is_free = false;
            Heap.num_free_blocks -= 1;
            Heap.num_free_bytes -= request->size;
        }
    }
}

// merges adjacent free blocks
// recursive merge
// not what is asked for but i think it still works, here i go over all the memory and combine adjacent free spaces
void smerge(void *limit, void *ptr) {
    if (ptr == limit) {
        return;
    }

    MallocMetadata *ptr_md = (MallocMetadata *)ptr;
    validateCookie(ptr_md);

    void *next = ptr;
    next = (void *)((char *)next + ptr_md->size + sizeof(MallocMetadata));
    if (next == limit) {
        return;
    }
    MallocMetadata *next_md = (MallocMetadata *)next;
    validateCookie(next_md);

    if (ptr_md->is_free == false || next_md->is_free == false) {
        smerge(limit, next);
    } else {

        ptr_md->size += (next_md->size + sizeof(MallocMetadata));

        Heap.num_free_blocks -= 1;
        Heap.num_free_bytes += sizeof(MallocMetadata);
        Heap.num_allocated_blocks -= 1;
        Heap.num_allocated_bytes += sizeof(MallocMetadata);
        Heap.num_meta_data_bytes -= sizeof(MallocMetadata);

        HeapListRemove(ptr_md);
        HeapListRemove(next_md);
        HeapListInsert(ptr_md);

        if (next_md == Heap.wilderness) {
            Heap.wilderness = ptr_md;
            return;
        } else {
            smerge(limit, ptr);
        }
    }
}

void merge() {
    void *limit = sbrk(0);
    if (Heap.firsthead == NULL || limit == (void *)-1)
        return;
    void *ptr = (void *)Heap.firsthead;
    smerge(limit, ptr);
}

void *smalloc(size_t size) {
    if (size == 0 || size > SIZE_LIMIT)
        return NULL;

    // MMAP implementation
    if (size >= MMAP_THRESHOLD) {
        void *meta_data_address = mmap(NULL, size + (size_t)sizeof(MallocMetadata), PROT_READ | PROT_WRITE,
                                       MAP_ANONYMOUS | MAP_SHARED, -1, 0);
        if (meta_data_address == MAP_FAILED)
            return NULL;
        void *address = (void *)((MallocMetadata *)meta_data_address + 1);
        MallocMetadata *new_alloc = (MallocMetadata *)meta_data_address;
        new_alloc->cookie = MAIN_COOKIE;
        new_alloc->is_free = false;
        new_alloc->size = size;
        new_alloc->next = NULL;
        new_alloc->prev = NULL;
        newAllocAdjustment(size);
        if (Heap.mmhead == NULL) {
            Heap.mmhead = new_alloc;
            validateCookie(new_alloc);
        } else {
            MallocMetadata *ptr = Heap.mmhead;
            while (ptr->next != NULL){
                validateCookie(ptr);
                ptr = ptr->next;
            }
            ptr->next = new_alloc;
            new_alloc->prev = ptr;
        }
        return address;
    }

    // if its our first allocation
    if (Heap.head == NULL) {
        return firstAllocation(size);
    }

    // search for a possible memory space
    MallocMetadata *ptr = Heap.head;
    do {
        validateCookie(ptr);
        if (ptr->is_free && ptr->size >= size) {
            split(ptr, size);
            void *address = (void *)(ptr + 1);
            return address;
        }
        ptr = ptr->next;
    } while (ptr != NULL);

    validateCookie(Heap.wilderness);
    if (Heap.wilderness->is_free) {
        // calculate needed size
        validateCookie(Heap.wilderness);
        size_t needed = size - Heap.wilderness->size;
        size_t used = Heap.wilderness->size;
        void *bonus = sbrk(needed);
        if (bonus == (void *)-1)
            return NULL;
        void *address = (void *)(Heap.wilderness + 1);

        HeapListRemove(Heap.wilderness);
        Heap.wilderness->is_free = false;
        Heap.wilderness->size += needed;
        HeapListInsert(Heap.wilderness);

        Heap.num_free_blocks -= 1;
        Heap.num_free_bytes -= used;
        Heap.num_allocated_bytes += needed;

        return address;
    }

    // if there is no already allocated space and wilderness isnt free, allocate new
    void *meta_data_address = sbrk(size + sizeof(MallocMetadata));
    if (meta_data_address == (void *)-1)
        return NULL;
    void *address = (void *)((MallocMetadata *)meta_data_address + 1);
    MallocMetadata *new_alloc = (MallocMetadata *)meta_data_address;
    Heap.wilderness = new_alloc;
    new_alloc->cookie = MAIN_COOKIE;
    new_alloc->is_free = false;
    new_alloc->size = size;
    newAllocAdjustment(size);

    // LIST INSERT
    HeapListInsert(new_alloc);

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
    validateCookie(P_meta_data);
    if (P_meta_data->size >= MMAP_THRESHOLD) {

        if (P_meta_data == Heap.mmhead) {
            Heap.mmhead = P_meta_data->next;
            if (P_meta_data->next != NULL) {
                validateCookie(P_meta_data->next);
                P_meta_data->next->prev = NULL;
            }
        } else {
            //assert(P_meta_data->prev != NULL);
            P_meta_data->prev->next = P_meta_data->next;
            validateCookie(P_meta_data->prev);
            if (P_meta_data->next != NULL) {
                P_meta_data->next->prev = P_meta_data->prev;
            }
        }

        Heap.num_allocated_blocks -= 1;
        Heap.num_allocated_bytes -= P_meta_data->size;
        Heap.num_meta_data_bytes -= sizeof(MallocMetadata);
        munmap((void *)P_meta_data, P_meta_data->size + sizeof(MallocMetadata));
    } else {
        if (P_meta_data->is_free == false) {
            P_meta_data->is_free = true;
            Heap.num_free_blocks += 1;
            Heap.num_free_bytes += P_meta_data->size;
            merge();
        }
    }
    return;
}

// mmap realloc case
void *mmapsrealloc(void *oldp, size_t size) {
    MallocMetadata *oldp_meta_data = (MallocMetadata *)oldp - 1;
    validateCookie(oldp_meta_data);
    if (oldp_meta_data->size == size)
        return oldp;
    void *address = smalloc(size);
    if (address == NULL)
        return NULL;
    if (oldp_meta_data->size < size) {
        memmove(address, oldp, oldp_meta_data->size);
    } else {
        memmove(address, oldp, size);
    }

    sfree(oldp);
    return address;
}

void *srealloc(void *oldp, size_t size) {
    if (oldp == NULL)
        return smalloc(size);
    if (size == 0 || size > SIZE_LIMIT)
        return NULL;

    MallocMetadata *oldp_meta_data = (MallocMetadata *)oldp - 1;
    validateCookie(oldp_meta_data);

    // in case of a mmap alloc
    if (oldp_meta_data->size >= MMAP_THRESHOLD) {
        return mmapsrealloc(oldp, size);
    }

    // a. Try to reuse the current block without any merging.
    if (oldp_meta_data->size >= size) {
        split(oldp_meta_data, size);
        return oldp;
    }

    // get the previous and next in address order
    MallocMetadata *next;
    MallocMetadata *prev;
    bool is_first = false;
    bool is_wild = false;
    if (oldp_meta_data == Heap.wilderness) {
        is_wild = true;
        next = NULL;
    } else {
        next = (MallocMetadata *)((char *)oldp_meta_data + oldp_meta_data->size + sizeof(MallocMetadata));
        validateCookie(next);
    }
    if (oldp_meta_data == Heap.firsthead) {
        is_first = true;
        prev = NULL;
    } else {
        prev = Heap.firsthead;
        while ((MallocMetadata *)((char *)prev + prev->size + sizeof(MallocMetadata)) != oldp_meta_data) {
            prev = (MallocMetadata *)((char *)prev + prev->size + sizeof(MallocMetadata));
        }
        validateCookie(prev);
    }

    // b. Try to merge with the adjacent block with the lower address.
    //      If the block is the wilderness chunk, enlarge it after merging if needed.
    if (!is_first && prev->is_free) {
        if (oldp_meta_data->size + prev->size + sizeof(MallocMetadata) >= size) {
            oldp_meta_data->is_free = true;
            Heap.num_free_blocks += 1;
            Heap.num_free_bytes += oldp_meta_data->size;
            bool nextState;
            if (next != NULL) {
                nextState = next->is_free;
                next->is_free = false;
            }
            merge();
            if (next != NULL) {
                next->is_free = nextState;
            }
            split(prev, size);
            merge();
            void *address = (void *)(prev + 1);
            memmove(address, oldp, oldp_meta_data->size);
            return address;
        } else {
            if (is_wild) {
                oldp_meta_data->is_free = true;
                Heap.num_free_blocks += 1;
                Heap.num_free_bytes += oldp_meta_data->size;
                merge();
                split(prev, size);
                size_t needed = size - Heap.wilderness->size;
                void *bonus = sbrk(needed);
                if (bonus == (void *)-1)
                    return NULL;
                void *address = (void *)(prev + 1);
                memmove(address, oldp, oldp_meta_data->size);
                HeapListRemove(Heap.wilderness);
                Heap.wilderness->size += needed;
                HeapListInsert(Heap.wilderness);

                Heap.num_allocated_bytes += needed;

                return address;
            }
        }
    }

    // c. If the block is the wilderness chunk, enlarge it.
    if (is_wild) {
        // calculate needed size
        size_t needed = size - Heap.wilderness->size;
        void *bonus = sbrk(needed);
        if (bonus == (void *)-1)
            return NULL;
        void *address = (void *)(Heap.wilderness + 1);

        HeapListRemove(Heap.wilderness);
        //assert(Heap.wilderness->is_free == false);
        Heap.wilderness->size += needed;
        HeapListInsert(Heap.wilderness);

        Heap.num_allocated_bytes += needed;

        return address;
    }

    // d. Try to merge with the adjacent block with the higher address.
    if (next->is_free) {
        //assert(next != NULL);
        if (oldp_meta_data->size + next->size + sizeof(MallocMetadata) >= size) {
            oldp_meta_data->is_free = true;
            Heap.num_free_blocks += 1;
            Heap.num_free_bytes += oldp_meta_data->size;
            bool prevState;
            if (prev != NULL) {
                prevState = prev->is_free;
                prev->is_free = false;
            }
            merge();
            if (prev != NULL) {
                prev->is_free = prevState;
            }
            split(oldp_meta_data, size);
            void *address = (void *)(oldp_meta_data + 1);
            return address;
        }
    }

    // e. Try to merge all those three adjacent blocks together.
    if (next != NULL && prev != NULL && next->is_free && prev->is_free) {
        if (prev->size + oldp_meta_data->size + next->size + 2 * sizeof(MallocMetadata) >= size) {
            oldp_meta_data->is_free = true;
            Heap.num_free_blocks += 1;
            Heap.num_free_bytes += oldp_meta_data->size;
            merge();
            split(prev, size);
            //prev->is_free = false;
            //Heap.num_free_blocks -= 1;//IN case e didnt need split
            //Heap.num_free_bytes -= prev->size;
            void *address = (void *)(prev + 1);
            memmove(address, oldp, oldp_meta_data->size);
            return address;
        }
    }

    // f. If the wilderness chunk is the adjacent block with the higher address:
    //      i. Try to merge with the lower and upper blocks (such as in e), and enlarge the
    //          wilderness block as needed.
    //      ii. Try to merge only with higher address (the wilderness chunk), and enlarge it as
    //          needed.
    if (next != NULL && next == Heap.wilderness) {
        if (next->is_free) {
            if (prev != NULL && prev->is_free) {
                oldp_meta_data->is_free = true;
                Heap.num_free_blocks += 1;
                Heap.num_free_bytes += oldp_meta_data->size;
                merge();
                prev->is_free = false;
                Heap.num_free_blocks -= 1;
                Heap.num_free_bytes -= prev->size;
                void *address = (void *)(prev + 1);
                memmove(address, oldp, oldp_meta_data->size);

                //assert(prev == Heap.wilderness);
                // calculate needed size
                size_t needed = size - Heap.wilderness->size;
                void *bonus = sbrk(needed);
                if (bonus == (void *)-1) // TODO IF SBRK FAILS RETURN EVERYTHING TO BEFORE REALLOC
                    return NULL;

                HeapListRemove(Heap.wilderness);
                //assert(Heap.wilderness->is_free == false);
                Heap.wilderness->size += needed;
                HeapListInsert(Heap.wilderness);

                Heap.num_allocated_bytes += needed;

                return address;
            } else {
                oldp_meta_data->is_free = true;
                Heap.num_free_blocks += 1;
                Heap.num_free_bytes += oldp_meta_data->size;
                merge();
                oldp_meta_data->is_free = false;
                Heap.num_free_blocks -= 1;
                Heap.num_free_bytes -= oldp_meta_data->size;
                void *address = (void *)(oldp_meta_data + 1);

                //assert(oldp_meta_data == Heap.wilderness);

                size_t needed = size - Heap.wilderness->size;
                void *bonus = sbrk(needed);
                if (bonus == (void *)-1) // TODO IF SBRK FAILS RETURN EVERYTHING TO BEFORE REALLOC
                    return NULL;

                HeapListRemove(Heap.wilderness);
                //assert(Heap.wilderness->is_free == false);
                Heap.wilderness->size += needed;
                HeapListInsert(Heap.wilderness);

                Heap.num_allocated_bytes += needed;

                return address;
            }
        }
    }

    // g. Try to find a different block that’s large enough to contain the request (don’t forget
    // that you need to free the current block, therefore you should, if possible, merge it
    // with neighboring blocks before proceeding).
    // h. Allocate a new block with sbrk().
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
