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
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef unsigned char byte;

enum { KB = 1024, PAGE = 4 * KB, null = 0 };

#pragma pack(push, 1)
typedef struct node_s {
    struct node_s* next;
    size_t size;
} node_t;
#pragma pack(pop)

typedef struct arena_s {
    node_t  base;
    node_t* free;
    void*   that;
    node_t* allocated; /* list of regions allocated from "that" alloc */
    void* (*that_alloc)(void* that, size_t);
    void  (*that_free)(void* that, void*);
    size_t  parent_min_alloc_in_bytes;
    size_t  recycle_smaller_then_bytes;
    node_t**bins; /* [recycle_smaller_then_bytes] */;
} arena_t;

static void* arena_alloc(arena_t* arena, size_t bytes);
static void  arena_free(arena_t* arena, void* address);


/* The following implementation of malloc() and free() is from the book
 "The C Programming Language" by Brian W. Kernighan and Dennis M. Ritchie. */

heap_t* heap_create_ex(void* that, void* (alloc)(void* that, size_t),
                        void (free)(void* that, void*),
                        size_t parent_min_alloc_in_bytes,
                        size_t recycle_smaller_then_bytes) {
    assert(parent_min_alloc_in_bytes >= 4 * 1024);
    arena_t* arena = (arena_t*)alloc(that, sizeof(arena_t));
    memset(arena, 0, sizeof(arena_t));
    if (arena != null) {
        arena->free = &arena->base;
        arena->base.next = &arena->base;
        arena->that = that;
        arena->that_alloc = alloc;
        arena->that_free  = free;
        arena->parent_min_alloc_in_bytes = parent_min_alloc_in_bytes;
        arena->recycle_smaller_then_bytes = recycle_smaller_then_bytes;
        arena->bins = null;
        if (recycle_smaller_then_bytes > 1) {
            arena->bins = (node_t**)alloc(that, recycle_smaller_then_bytes * sizeof(node_t*));
            if (arena->bins != null) {
                memset(arena->bins, 0, recycle_smaller_then_bytes * sizeof(node_t*));
            } else {
                heap_destroy((heap_t*)arena);
                arena = null;
            }
        }
    }
    return (heap_t*)arena;
}

static void* runtime_alloc(void* that, size_t bytes) { return malloc(bytes); }
static void  runtime_free(void* that, void* a) { free(a); }

heap_t* heap_create() {
    return heap_create_ex(null, runtime_alloc, runtime_free, 1024 * 1024, 1024);
}

bool heap_destroy(heap_t* heap) {
    heap_compact(heap);
    arena_t* arena = (arena_t*)heap;
    bool leaks = false;
#ifdef DEBUG
    {
        node_t* f = arena->free;
        do {
            if (f->size > 0) {
                node_t* ma = arena->allocated;
                while (ma != null && ma != f - 1) { ma = ma->next; }
                if (ma == null || ma->size != f->size + 2) {
                    printf("WARNING: memory leaks in the arena: %p[%lld]\n", f, (uint64_t)(f->size * sizeof(node_t)));
                    leaks = true;
                }
            }
            f = f->next;
        } while (f != arena->free);
    }
#endif
    while (arena->allocated != null) {
        node_t* next = arena->allocated->next;
//      printf("that_free(%lld, %p)\n", (uint64_t)(arena->allocated->size * sizeof(node_t)), arena->allocated);
        arena->that_free(arena->that, arena->allocated);
        arena->allocated = next;
    }
    arena->that_free(arena->that, arena->bins);
    arena->that_free(arena->that, arena);
    return !leaks;
}

static void arena_free(arena_t* arena, void* ap);

static inline size_t number_of_units(size_t bytes) {
    return (bytes + sizeof(node_t) - 1) / sizeof(node_t);
}

