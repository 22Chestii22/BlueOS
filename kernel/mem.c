#include "types.h"
#include "string.h"
#include "screen.h"

#define HEAP_START 0x1000000
#define HEAP_SIZE 0x2000000

typedef struct block_header
{
    uint32_t magic;
    uint32_t size;
    uint32_t free;
    struct block_header* next;
    struct block_header* prev;
} block_header_t;

#define BLOCK_MAGIC 0xB10CAB1E
#define BLOCK_ALIGN 8
#define HEADER_SIZE ((sizeof(block_header_t) + BLOCK_ALIGN - 1) & ~(BLOCK_ALIGN - 1))

static block_header_t* heap_start = NULL;

void mem_init(void)
{
    heap_start = (block_header_t*)HEAP_START;
    heap_start->magic = BLOCK_MAGIC;
    heap_start->size = HEAP_SIZE - HEADER_SIZE;
    heap_start->free = 1;
    heap_start->next = NULL;
    heap_start->prev = NULL;

    printf("[MEM] Heap initialized at 0x%x, size %d KB\n",
           HEAP_START, HEAP_SIZE / 1024);
}

static void split_block(block_header_t* block, uint32_t size)
{
    if (block->size <= size + HEADER_SIZE + BLOCK_ALIGN)
        return;

    block_header_t* new_block = (block_header_t*)((uint64_t)block + HEADER_SIZE + size);
    new_block->magic = BLOCK_MAGIC;
    new_block->size = block->size - size - HEADER_SIZE;
    new_block->free = 1;
    new_block->next = block->next;
    new_block->prev = block;

    block->size = size;
    block->next = new_block;

    if (new_block->next)
        new_block->next->prev = new_block;
}

static void merge_blocks(block_header_t* block)
{
    if (block->next && block->next->free)
    {
        block->size += HEADER_SIZE + block->next->size;
        block->next = block->next->next;
        if (block->next)
            block->next->prev = block;
    }
}

void* malloc(uint32_t size)
{
    if (size == 0) return NULL;

    size = (size + BLOCK_ALIGN - 1) & ~(BLOCK_ALIGN - 1);

    block_header_t* current = heap_start;
    while (current)
    {
        if (current->free && current->size >= size)
        {
            split_block(current, size);
            current->free = 0;
            return (void*)((uint64_t)current + HEADER_SIZE);
        }
        current = current->next;
    }

    return NULL;
}

void free(void* ptr)
{
    if (!ptr) return;

    block_header_t* block = (block_header_t*)((uint64_t)ptr - HEADER_SIZE);
    if (block->magic != BLOCK_MAGIC) return;

    block->free = 1;

    if (block->prev && block->prev->free)
    {
        block = block->prev;
        merge_blocks(block);
    }

    merge_blocks(block);
}

void* calloc(uint32_t num, uint32_t size)
{
    uint32_t total = num * size;
    void* ptr = malloc(total);
    if (ptr)
        memset(ptr, 0, total);
    return ptr;
}

uint32_t mem_get_used(void)
{
    uint32_t total = 0;
    block_header_t* current = heap_start;
    while (current)
    {
        if (!current->free)
            total += current->size;
        current = current->next;
    }
    return total;
}

uint32_t mem_get_free(void)
{
    uint32_t total = 0;
    block_header_t* current = heap_start;
    while (current)
    {
        if (current->free)
            total += current->size;
        current = current->next;
    }
    return total;
}

void* realloc(void* ptr, uint32_t new_size)
{
    if (!ptr) return malloc(new_size);
    if (new_size == 0) { free(ptr); return NULL; }

    block_header_t* block = (block_header_t*)((uint64_t)ptr - HEADER_SIZE);
    uint32_t old_size = block->size;

    void* new_ptr = malloc(new_size);
    if (!new_ptr) return NULL;

    uint32_t copy_size = old_size < new_size ? old_size : new_size;
    memcpy(new_ptr, ptr, copy_size);
    free(ptr);
    return new_ptr;
}
