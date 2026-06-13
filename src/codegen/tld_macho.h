#ifndef TLD_MACHO_H
#define TLD_MACHO_H

#include <stdint.h>
#include <stdbool.h>

#define MH_MAGIC_64          0xFEEDFACF
#define MH_EXECUTE           0x2
#define MH_DYLIB             0x6        // NEW: File type for Dynamic Libraries (.dylib)

#define CPU_TYPE_X86_64      0x01000007
#define CPU_TYPE_ARM64       0x0100000C
#define CPU_SUBTYPE_ALL      0x0

#define LC_SEGMENT_64        0x19
#define LC_MAIN              0x80000028
#define LC_BUILD_VERSION     0x32

#define PLATFORM_MACOS       1

#define VM_PROT_NONE         0x0
#define VM_PROT_READ         0x1
#define VM_PROT_WRITE        0x2
#define VM_PROT_EXECUTE      0x4

typedef struct {
    uint32_t magic;
    int32_t  cputype;
    int32_t  cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
    uint32_t reserved;
} TekoMachHeader64;

typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    char     segname[16];
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;
    uint64_t filesize;
    int32_t  maxprot;
    int32_t  initprot;
    uint32_t nsects;
    uint32_t flags;
} TekoMachSegmentCommand64;

typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    uint64_t entryoff;
    uint64_t stacksize;
} TekoMachEntryPointCommand;

typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t platform;
    uint32_t minos;
    uint32_t sdk;
    uint32_t ntools;
} TekoMachBuildVersionCommand;

// Updated signature: Now receives 'is_shared' from the project manager
bool tld_macho_write_executable(const char* filename, const uint8_t* machine_code, uint32_t code_size, int32_t cpu_type, bool is_shared);

#endif // TLD_MACHO_H