static node_t* arena_alloc_pages(arena_t* arena, size_t nu) {
    if (nu < arena->parent_min_alloc_in_bytes / sizeof(node_t)) {
        nu = arena->parent_min_alloc_in_bytes / sizeof(node_t);
    }
    node_t *pg = arena->that_alloc(arena->that, (nu + 1) * sizeof(node_t));
//  printf("that_alloc(%d)=%p\n", (int)(nu * sizeof(node_t)), pg);
    if (pg == null) {
        return null;
    }
    pg->next = arena->allocated;
    arena->allocated = pg;
    pg->size = nu + 1;
    node_t* up = (node_t*)pg + 1;
    up->size = nu - 1;
    arena_free(arena, up + 1);
    return arena->free;
}

/* The following implementation of alloc() and free() is from the book
   "The C Programming Language" by Brian W. Kernighan and Dennis M. Ritchie. */

static void* arena_alloc(arena_t* arena, size_t bytes) {
    // "nu" number of units is "bytes rounded and measured in number of times of sizeof(header_r)"
    size_t nu = number_of_units(bytes) + 1;
    node_t* prev = arena->free;
    node_t* p = prev->next;
    while  (p->size < nu) {
        if (p == arena->free) {
            p = arena_alloc_pages(arena, nu);
            if (p == null) {
                return null;
            }
        }
        prev = p;
        p = p->next;
    }
    if (p->size == nu) {
        prev->next = p->next;
    } else {
        p->size -= nu;
        p += p->size;
        p->size = nu;
    }
    arena->free = prev;
    p->next = null;
    return p + 1;
}

static void arena_free(arena_t* arena, void* ap) {
    node_t* a = (node_t*)ap - 1;
    node_t* f = arena->free;
    node_t* n = f->next;
    for (;;) {
        if ((f < a && a < n) || (n <= f && (f < a || a < n))) { break; } /* && || are faster here than "|", "&" */
        f = n;
        n = f->next;
    }
    if (a + a->size == n) {
        a->size += n->size;
        a->next  = n->next;
    } else {
        a->next = n;
    }
    if (f + f->size == a) {
        f->size += a->size;
        f->next  = a->next;
    } else {
        f->next = a;
    }
    arena->free = f;
}

void* heap_allocz(heap_t* heap, size_t bytes) {
    void* a = heap_alloc(heap, bytes);
    if (a != 0) { memset(a, 0, bytes); }
    return a;
}

static inline size_t _heap_alloc_usable_size(void* a) {
    node_t* n = (node_t*)a - 1;
    assert(n->size > 1);
    return (n->size - 1) * sizeof(node_t);
}

size_t heap_alloc_usable_size(void* a) {
    return _heap_alloc_usable_size(a);
}

void heap_compact(heap_t* heap) {
    arena_t* arena = (arena_t*)heap;
    for (int i = 0; i < arena->recycle_smaller_then_bytes; i++) {
        for (;;) {
            node_t* n = arena->bins[i];
            if (n == null) {
                break;
            }
            arena->bins[i] = n->next;
            n->next = null;
            arena_free(arena, (byte*)n + sizeof(node_t));
        }
    }
}

void* heap_alloc(heap_t* heap, size_t size) {
    arena_t* arena = (arena_t*)heap;
    size_t bytes = number_of_units(size) * sizeof(node_t);
    if (bytes >= arena->recycle_smaller_then_bytes || arena->bins[bytes] == null) {
        void* a = arena_alloc(arena, size);
        if (a == null) {
            heap_compact(heap);
            a = arena_alloc(arena, size);
        }
        assert(a == null || bytes == _heap_alloc_usable_size(a));
        return a;
    } else {
        node_t* n = arena->bins[bytes];
        arena->bins[bytes] = n->next;
        n->next = null;
        assert(n->size - 1 == bytes / sizeof(node_t));
        return (byte*)n + sizeof(node_t);
    }
}

void heap_free(heap_t* heap, void* a) {
    if (a != null) {
        arena_t* arena = (arena_t*)heap;
        size_t bytes = _heap_alloc_usable_size(a);
        if (bytes >= arena->recycle_smaller_then_bytes) {
            arena_free(arena, a);
        } else {
            node_t* n = (node_t*)a - 1;
            assert(n->size - 1 == bytes / sizeof(node_t) && n->next == null);
            n->next = arena->bins[bytes];
            arena->bins[bytes] = n;
        }
    }
}
