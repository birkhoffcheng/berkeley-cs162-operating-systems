#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <syscall.h>
#include <limits.h>


struct mem_block {
	size_t size;
	bool free;
	struct mem_block *next;
	struct mem_block *prev;
};

static struct mem_block *head = NULL;
static struct mem_block *tail = NULL;

static void remove_entry(struct mem_block *mem) {
	if (mem == NULL)
		return;
	if (mem->prev)
		mem->prev->next = mem->next;
	else
		head = mem->next;
	if (mem->next)
		mem->next->prev = mem->prev;
	else
		tail = mem->prev;
}

void *malloc(size_t size) {
	if (size == 0)
		return NULL;
	if (head == NULL)
		goto alloc;
	struct mem_block *current;
	for (current = head; current != NULL; current = current->next) {
		if (current->free && current->size >= size) {
			if (current->size - size > sizeof(struct mem_block)) {
				struct mem_block *new = (char *)(current + 1) + size;
				new->free = true;
				new->size = current->size - size - sizeof(struct mem_block);
				new->next = current->next;
				new->prev = current;
				if (current->next)
					current->next->prev = new;
				else
					tail = new;
				current->next = new;
				current->size = size;
				return current + 1;
			}
			else {
				current->free = false;
				return current + 1;
			}
		}
	}
alloc:
	current = sbrk(sizeof(struct mem_block) + size);
	if (current == (void *)-1)
		return NULL;
	current->size = size;
	current->free = false;
	current->next = NULL;
	current->prev = tail;
	tail = current;
	if (head == NULL)
		head = current;
	return current + 1;
}

void free(void* ptr) {
	if (ptr == NULL)
		return;
	struct mem_block *mem = (struct mem_block *)ptr - 1;
	mem->free = true;
	if (mem->prev && mem->prev->free) {
		mem->prev->size += sizeof(struct mem_block) + mem->size;
		remove_entry(mem);
		mem = mem->prev;
	}
	if (mem->next && mem->next->free) {
		mem->size += sizeof(struct mem_block) + mem->next->size;
		remove_entry(mem->next);
	}
}

void *calloc(size_t nmemb, size_t size) {
	if (nmemb > ULONG_MAX / size)
		return NULL;
	size_t alloc_size = nmemb * size;
	void *mem = malloc(alloc_size);
	if (mem == NULL)
		return mem;
	return memset(mem, 0, alloc_size);
}

void *realloc(void* ptr, size_t size) {
	void *new = NULL;
	if (ptr == NULL)
		return malloc(size);
	else if (size == 0)
		goto free_and_return;
	struct mem_block *mem = (struct mem_block *)ptr - 1;
	if (size <= mem->size)
		return ptr;
	else if (mem == tail && sbrk(size - mem->size) != (void *)-1) {
		mem->size = size;
		return ptr;
	}
	else if (mem->next && mem->next->free && mem->size + mem->next->size + sizeof(struct mem_block) >= size) {
		mem->size += mem->next->size + sizeof(struct mem_block);
		remove_entry(mem->next);
		return ptr;
	}
	if ((new = malloc(size)) == NULL)
		goto free_and_return;
	memcpy(new, ptr, (size < mem->size) ? size : mem->size);
free_and_return:
	free(ptr);
	return new;
}
