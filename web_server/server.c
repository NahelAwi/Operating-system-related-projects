#include "pthread.h"
#include "request.h"
#include "segel.h"
#include "stdbool.h"
#include <math.h>
#include <sys/time.h>

//
// server.c: A very, very simple web server
//
// To run:
//  ./server <portnum (above 2000)>
//
// Repeatedly handles HTTP requests sent to this port number.
// Most of the work is done within routines written in request.c
//

#define CMD_ARGS_NUM 5

/*
 * Macro providing a “safe” way to invoke system calls
 */
#define DO_SYS(syscall)                                                                                                \
    do {                                                                                                               \
        /* safely invoke a system call */                                                                              \
        if ((syscall) == -1) {                                                                                         \
            perror(#syscall);                                                                                          \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)

// definitions
typedef struct shared_info shared_info_t;
typedef struct master_info master_info_t;
typedef void (*overload_alg_func)(shared_info_t *, master_info_t *master_info);
typedef struct timeval timeval_t;

typedef struct request {
    int connfd;
    timeval_t arrival_time;
    timeval_t dispatch_interval;
} request_t;

typedef struct requests_queue {
    request_t *items; // connfd array
    int head;         // next request to proccess
    int tail;         // next available place in the queue
    int size;
} requests_queue_t;

typedef struct requests_list {
    request_t *items; // connfd array
    int size;
} requests_list_t;

typedef struct shared_info {
    requests_queue_t *requests_queue;
    requests_list_t *requests_list;
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_not_full;
    pthread_cond_t queue_not_empty;
    int queue_capacity;

} shared_info_t; // shared info between master and workers

typedef struct thread_info {
    int thread_id;
    int requests_count;
    int static_requests_count;
    int dynamic_requests_count;
    shared_info_t *sh_info;
    pthread_t *self;
} thread_info_t;

typedef struct master_info {
    int listenfd;
    int clientlen;
    int connfd;
    struct sockaddr_in clientaddr;
    overload_alg_func policy;
    thread_info_t *workers_pool;
    timeval_t accept_time;
} master_info_t;

// logging and testing
#define _DEBUG_LOG_TEST // TODO Turn off for submission!!
#if defined(DEBUG_LOG) || defined(DEBUG_LOG_TEST)
    #define LOG(code) code;
#else
    #define LOG(code)
#endif
#if defined(DEBUG_TEST) || defined(DEBUG_LOG_TEST)
    #include <assert.h>
// test size match head and tail values
bool check_queue(int head, int tail, int size, int capacity) {

    if (head == tail && (size == 0 || size == capacity))
        return true;
    return (((tail + capacity) - head) % capacity) == size;
}
    #define TEST(code) code;
#else
    #define TEST(code)
#endif

void print_queue(shared_info_t *sh_info) {
    requests_queue_t *queue = sh_info->requests_queue;
    printf("Head: %d, Tail: %d, Size: %d, Capacity: %d\n", //
           queue->head, queue->tail, queue->size, sh_info->queue_capacity);
    printf("[");
    for (int i = 0; i < sh_info->queue_capacity; i++) {
        printf("%d", queue->items[i].connfd);
        if (i < sh_info->queue_capacity - 1) {
            printf(", ");
        }
    }
    printf("]\n");

    printf(" ");
    for (int i = 0; i < sh_info->queue_capacity; i++) {
        if (i == queue->head) {
            printf("h");
        } else if (i == queue->tail) {
            printf("t");
        } else {
            printf(" ");
        }
        int numLen = 0; // calculate the length of current item
        int temp = queue->items[i].connfd;
        while (temp) {
            numLen++;
            temp /= 10;
        }
        if (temp < 0)
            numLen++; // negative sign

        for (int i = 0; i < numLen + 1; i++) { // print spaces
            printf(" ");
        }
    }
    printf("\n");
}

void print_indexes(bool rand_indexes[], int n, shared_info_t *sh_info) {
    requests_queue_t *queue = sh_info->requests_queue;
    bool any = false;
    printf("picked indexes:\n[");
    for (int i = 0; i < n; ++i) {
        if (rand_indexes[i]) {
            printf("%d, ", i);
            any = true;
        }
    }
    if (any)
        printf("\b\b");
    printf("]\n");

    printf("picked values:\n[");
    for (int i = 0; i < n; ++i) {
        if (rand_indexes[i]) {
            printf("%d, ", queue->items[(queue->head + i) % sh_info->queue_capacity].connfd);
            any = true;
        }
    }
    if (any)
        printf("\b\b");
    printf("]\n");
}
// overload policies
void block(shared_info_t *sh_info, master_info_t *master_info) {
    LOG(printf("master waits\n"));
    pthread_cond_wait(&sh_info->queue_not_full, &sh_info->queue_mutex);
}

void drop_tail(shared_info_t *sh_info, master_info_t *master_info) {
    pthread_mutex_unlock(&sh_info->queue_mutex);
    LOG(printf("dropping tail\n"));
    Close(master_info->connfd);
    master_info->connfd =
        Accept(master_info->listenfd, (SA *)&master_info->clientaddr, (socklen_t *)&master_info->clientlen);
    DO_SYS(gettimeofday(&(master_info->accept_time), NULL));
    pthread_mutex_lock(&sh_info->queue_mutex);
}

void drop_head(shared_info_t *sh_info, master_info_t *master_info) {
    if (sh_info->requests_queue->size == 0)
        drop_tail(sh_info, master_info);
    else {
        LOG(printf("dropping head %d\n", sh_info->requests_queue->items[sh_info->requests_queue->head].connfd));
        Close(sh_info->requests_queue->items[sh_info->requests_queue->head].connfd);
        sh_info->requests_queue->head = (sh_info->requests_queue->head + 1) % sh_info->queue_capacity;
        sh_info->requests_queue->size -= 1;
    }
}

void drop_random(shared_info_t *sh_info, master_info_t *master_info) {
    if (sh_info->requests_queue->size == 0)
        drop_tail(sh_info, master_info);
    else {
        LOG(printf("dropping 50%% of the requests randomly\n"));
        // initialize arrays.
        // rand_indexes store the result random indexes keeping it ordered without the need to sort
        // indexes store all the indexes fom 0 to n-1 to randomly pick from
        int n = sh_info->requests_queue->size;
        bool rand_indexes[n];
        int indexes[n];
        for (int i = 0; i < n; ++i) {
            indexes[i] = i;
            rand_indexes[i] = false;
        }
        srand(time(NULL)); // Seed the random number generator
        LOG(printf("queue before:\n"); print_queue(sh_info););

        // pick n/2 indexes (that will be kept) randomly using pick and swap algorithm
        int limit = n / 2; // floor of the number to keep is ceil of the number to remove.
        for (int i = 0; i < limit; ++i) {
            int index = rand() % (n - i);        // pick index
            rand_indexes[indexes[index]] = true; // keep result sorted
            int temp = indexes[index];
            indexes[index] = indexes[n - i - 1]; // swap
            indexes[n - i - 1] = temp;
        }
        LOG(print_indexes(rand_indexes, n, sh_info));

        // drop unpicked items from the queue by moving all picked items to the head one bt one while keeping the order
        int head = sh_info->requests_queue->head;
        int tail = head;
        for (int i = 0; i < n; ++i) {
            if (rand_indexes[i] == true) {
                sh_info->requests_queue->items[tail] =
                    sh_info->requests_queue->items[(head + i) % sh_info->queue_capacity];
                // TEST(sh_info->requests_queue->items[(head + i) % sh_info->queue_capacity].connfd = -1;); // for
                // dropped test TODO
                tail = (tail + 1) % sh_info->queue_capacity;
            } else
                Close(sh_info->requests_queue->items[(head + i) % sh_info->queue_capacity].connfd);
        }
        sh_info->requests_queue->tail = tail;
        sh_info->requests_queue->size = limit;
        LOG(printf("queue after:\n"); print_queue(sh_info););
        TEST(assert(check_queue(head, tail, limit, sh_info->queue_capacity))); // test size match head and tail values
        // TEST(for (int i = 0; i != n - limit; i = (i + 1) % sh_info->queue_capacity) {
        //     // assert if dropped file not closed
        //     assert(close(sh_info->requests_queue->items[tail + i].connfd) == -1);
        // }); TODO
    }
}

// threads functions
void *workerFunction(void *info) {
    thread_info_t *thread_info = info;
    shared_info_t *sh_info = thread_info->sh_info;
    LOG(printf("worker %d started\n", thread_info->thread_id));
    while (1) {
        // check that the queue not empty
        pthread_mutex_lock(&sh_info->queue_mutex);
        while (sh_info->requests_queue->size == 0) {
            LOG(printf("worker %d wait\n", thread_info->thread_id));
            pthread_cond_wait(&sh_info->queue_not_empty, &sh_info->queue_mutex);
        }
        // take head out of queue
        request_t head_req = sh_info->requests_queue->items[sh_info->requests_queue->head];
        sh_info->requests_queue->head = (sh_info->requests_queue->head + 1) % sh_info->queue_capacity;
        sh_info->requests_queue->size -= 1;

        // stamp dispatch_interval
        timeval_t temp;
        DO_SYS(gettimeofday(&temp, NULL));
        head_req.dispatch_interval.tv_sec = temp.tv_sec - head_req.arrival_time.tv_sec;
        head_req.dispatch_interval.tv_usec = temp.tv_usec - head_req.arrival_time.tv_usec;
        if (head_req.dispatch_interval.tv_usec < 0) {
            head_req.dispatch_interval.tv_sec -= 1;
            head_req.dispatch_interval.tv_usec += 1000000;
        }

        // insert to list. thread always use the list index which equals to its id
        sh_info->requests_list->items[thread_info->thread_id] = head_req;
        sh_info->requests_list->size += 1;

        // create statistics stamp
        statistics_t stats_stamp;
        stats_stamp.thread_id = thread_info->thread_id;
        stats_stamp.requests_count = thread_info->requests_count;
        stats_stamp.static_requests_count = thread_info->static_requests_count;
        stats_stamp.dynamic_requests_count = thread_info->dynamic_requests_count;
        stats_stamp.arrival_time = head_req.arrival_time;
        stats_stamp.dispatch_interval = head_req.dispatch_interval;
        LOG(printf("worker %d piked request %d\n", thread_info->thread_id, head_req.connfd));
        pthread_cond_signal(&sh_info->queue_not_full);
        pthread_mutex_unlock(&sh_info->queue_mutex);

        // handle request
        TEST(usleep(100000));
        int result = requestHandle(head_req.connfd, stats_stamp);
        Close(head_req.connfd);

        pthread_mutex_lock(&sh_info->queue_mutex);
        // update statistics
        thread_info->requests_count += 1;
        if (result == STATIC)
            thread_info->static_requests_count += 1;
        if (result == DYNAMIC)
            thread_info->dynamic_requests_count += 1;
        // remove request from list
        sh_info->requests_list->items[thread_info->thread_id].connfd = -1;
        sh_info->requests_list->size -= 1;
        TEST(assert(check_queue(sh_info->requests_queue->head, sh_info->requests_queue->tail,
                                sh_info->requests_queue->size, sh_info->queue_capacity)));
        LOG(printf("worker %d handle request %d\n", thread_info->thread_id, head_req.connfd));
        pthread_cond_signal(&sh_info->queue_not_full);
        pthread_mutex_unlock(&sh_info->queue_mutex);
    }
}

void runMaster(int port, shared_info_t *sh_info, master_info_t *master_info) {
    master_info->clientlen = sizeof(master_info->clientaddr);
    while (1) {
        // wait for request
        master_info->connfd =
            Accept(master_info->listenfd, (SA *)&master_info->clientaddr, (socklen_t *)&master_info->clientlen);
        DO_SYS(gettimeofday(&(master_info->accept_time), NULL));

        // check that the queue not full
        pthread_mutex_lock(&sh_info->queue_mutex);
        while (sh_info->requests_queue->size + sh_info->requests_list->size == sh_info->queue_capacity) {
            master_info->policy(sh_info, master_info);
        }

        // Save the relevant info in a buffer and have one of the worker threads do the work.
        request_t *tail_req = &sh_info->requests_queue->items[sh_info->requests_queue->tail];
        tail_req->arrival_time = master_info->accept_time;
        tail_req->connfd = master_info->connfd;
        sh_info->requests_queue->tail = (sh_info->requests_queue->tail + 1) % sh_info->queue_capacity;
        sh_info->requests_queue->size += 1;
        TEST(assert(check_queue(sh_info->requests_queue->head, sh_info->requests_queue->tail,
                                sh_info->requests_queue->size, sh_info->queue_capacity)));
        LOG(printf("master add request connfd %d\n", master_info->connfd));
        pthread_cond_broadcast(&sh_info->queue_not_empty);
        pthread_mutex_unlock(&sh_info->queue_mutex);
    }
}

// parse the cmd arguments
void getargs(int *port, int *threads_num, int *queue_capacity, overload_alg_func *sched_alg, int argc, char *argv[]) {
    if (argc < CMD_ARGS_NUM) {
        fprintf(stderr, "Usage: %s <port> <threads> <queue_size> <schedalg>\n", argv[0]);
        exit(1);
    }
    *port = atoi(argv[1]);
    *threads_num = atoi(argv[2]);
    *queue_capacity = atoi(argv[3]);
    char *policy = argv[4];
    if (strcmp(policy, "block") == 0) {
        *sched_alg = block;
    } else if (strcmp(policy, "dt") == 0) {
        *sched_alg = drop_tail;
    } else if (strcmp(policy, "dh") == 0) {
        *sched_alg = drop_head;
    } else if (strcmp(policy, "random") == 0) {
        *sched_alg = drop_random;
    } else {
        printf("Error: invalid argument '%s'\n", policy); //
    }
}

// Method that creates a pool of worker threads
thread_info_t *createWorkersPool(int num_threads, shared_info_t *sh_info) {
    // Allocate memory for the thread info array
    thread_info_t *thread_pool = malloc(num_threads * sizeof(thread_info_t));

    // Create the worker threads
    for (int i = 0; i < num_threads; i++) {
        thread_pool[i].thread_id = i;
        thread_pool[i].requests_count = 0;
        thread_pool[i].static_requests_count = 0;
        thread_pool[i].dynamic_requests_count = 0;
        thread_pool[i].sh_info = sh_info;
        thread_pool[i].self = malloc(sizeof(pthread_t));
        DO_SYS(pthread_create(thread_pool[i].self, NULL, workerFunction, &thread_pool[i]));
    }

    return thread_pool;
}

requests_queue_t *createRequestQueue(int queue_capacity) {
    requests_queue_t *requests_queue = malloc(sizeof(requests_queue_t));
    requests_queue->items = malloc(queue_capacity * sizeof(request_t));
    requests_queue->size = 0;
    requests_queue->head = 0;
    requests_queue->tail = 0;
    return requests_queue;
}

requests_list_t *createRequestList(int queue_capacity) {
    requests_list_t *requests_list = malloc(sizeof(requests_list_t));
    requests_list->items = malloc(queue_capacity * sizeof(request_t));
    requests_list->size = 0;
    return requests_list;
}

int main(int argc, char *argv[]) {
    int port, threads_num, queue_capacity; // cmd args
    overload_alg_func policy;
    getargs(&port, &threads_num, &queue_capacity, &policy, argc, argv);

    // create request queue
    shared_info_t sh_info;
    sh_info.queue_capacity = queue_capacity;
    sh_info.requests_queue = createRequestQueue(queue_capacity); // request waiting for worker
    sh_info.requests_list = createRequestList(threads_num);      // requests currently handled by some worker

    // init locks and conditions
    pthread_mutex_init(&sh_info.queue_mutex, NULL);
    pthread_cond_init(&sh_info.queue_not_empty, NULL);
    pthread_cond_init(&sh_info.queue_not_full, NULL);

    // create workers pool and start processing requests
    master_info_t master_info;
    master_info.policy = policy;
    master_info.workers_pool = createWorkersPool(threads_num, &sh_info);
    master_info.listenfd = Open_listenfd(port);
    LOG(printf("master listening\n"));
    runMaster(port, &sh_info, &master_info);
}
