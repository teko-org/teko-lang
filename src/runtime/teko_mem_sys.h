#ifndef TEKO_MEM_SYS_H
#define TEKO_MEM_SYS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Abstract, clean signatures that shield the Arenas from OS differences
void* teko_sys_allocate_pages(size_t size);
bool  teko_sys_free_pages(void* address, size_t size);

#endif // TEKO_MEM_SYS_H
