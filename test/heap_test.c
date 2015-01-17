/*  Copyleft (ɔ) 2013, Leo Kuznetsov
    No rights reserved.
 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
 * Redistributions of source code must retain the above copyright notice, this
 list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following disclaimer in the documentation
 and/or other materials provided with the distribution.
 * Neither the name of the {organization} nor the names of its
 contributors may be used to endorse or promote products derived from
 this software without specific prior written permission.
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include "heap.h"
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#ifdef __ANDROID__
#include <android/log.h>
#define printf(...) __android_log_print(ANDROID_LOG_INFO, "heap", __VA_ARGS__)
#endif


enum { NANOSECONDS_IN_SECOND = 1000000000, null = 0 };
#define countof(a) (sizeof(a) / sizeof((a)[0]))

/*
 MBP 2013 Intel Core i7 2.5GHz 1333MHz DDR3 clang-503.0.40 -Ofast

 32bit:

 535 nanoseconds for large alloc()/free() pair - heap
  24 nanoseconds for small alloc()/free() pair - heap *)
 118 nanoseconds for small alloc()/free() pair - runtime

 64bit:

 530 nanoseconds for large alloc()/free() pair - heap
  23 nanoseconds for small alloc()/free() pair - heap
 135 nanoseconds for small alloc()/free() pair - runtime

 Android 4.2.2 OMAP5432 ARM Cortex A15 1.5GHz DDR3L
1628 nanoseconds for large alloc()/free() pair - heap
  76 nanoseconds for small alloc()/free() pair - heap
 547 nanoseconds for small alloc()/free() pair - runtime

 Android NVIDIA Tegra 4 1.8GHz
1685 nanoseconds for large alloc()/free() pair - heap
  72 nanoseconds for small alloc()/free() pair - heap
 540 nanoseconds for small alloc()/free() pair - runtime

 *) Light travels approximately 29.98 centimeters 11.8 inches in 1 nanosecond.
    In 24 nanoseconds light just enough time to travel to a mirror 3.6 meters
    (11.8 feet) away across the room and back while heap_alloc()  and heap_free()
    has been executed.
*/

#ifdef __MACH__

#include <mach/mach.h>
#include <mach/mach_time.h>
#include <sys/time.h>

#define CLOCK_MONOTONIC 1

static inline uint64_t nanotime() {
    static mach_timebase_info_data_t    timebase_info;
    if ( timebase_info.denom == 0 ) {
        (void) mach_timebase_info(&timebase_info);
    }
    return mach_absolute_time() * timebase_info.numer / timebase_info.denom;
}

static inline int clock_gettime(int clk_id, struct timespec* t) {
    uint64_t ns = nanotime();
    t->tv_sec  = (int)(ns / NANOSECONDS_IN_SECOND);
    t->tv_nsec = (int)(ns % NANOSECONDS_IN_SECOND);
    return 0;
}

#endif

static inline uint64_t nanoseconds() {
    struct timespec ts = {0};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_nsec + (uint64_t)ts.tv_sec * NANOSECONDS_IN_SECOND;
}

static inline double random_in_range(size_t n) { // random in range [0..n-1]
    size_t r = (int)(((double)random() / RAND_MAX) * n - 1);
    assert(0 <= r && r < n);
    return r;
}

static void* empty_alloc(size_t bytes) {
    static intptr_t sequential = 1;
    return (void*)(sequential++);
}

static void empty_free(void* ap) {
    static intptr_t sequential = 0;
    sequential -= (intptr_t)ap;
}

static uint64_t _test(void* (alloc)(size_t), void (free)(void*), bool large) {
    uint64_t start = nanoseconds();
    enum { A = 1024, M = 1024 };
    void* array[A] = {0};
    #ifdef __ANDROID__
    const int N = 1000000 * (large ? 1 : 10);
    #else
    const int N = 10000000 * (large ? 1 : 10);
    #endif
    int allocs = 0;
    int frees = 0;
    for (int i = 0; i < N; i++) {
        int ix = random_in_range(countof(array));
        assert(0 <= ix && ix < countof(array));
        if (array[ix] == null) {
            array[ix] = alloc(random_in_range(M) + (large ? 1024 : 1));
            allocs++;
        } else {
            free(array[ix]);
            frees++;
            array[ix] = null;
        }
    }
    for (int i = 0; i < countof(array); i++) {
        if (array[i] != null) {
            free(array[i]);
            frees++;
            array[i] = null;
        }
    }
    uint64_t finish = nanoseconds();
    assert(allocs == frees);
    return (finish - start) / allocs;
}

static void test(bool verbose, bool large,
                 void* (alloc)(size_t),
                 void (free)(void*),
                 const char* title) {
    heap_t* heap = heap_create();
    uint64_t time0 = _test(empty_alloc, empty_free, large);
    heap_destroy(heap);
    heap = heap_create();
    uint64_t time1 = _test(alloc, free, large);
    heap_destroy(heap);
    if (verbose) {
        printf("% 4lld nanoseconds for %s alloc()/free() pair - %s\n",
               (time1 - time0), large ? "large" : "small", title);
    }
}

static heap_t test_heap;

static void* test_alloc(size_t bytes) {
    return heap_alloc(test_heap, bytes);
}

static void test_free(void* a) {
    heap_free(test_heap, a);
}

void heap_test(int verbose) {
    test_heap = heap_create();
    test(verbose, true, test_alloc, test_free, "heap");
    heap_destroy(test_heap);
    test_heap = heap_create();
    test(verbose, false, test_alloc, test_free, "heap");
    heap_destroy(test_heap);
    test(verbose, false, malloc, free, "runtime");
}

