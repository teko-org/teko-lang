#include "teko_mem_sys.h"

// ---------------------------------------------------------------------------
// BRANCH 1: MICROSOFT WINDOWS ECOSYSTEM (Win32 VirtualAlloc API)
// ---------------------------------------------------------------------------
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>

void* teko_sys_allocate_pages(size_t size) {
    if (size == 0) return NULL;
    // MEM_COMMIT | MEM_RESERVE physically allocates clean (zeroed) pages in virtual RAM
    // PAGE_READWRITE sets the basic protection required by our data Arenas
    return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

bool teko_sys_free_pages(void* address, size_t size) {
    if (!address) return false;
    // MEM_RELEASE returns the pages back to the Windows memory manager instantly
    return VirtualFree(address, 0, MEM_RELEASE) != 0;
}

// ---------------------------------------------------------------------------
// BRANCH 2: UNIX/POSIX ECOSYSTEM (Linux, macOS Darwin, FreeBSD via mmap)
// ---------------------------------------------------------------------------
#else
#include <sys/mman.h>

// Fallback in case MAP_ANONYMOUS is not nominally mapped on some old BSD variant
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS 0x20
#endif

void* teko_sys_allocate_pages(size_t size) {
    if (size == 0) return NULL;

    // PROT_READ | PROT_WRITE grants read and write access to the pages
    // MAP_PRIVATE | MAP_ANONYMOUS allocates isolated, clean virtual memory outside the filesystem
    void* addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (addr == MAP_FAILED) {
        return NULL;
    }
    return addr;
}

bool teko_sys_free_pages(void* address, size_t size) {
    if (!address || size == 0) return false;
    // munmap discards the block and recycles the space in the Kernel page table in O(1)
    return munmap(address, size) == 0;
}
#endif
