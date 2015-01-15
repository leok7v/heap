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
#ifndef __HEAP__
#define __HEAP__
#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>

typedef void* heap_t;

heap_t* heap_create(); /* parent_min_alloc_in_bytes=1MB recycle_smaller_than_bytes=1024 */
void*   heap_alloc(heap_t* heap, size_t bytes);
size_t  heap_alloc_usable_size(void* a); /* only valid for heap_alloc()-ed memory */
void    heap_free(heap_t* heap, void* a);
bool    heap_destroy(heap_t* heap);      /* returns false if memory leaks were detected */

heap_t* heap_create_ex(void* that, void* (alloc)(void* that, size_t),
                       void (free)(void* that, void*),
                       size_t parent_min_alloc_in_bytes,
                       size_t recycle_smaller_than_bytes);

void heap_compact(heap_t* heap); /* mainly exposed for testing */
void heap_test(int verbose);

#endif
