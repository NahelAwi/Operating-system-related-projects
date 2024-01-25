#include "sys/time.h"

#ifndef __REQUEST_H__

// enum helper
enum request_result { ERR501 = -501, ERR404 = -404, ERR403 = -403, DYNAMIC = 0, STATIC = 1 };

// definitions
typedef struct timeval timeval_t;
typedef struct statistics {
    // request statistics
    timeval_t arrival_time;
    timeval_t dispatch_interval;
    // thread statistics
    int thread_id;
    int requests_count;
    int static_requests_count;
    int dynamic_requests_count;

} statistics_t;

int requestHandle(int fd, statistics_t stats);

#endif
