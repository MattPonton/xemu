/*
 * XEMU Memory Viewer - Demonstrates accessing Xbox UMA (64MB)
 *
 * This file shows how to access the Xbox's Unified Memory Architecture
 * for implementing a memory viewer feature in XEMU.
 *
 * Copyright (c) 2025 Example
 */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "system/memory.h" // MemoryRegion definition, memory_region_* helpers
#include "system/address-spaces.h"
#include "hw/xbox/nv2a/nv2a.h"
#include "system/runstate.h"
#include "system/cpus.h" // qemu_get_cpu
#include "hw/core/cpu.h" // CPUState
#include "exec/cpu-common.h" // cpu_memory_rw_debug

#include "xemu-memory-viewer.h" // prototypes for the functions defined here

#include <time.h>

typedef struct XboxMemoryViewer {
    MemoryRegion *xbox_ram;    // Points to the Xbox's UMA
    void *ram_ptr;             // Direct pointer to RAM data
    uint64_t ram_size;         // Size of Xbox RAM (typically 64MB)
} XboxMemoryViewer;

static XboxMemoryViewer g_memory_viewer = { 0 };

#define XEMU_VIRT_DUMP_START 0x00010000u
#define XEMU_VIRT_DUMP_SIZE  0x04000000u  /* 64MB */
#define XEMU_VIRT_DUMP_END   (XEMU_VIRT_DUMP_START + XEMU_VIRT_DUMP_SIZE)

static void xemu_mv_pause_vm(bool *out_was_running)
{
    bool was_running = runstate_is_running();
    if (out_was_running) *out_was_running = was_running;

    if (was_running) {
        /* Best-effort “stop ASAP” – this pauses CPU/devices which also stops rendering updates */
        vm_stop(RUN_STATE_PAUSED);
    }
}

static void xemu_mv_resume_vm(bool was_running)
{
    if (was_running) {
        vm_start();
    }
}

/*
 * Initialize the memory viewer by finding the Xbox RAM region
 */
bool xemu_memory_viewer_init(void)
{
    // Look for the xbox.ram memory region
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *subregion;
    
    QTAILQ_FOREACH(subregion, &system_memory->subregions, subregions_link) {
        if (subregion->name && strcmp(subregion->name, "xbox.ram") == 0) {
            g_memory_viewer.xbox_ram = subregion;
            g_memory_viewer.ram_ptr = memory_region_get_ram_ptr(subregion);
            g_memory_viewer.ram_size = memory_region_size(subregion);
            
            printf("Xbox Memory Viewer initialized:\n");
            printf("  RAM Region: %s\n", subregion->name);
            printf("  RAM Size: %" PRIu64 " MB\n", g_memory_viewer.ram_size / (1024 * 1024));
            printf("  RAM Ptr: %p\n", g_memory_viewer.ram_ptr);
            
            return true;
        }
    }
    
    printf("Error: Could not find xbox.ram memory region\n");
    return false;
}

/*
 * Normalize the Xbox RAM Address to make sure we're checking Physical, Kernal-mapped mirror, and Shadow mirror RAM.
 */
static bool normalize_xbox_ram_addr(uint32_t in, uint32_t *out, uint64_t ram_size) {
    if (in < ram_size) {
        *out = in;
        return true;
    }

    if (in >= 0x80000000 && (uint64_t)(in - 0x80000000) < ram_size) {
        *out = in - 0x80000000;
        return true;
    }

    if (in >= 0xF0000000 && (uint64_t)(in - 0xF0000000) < ram_size) {
        *out = in - 0xF0000000;
        return true;
    }

    return false;
}

/*
 * Read a block of memory from Xbox RAM
 */
bool xemu_memory_viewer_read(uint32_t xbox_addr, void *buffer, size_t size)
{
    if (!buffer || size == 0 || !g_memory_viewer.xbox_ram || !g_memory_viewer.ram_ptr) {
        return false;
    }
    
    // // Validate address range
    // if (xbox_addr >= g_memory_viewer.ram_size || 
    //     xbox_addr + size > g_memory_viewer.ram_size) {
    //     printf("Memory read out of bounds: 0x%08X + %zu > %" PRIu64 "\n", 
    //            xbox_addr, size, g_memory_viewer.ram_size);
    //     return false;
    // }
    
    // // Direct memory copy from Xbox RAM
    // memcpy(buffer, (uint8_t*)g_memory_viewer.ram_ptr + xbox_addr, size);

    uint32_t phys;
    if (!normalize_xbox_ram_addr(xbox_addr, &phys, g_memory_viewer.ram_size)) {
        printf("Memory read unsupported addr: 0x%08X (ram_size=0x%08" PRIX64 ")\n",
               xbox_addr, g_memory_viewer.ram_size);
        return false;
    }

    // Validate range using *phys*
    if ((uint64_t)phys + (uint64_t)size > g_memory_viewer.ram_size) {
        printf("Memory read out of bounds: addr=0x%08X phys=0x%08X size=%zu ram_size=0x%08" PRIX64 "\n",
               xbox_addr, phys, size, g_memory_viewer.ram_size);
        return false;
    }

    memcpy(buffer, (uint8_t *)g_memory_viewer.ram_ptr + phys, size);
    return true;
}

/*
 * Write a block of memory to Xbox RAM
 */
// bool xemu_memory_viewer_write(uint32_t xbox_addr, const void *buffer, size_t size)
// {
//     if (!g_memory_viewer.xbox_ram || !g_memory_viewer.ram_ptr) {
//         return false;
//     }
    
//     // Validate address range
//     if (xbox_addr >= g_memory_viewer.ram_size || 
//         xbox_addr + size > g_memory_viewer.ram_size) {
//         printf("Memory write out of bounds: 0x%08X + %zu > %" PRIu64 "\n", 
//                xbox_addr, size, g_memory_viewer.ram_size);
//         return false;
//     }
    
//     // Direct memory copy to Xbox RAM
//     memcpy((uint8_t*)g_memory_viewer.ram_ptr + xbox_addr, buffer, size);
    
//     // Mark the memory region as dirty for graphics updates
//     memory_region_set_dirty(g_memory_viewer.xbox_ram, xbox_addr, size);
    
//     return true;
// }
bool xemu_memory_viewer_write(uint32_t xbox_addr, const void *data, size_t size)
{
    if (!data || size == 0) {
        return false;
    }

    if (!g_memory_viewer.xbox_ram || !g_memory_viewer.ram_ptr) {
        return false;
    }

    uint32_t phys;
    if (!normalize_xbox_ram_addr(xbox_addr, &phys, g_memory_viewer.ram_size)) {
        printf("Memory write unsupported addr: 0x%08X (ram_size=0x%08" PRIX64 ")\n",
               xbox_addr, g_memory_viewer.ram_size);
        return false;
    }

    // Validate range using phys, with 64-bit math to avoid overflow
    if ((uint64_t)phys + (uint64_t)size > g_memory_viewer.ram_size) {
        printf("Memory write out of bounds: addr=0x%08X phys=0x%08X size=%zu ram_size=0x%08" PRIX64 "\n",
               xbox_addr, phys, size, g_memory_viewer.ram_size);
        return false;
    }

    memcpy((uint8_t *)g_memory_viewer.ram_ptr + phys, data, size);
    return true;
}

/*
 * Get a direct pointer to Xbox memory (use with caution!)
 */
void *xemu_memory_viewer_get_ptr(uint32_t xbox_addr, size_t size)
{
    if (!g_memory_viewer.xbox_ram || !g_memory_viewer.ram_ptr) {
        return NULL;
    }
    
    if (xbox_addr >= g_memory_viewer.ram_size || 
        xbox_addr + size > g_memory_viewer.ram_size) {
        return NULL;
    }
    
    return (uint8_t*)g_memory_viewer.ram_ptr + xbox_addr;
}

/*
 * Get Xbox memory region info
 */
bool xemu_memory_viewer_get_info(uint64_t *size, void **ptr)
{
    if (!g_memory_viewer.xbox_ram) {
        return false;
    }
    
    if (size) *size = g_memory_viewer.ram_size;
    if (ptr) *ptr = g_memory_viewer.ram_ptr;
    
    return true;
}

/*
 * Search for a pattern in Xbox memory
 */
// uint32_t xemu_memory_viewer_search(const void *pattern, size_t pattern_size, 
//                                    uint32_t start_addr)
// {
//     if (!g_memory_viewer.xbox_ram || !g_memory_viewer.ram_ptr) {
//         return 0xFFFFFFFF; // Invalid address
//     }
    
//     uint8_t *ram = (uint8_t*)g_memory_viewer.ram_ptr;
//     uint8_t *search_pattern = (uint8_t*)pattern;
    
//     for (uint32_t addr = start_addr; 
//          addr <= g_memory_viewer.ram_size - pattern_size; 
//          addr++) {
        
//         if (memcmp(ram + addr, search_pattern, pattern_size) == 0) {
//             return addr;
//         }
//     }
    
//     return 0xFFFFFFFF; // Not found
// }
static uint32_t get_xbox_addr_base(uint32_t addr)
{
    // Preserve the "domain" the user typed, so results feel consistent
    if (addr >= 0xF0000000) return 0xF0000000;
    if (addr >= 0x80000000) return 0x80000000;
    return 0x00000000;
}

bool xemu_memory_viewer_search(uint32_t start_addr,
                              const uint8_t *pattern,
                              size_t pattern_len,
                              uint32_t *out_found_addr)
{
    if (!pattern || pattern_len == 0 || !out_found_addr) {
        return false;
    }

    if (!g_memory_viewer.xbox_ram || !g_memory_viewer.ram_ptr) {
        return false;
    }

    // Normalize start address into physical RAM offset
    uint32_t phys_start;
    if (!normalize_xbox_ram_addr(start_addr, &phys_start, g_memory_viewer.ram_size)) {
        printf("Memory search unsupported start addr: 0x%08X (ram_size=0x%08" PRIX64 ")\n",
               start_addr, g_memory_viewer.ram_size);
        return false;
    }

    // If pattern is bigger than RAM, impossible
    if ((uint64_t)pattern_len > g_memory_viewer.ram_size) {
        return false;
    }

    // If start is too close to the end to fit the pattern, no match possible
    if ((uint64_t)phys_start + (uint64_t)pattern_len > g_memory_viewer.ram_size) {
        return false;
    }

    const uint8_t *ram = (const uint8_t *)g_memory_viewer.ram_ptr;

    // Search range: [phys_start, ram_size - pattern_len]
    uint64_t last = g_memory_viewer.ram_size - (uint64_t)pattern_len;

    for (uint64_t i = (uint64_t)phys_start; i <= last; i++) {
        if (memcmp(ram + i, pattern, pattern_len) == 0) {
            uint32_t base = get_xbox_addr_base(start_addr);
            *out_found_addr = base + (uint32_t)i;
            return true;
        }
    }

    return false;
}

/*
 * Dump memory region to console (for debugging)
 */
void xemu_memory_viewer_dump(uint32_t xbox_addr, size_t size)
{
    if (!g_memory_viewer.xbox_ram || !g_memory_viewer.ram_ptr) {
        printf("Memory viewer not initialized\n");
        return;
    }
    
    if (xbox_addr >= g_memory_viewer.ram_size || 
        xbox_addr + size > g_memory_viewer.ram_size) {
        printf("Dump address out of bounds\n");
        return;
    }
    
    uint8_t *data = (uint8_t*)g_memory_viewer.ram_ptr + xbox_addr;
    
    printf("Memory dump at 0x%08X (size: %zu bytes):\n", xbox_addr, size);
    
    for (size_t i = 0; i < size; i += 16) {
        printf("%08X: ", xbox_addr + (uint32_t)i);
        
        // Hex dump
        for (size_t j = 0; j < 16 && i + j < size; j++) {
            printf("%02X ", data[i + j]);
        }
        
        // Padding
        for (size_t j = size - i; j < 16; j++) {
            printf("   ");
        }
        
        printf(" |");
        
        // ASCII dump
        for (size_t j = 0; j < 16 && i + j < size; j++) {
            char c = data[i + j];
            printf("%c", (c >= 32 && c <= 126) ? c : '.');
        }
        
        printf("|\n");
    }
    printf("\n");
}

/*
 * Example usage function - demonstrates how to use the memory viewer
 */
void xemu_memory_viewer_example(void)
{
    if (!xemu_memory_viewer_init()) {
        return;
    }
    
    // Example 1: Read first 256 bytes of Xbox memory
    uint8_t buffer[256];
    if (xemu_memory_viewer_read(0x00000000, buffer, sizeof(buffer))) {
        printf("Successfully read %zu bytes from Xbox memory start\n", sizeof(buffer));
        xemu_memory_viewer_dump(0x00000000, 64); // Dump first 64 bytes
    }
    
    // Example 2: Search for a specific pattern
    const char pattern[] = "Xbox";
    // uint32_t found_addr = xemu_memory_viewer_search(pattern, strlen(pattern), 0);
    // if (found_addr != 0xFFFFFFFF) {
    //     printf("Found pattern 'Xbox' at address: 0x%08X\n", found_addr);
    // }
    uint32_t found_addr = 0xFFFFFFFF;

    if (xemu_memory_viewer_search(0x00000000,
                                (const uint8_t *)pattern,
                                strlen(pattern),
                                &found_addr)) {
        printf("Found pattern 'Xbox' at address: 0x%08X\n", found_addr);
    } else {
        printf("Pattern 'Xbox' not found\n");
    }

    // Example 3: Get direct pointer for performance-critical operations
    void *direct_ptr = xemu_memory_viewer_get_ptr(0x00000000, 1024);
    if (direct_ptr) {
        printf("Got direct pointer to Xbox memory: %p\n", direct_ptr);
        // Can now access memory directly: ((uint8_t*)direct_ptr)[offset]
    }
    
    // Example 4: Write some data (be careful with this!)
    const char test_data[] = "XEMU Memory Viewer Test";
    if (xemu_memory_viewer_write(0x00001000, test_data, strlen(test_data))) {
        printf("Successfully wrote test data to Xbox memory\n");
        xemu_memory_viewer_dump(0x00001000, 32);
    }
}

/*
 * Save Xbox memory to a dump file with timestamp
 */
void xemu_memory_viewer_save_dump_file(void)
{
    if (!g_memory_viewer.xbox_ram || !g_memory_viewer.ram_ptr) {
        printf("Memory viewer not initialized\n");
        return;
    }
    
    // Generate filename with timestamp
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char filename[256];
    strftime(filename, sizeof(filename), "xemu_memory_dump_%Y%m%d_%H%M%S.bin", tm_info);
    
    // Save to file
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        printf("Failed to create dump file: %s\n", filename);
        return;
    }
    
    size_t written = fwrite(g_memory_viewer.ram_ptr, 1, g_memory_viewer.ram_size, fp);
    fclose(fp);
    
    if (written == g_memory_viewer.ram_size) {
        printf("Memory dump saved: %s (%" PRIu64 " bytes)\n", filename, g_memory_viewer.ram_size);
    } else {
        printf("Failed to write complete dump (wrote %zu/%" PRIu64 " bytes)\n", 
               written, g_memory_viewer.ram_size);
    }
}

/*
 * Load Xbox memory from a dump file
 */
bool xemu_memory_viewer_load_dump_file(const char *filename)
{
    if (!g_memory_viewer.xbox_ram || !g_memory_viewer.ram_ptr) {
        printf("Memory viewer not initialized\n");
        return false;
    }
    
    if (!filename) {
        printf("Invalid filename provided\n");
        return false;
    }
    
    // Open file
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        printf("Failed to open dump file: %s\n", filename);
        return false;
    }
    
    // Get file size
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    if (file_size < 0) {
        printf("Failed to get file size for: %s\n", filename);
        fclose(fp);
        return false;
    }
    
    // Validate file size
    if ((uint64_t)file_size > g_memory_viewer.ram_size) {
        printf("Warning: Dump file (%ld bytes) is larger than Xbox RAM (%" PRIu64 " bytes)\n", 
               file_size, g_memory_viewer.ram_size);
        printf("Only the first %" PRIu64 " bytes will be loaded\n", g_memory_viewer.ram_size);
        file_size = g_memory_viewer.ram_size;
    } else if ((uint64_t)file_size < g_memory_viewer.ram_size) {
        printf("Warning: Dump file (%ld bytes) is smaller than Xbox RAM (%" PRIu64 " bytes)\n", 
               file_size, g_memory_viewer.ram_size);
        printf("Only %ld bytes will be overwritten\n", file_size);
    }
    
    // Read file data directly into Xbox memory
    size_t bytes_read = fread(g_memory_viewer.ram_ptr, 1, file_size, fp);
    fclose(fp);
    
    if (bytes_read != (size_t)file_size) {
        printf("Failed to read complete dump file (read %zu/%ld bytes)\n", 
               bytes_read, file_size);
        return false;
    }
    
    // Mark memory as dirty for graphics updates
    memory_region_set_dirty(g_memory_viewer.xbox_ram, 0, bytes_read);
    
    printf("Memory dump applied successfully: %s (%zu bytes loaded)\n", 
           filename, bytes_read);
    printf("Xbox memory has been overwritten with dump data\n");
    
    return true;
}

/*
 * Reset NV2A GPU state to prevent crashes after memory restoration
 */
void xemu_memory_viewer_reset_nv2a_state(void)
{
    printf("Resetting NV2A GPU state for safe memory restoration...\n");
    
    // Critical GPU regions that must be zeroed to prevent PFIFO crashes
    // These are more targeted and avoid clearing important system areas
    const struct {
        uint32_t start;
        uint32_t size;
        const char *description;
    } critical_gpu_regions[] = {
        /* Only clear the most critical PFIFO areas that cause crashes */
        {0x00001000, 0x00001000, "PFIFO command area"},
        {0x00002000, 0x00002000, "PFIFO push buffer"},
        {0x00010000, 0x00010000, "Primary GPU command buffers"},
        {0x00020000, 0x00010000, "Secondary GPU buffers"},
    };
    
    // Zero out only the  critical GPU regions (Not the entire first 1MB)
    for (size_t i = 0; i < sizeof(critical_gpu_regions) / sizeof(critical_gpu_regions[0]); i++) {
        uint32_t addr = critical_gpu_regions[i].start;
        uint32_t clear_size = critical_gpu_regions[i].size;
        
        if (addr + clear_size <= g_memory_viewer.ram_size) {
            // Zero out the region to prevent invalid GPU commands
            memset((uint8_t*)g_memory_viewer.ram_ptr + addr, 0, clear_size);
            printf("  Cleared: 0x%08X-0x%08X (%s)\n", 
                   addr, addr + clear_size - 1, critical_gpu_regions[i].description);
            
            // Mark as dirty immediately
            memory_region_set_dirty(g_memory_viewer.xbox_ram, addr, clear_size);
        }
    }
    
    printf("NV2A GPU state reset completed - critical GPU regions cleared\n");
}

/*
 * Load Xbox memory from a dump file with graphics safety
 */
bool xemu_memory_viewer_load_dump_file_safe(const char *filename, bool reset_graphics)
{
    if (!g_memory_viewer.xbox_ram || !g_memory_viewer.ram_ptr) {
        printf("Memory viewer not initialized\n");
        return false;
    }
    
    if (!filename) {
        printf("Invalid filename provided\n");
        return false;
    }
    
    // Check if emulator is running and pause it for safety
    bool was_running = runstate_is_running();
    if (was_running) {
        printf("Pausing emulator for safe memory restoration...\n");
        vm_stop(RUN_STATE_PAUSED);
    }
    
    // Open file
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        printf("Failed to open dump file: %s\n", filename);
        if (was_running) {
            vm_start();
        }
        return false;
    }
    
    // Get file size
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    if (file_size < 0) {
        printf("Failed to get file size for: %s\n", filename);
        fclose(fp);
        if (was_running) {
            vm_start();
        }
        return false;
    }
    
    // Validate file size
    if ((uint64_t)file_size > g_memory_viewer.ram_size) {
        printf("Warning: Dump file (%ld bytes) is larger than Xbox RAM (%" PRIu64 " bytes)\n", 
               file_size, g_memory_viewer.ram_size);
        printf("Only the first %" PRIu64 " bytes will be loaded\n", g_memory_viewer.ram_size);
        file_size = g_memory_viewer.ram_size;
    } else if ((uint64_t)file_size < g_memory_viewer.ram_size) {
        printf("Warning: Dump file (%ld bytes) is smaller than Xbox RAM (%" PRIu64 " bytes)\n", 
               file_size, g_memory_viewer.ram_size);
        printf("Only %ld bytes will be overwritten\n", file_size);
    }
    
    // Read file data into temporary buffer first
    uint8_t *temp_buffer = (uint8_t*)malloc(file_size);
    if (!temp_buffer) {
        printf("Failed to allocate temporary buffer for dump file\n");
        fclose(fp);
        if (was_running) {
            vm_start();
        }
        return false;
    }
    
    size_t bytes_read = fread(temp_buffer, 1, file_size, fp);
    fclose(fp);
    
    if (bytes_read != (size_t)file_size) {
        printf("Failed to read complete dump file (read %zu/%ld bytes)\n", 
               bytes_read, file_size);
        free(temp_buffer);
        if (was_running) {
            vm_start();
        }
        return false;
    }
    
    /* Clear GPU state before loading if requested to prevent conflicts with restored data*/
    if (reset_graphics) {
        printf("Pre-load GPU state cleanup...\n");
        xemu_memory_viewer_reset_nv2a_state();
    }

    // Copy data to Xbox memory
    memcpy(g_memory_viewer.ram_ptr, temp_buffer, bytes_read);
    free(temp_buffer);
    
    // Mark memory as dirty for graphics updates
    memory_region_set_dirty(g_memory_viewer.xbox_ram, 0, bytes_read);
    
    printf("Memory dump applied successfully: %s (%zu bytes loaded)\n", 
           filename, bytes_read);
    printf("Xbox memory has been restored with dump data\n");
    
    // Resume emulator if it was running
    if (was_running) {
        printf("Resuming emulator...\n");
        vm_start();
    }
    
    return true;
}

/*
 * Load partial Xbox memory dump (safer for specific regions)
 */
bool xemu_memory_viewer_load_dump_partial(const char *filename, uint32_t target_addr, uint32_t max_size)
{
    if (!g_memory_viewer.xbox_ram || !g_memory_viewer.ram_ptr) {
        printf("Memory viewer not initialized\n");
        return false;
    }
    
    if (!filename) {
        printf("Invalid filename provided\n");
        return false;
    }
    
    // Validate target address and size
    if (target_addr >= g_memory_viewer.ram_size) {
        printf("Target address 0x%08X is out of bounds\n", target_addr);
        return false;
    }
    
    if (target_addr + max_size > g_memory_viewer.ram_size) {
        max_size = g_memory_viewer.ram_size - target_addr;
        printf("Adjusted max size to %" PRIu32 " to stay within bounds\n", max_size);
    }
    
    // Open file
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        printf("Failed to open dump file: %s\n", filename);
        return false;
    }
    
    // Get file size
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    if (file_size < 0) {
        printf("Failed to get file size for: %s\n", filename);
        fclose(fp);
        return false;
    }
    
    // Determine how much to load
    uint32_t load_size = (file_size < max_size) ? file_size : max_size;
    
    // Read file data into target memory location
    size_t bytes_read = fread((uint8_t*)g_memory_viewer.ram_ptr + target_addr, 1, load_size, fp);
    fclose(fp);
    
    if (bytes_read != load_size) {
        printf("Failed to read dump file (read %zu/%u bytes)\n", bytes_read, load_size);
        return false;
    }
    
    // Mark memory region as dirty
    memory_region_set_dirty(g_memory_viewer.xbox_ram, target_addr, bytes_read);
    
    printf("Partial memory dump loaded: %s (%zu bytes at 0x%08X)\n", 
           filename, bytes_read, target_addr);
    
    return true;
}

/*
 * Save Xbox memory to a dump file excluding GPU-sensitive regions
 */
void xemu_memory_viewer_save_dump_file_safe(void)
{
    if (!g_memory_viewer.xbox_ram || !g_memory_viewer.ram_ptr) {
        printf("Memory viewer not initialized\n");
        return;
    }
    
    // Generate filename with timestamp
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char filename[256];
    strftime(filename, sizeof(filename), "xemu_memory_dump_safe_%Y%m%d_%H%M%S.bin", tm_info);
    
    // Create a copy of memory with GPU regions zeroed out
    uint8_t *safe_memory = malloc(g_memory_viewer.ram_size);
    if (!safe_memory) {
        printf("Failed to allocate memory for safe dump\n");
        return;
    }
    
    // Copy all memory first
    memcpy(safe_memory, g_memory_viewer.ram_ptr, g_memory_viewer.ram_size);
    
    // Zero out GPU-sensitive regions that could cause crashes when restored
    const struct {
        uint32_t start;
        uint32_t size;
        const char *description;
    } gpu_regions[] = {
        {0x00000000, 0x00001000, "System vectors"},
        {0x00010000, 0x00010000, "GPU command buffers"},
        {0x00020000, 0x00010000, "GPU push buffers"},
        {0x00030000, 0x00010000, "PFIFO command area"},
        {0x00040000, 0x00010000, "Graphics heap area"},
        {0x00050000, 0x00010000, "DMA buffers"},
        {0x00060000, 0x00010000, "Surface buffers"},
        {0x00070000, 0x00010000, "Texture cache area"},
    };
    
    printf("Creating safe memory dump (excluding GPU regions):\n");
    for (size_t i = 0; i < sizeof(gpu_regions) / sizeof(gpu_regions[0]); i++) {
        uint32_t start = gpu_regions[i].start;
        uint32_t size = gpu_regions[i].size;
        
        if (start + size <= g_memory_viewer.ram_size) {
            memset(safe_memory + start, 0, size);
            printf("  Excluded: 0x%08X-0x%08X (%s)\n", 
                   start, start + size - 1, gpu_regions[i].description);
        }
    }
    
    // Save to file
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        printf("Failed to create safe dump file: %s\n", filename);
        free(safe_memory);
        return;
    }
    
    size_t written = fwrite(safe_memory, 1, g_memory_viewer.ram_size, fp);
    fclose(fp);
    free(safe_memory);
    
    if (written == g_memory_viewer.ram_size) {
        printf("Safe memory dump saved: %s (%" PRIu64 " bytes)\n", filename, g_memory_viewer.ram_size);
        printf("GPU-sensitive regions excluded for crash-free restoration\n");
    } else {
        printf("Failed to write complete safe dump (wrote %zu/%" PRIu64 " bytes)\n", 
               written, g_memory_viewer.ram_size);
    }
}

static CPUState *xemu_mv_cpu0(void) {
    return qemu_get_cpu(0);
}

bool xemu_memory_viewer_read_virt(uint32_t vaddr, void *buffer, size_t size) {
    if (!buffer || size == 0) return false;

    CPUState *cpu = xemu_mv_cpu0();
    
    if (!cpu) return false;

    uint8_t *out = (uint8_t *)buffer;
    size_t remaining = size;

    while (remaining > 0) {
        uint32_t page_off = vaddr & 0xFFFu;
        size_t chunk = 0x1000u - page_off;
        if (chunk > remaining)
            chunk = remaining;
        
        int rc = cpu_memory_rw_debug(cpu, vaddr, out, (int)chunk, 0 /*is_write*/);
        if (rc != 0) {
            return false; // unmapped / failed translation
        }

        vaddr += (uint32_t)chunk;
        out += chunk;
        remaining -= chunk;
    }

    return true;
}

bool xemu_memory_viewer_write_virt(uint32_t vaddr, const void *data, size_t size)
{
    if (!data || size == 0) return false;

    CPUState *cpu = xemu_mv_cpu0();
    if (!cpu) return false;

    const uint8_t *in = (const uint8_t *)data;
    size_t remaining = size;

    while (remaining > 0) {
        uint32_t page_off = vaddr & 0xFFFu;
        size_t chunk = 0x1000u - page_off;
        if (chunk > remaining) chunk = remaining;

        int rc = cpu_memory_rw_debug(cpu, vaddr, (void*)in, (int)chunk, 1 /*is_write*/);
        if (rc != 0) {
            return false;
        }

        vaddr += (uint32_t)chunk;
        in += chunk;
        remaining -= chunk;
    }

    return true;
}

bool xemu_memory_viewer_search_virt(uint32_t start_vaddr,
                                    uint32_t bytes_to_scan,
                                    const uint8_t *pattern,
                                    size_t pattern_len,
                                    uint32_t *out_found_vaddr)
{
    if (!pattern || pattern_len == 0 || !out_found_vaddr) return false;
    if (bytes_to_scan < pattern_len) return false;
    if (pattern_len > 256) return false;

    uint8_t page[0x1000];
    uint8_t carry[256];
    size_t carry_len = 0;

    uint32_t v = start_vaddr;
    uint32_t end = start_vaddr + bytes_to_scan; // ok for your current 64MB window

    bool first_page = true;

    while (v < end) {
        uint32_t page_base = v & ~0xFFFu;
        uint32_t start_off = v & 0xFFFu;

        if (!xemu_memory_viewer_read_virt(page_base, page, sizeof(page))) {
            v = page_base + 0x1000u;
            carry_len = 0;
            first_page = false;
            continue;
        }

        const size_t window_len = carry_len + sizeof(page);

        // IMPORTANT: don’t rescan bytes before start_vaddr on the first page
        size_t i0 = 0;
        if (first_page) {
            // carry_len is 0 on first_page in your code, but this is safe either way
            i0 = carry_len + (size_t)start_off;
            if (i0 > window_len) i0 = window_len;
        }

        for (size_t i = i0; i + pattern_len <= window_len; i++) {
            size_t matched = 0;
            while (matched < pattern_len) {
                size_t idx = i + matched;
                uint8_t b = (idx < carry_len) ? carry[idx] : page[idx - carry_len];
                if (b != pattern[matched]) break;
                matched++;
            }
            if (matched == pattern_len) {
                uint32_t found = page_base;
                if (i < carry_len) {
                    found -= (uint32_t)(carry_len - i);
                } else {
                    found += (uint32_t)(i - carry_len);
                }
                *out_found_vaddr = found;
                return true;
            }
        }

        carry_len = (pattern_len > 1) ? (pattern_len - 1) : 0;
        if (carry_len > 0) {
            memcpy(carry, page + sizeof(page) - carry_len, carry_len);
        }

        v = page_base + 0x1000u;
        first_page = false;
    }

    return false;
}

size_t xemu_memory_viewer_search_virt_all(uint32_t start_vaddr,
                                         uint32_t bytes_to_scan,
                                         const uint8_t *pattern,
                                         size_t pattern_len,
                                         uint32_t *out_found_vaddrs,
                                         size_t max_found)
{
    if (!pattern || pattern_len == 0 || !out_found_vaddrs || max_found == 0) return 0;
    if (bytes_to_scan < pattern_len) return 0;
    if (pattern_len > 256) return 0;

    uint8_t page[0x1000];
    uint8_t carry[256];
    size_t carry_len = 0;

    uint32_t v = start_vaddr;
    uint32_t end = start_vaddr + bytes_to_scan;

    bool first_page = true;
    size_t found_count = 0;

    while (v < end) {
        uint32_t page_base = v & ~0xFFFu;
        uint32_t start_off = v & 0xFFFu;

        if (!xemu_memory_viewer_read_virt(page_base, page, sizeof(page))) {
            v = page_base + 0x1000u;
            carry_len = 0;
            first_page = false;
            continue;
        }

        const size_t window_len = carry_len + sizeof(page);

        size_t i0 = 0;
        if (first_page) {
            i0 = carry_len + (size_t)start_off;
            if (i0 > window_len) i0 = window_len;
        }

        for (size_t i = i0; i + pattern_len <= window_len; i++) {
            size_t matched = 0;
            while (matched < pattern_len) {
                size_t idx = i + matched;
                uint8_t b = (idx < carry_len) ? carry[idx] : page[idx - carry_len];
                if (b != pattern[matched]) break;
                matched++;
            }
            if (matched == pattern_len) {
                // Compute match virtual address
                uint32_t found = page_base;
                if (i < carry_len) {
                    found -= (uint32_t)(carry_len - i);
                } else {
                    found += (uint32_t)(i - carry_len);
                }

                // Enforce search window start/end (avoid matches that start before start_vaddr)
                if (found < start_vaddr) {
                    continue;
                }
                // (Optional) enforce end bound if you want strictness:
                // if ((uint64_t)found + pattern_len > (uint64_t)end) continue;

                out_found_vaddrs[found_count++] = found;
                if (found_count >= max_found) {
                    return found_count;
                }
            }
        }

        // Carry last (pattern_len-1) bytes for cross-page matches
        carry_len = (pattern_len > 1) ? (pattern_len - 1) : 0;
        if (carry_len > 0) {
            memcpy(carry, page + sizeof(page) - carry_len, carry_len);
        }

        v = page_base + 0x1000u;
        first_page = false;
    }

    return found_count;
}

bool xemu_memory_viewer_save_dump_file_virt_range(const char *filename,
                                                  uint32_t vstart,
                                                  uint32_t vsize)
{
    if (!filename || !filename[0]) return false;

    /* Ensure we have a cpu + RAM region discovered (init normally already called from UI) */
    if (!g_memory_viewer.xbox_ram || !g_memory_viewer.ram_ptr) {
        printf("Memory viewer not initialized\n");
        return false;
    }

    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        printf("Failed to create dump file: %s\n", filename);
        return false;
    }

    bool was_running = false;
    xemu_mv_pause_vm(&was_running);

    const size_t kChunk = 0x10000; /* 64KB */
    uint8_t *buf = (uint8_t *)malloc(kChunk);
    if (!buf) {
        fclose(fp);
        xemu_mv_resume_vm(was_running);
        printf("Out of memory allocating dump buffer\n");
        return false;
    }

    uint32_t failures = 0;
    uint32_t off = 0;

    while (off < vsize) {
        size_t chunk = kChunk;
        if ((uint64_t)off + chunk > vsize) {
            chunk = (size_t)(vsize - off);
        }

        uint32_t addr = vstart + off;

        /* If a page is unmapped, we still write zeros so file stays a fixed size */
        if (!xemu_memory_viewer_read_virt(addr, buf, chunk)) {
            memset(buf, 0, chunk);
            failures++;
        }

        if (fwrite(buf, 1, chunk, fp) != chunk) {
            printf("Failed writing dump data at vaddr=0x%08X\n", addr);
            free(buf);
            fclose(fp);
            xemu_mv_resume_vm(was_running);
            return false;
        }

        off += (uint32_t)chunk;
    }

    free(buf);
    fclose(fp);
    xemu_mv_resume_vm(was_running);

    if (failures) {
        printf("Virtual dump saved with %u unmapped read chunk(s) zero-filled: %s\n",
               failures, filename);
    } else {
        printf("Virtual dump saved: %s (%u bytes, vaddr 0x%08X..0x%08X)\n",
               filename, vsize, vstart, vstart + vsize);
    }

    return true;
}

void xemu_memory_viewer_save_dump_file_virt(void)
{
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);

    char filename[256];
    strftime(filename, sizeof(filename),
             "xemu_virtual_dump_%Y%m%d_%H%M%S.bin",
             tm_info);

    (void)xemu_memory_viewer_save_dump_file_virt_range(filename,
                                                       XEMU_VIRT_DUMP_START,
                                                       XEMU_VIRT_DUMP_SIZE);
}

bool xemu_memory_viewer_load_dump_file_virt_range(const char *filename,
                                                  uint32_t vstart,
                                                  uint32_t vsize)
{
    if (!filename || !filename[0]) return false;

    if (!g_memory_viewer.xbox_ram || !g_memory_viewer.ram_ptr) {
        printf("Memory viewer not initialized\n");
        return false;
    }

    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        printf("Failed to open dump file: %s\n", filename);
        return false;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size < 0) {
        fclose(fp);
        printf("Failed to get file size for: %s\n", filename);
        return false;
    }

    uint32_t load_size = vsize;
    if ((uint64_t)file_size < (uint64_t)vsize) {
        printf("Warning: dump is smaller than expected (%ld < %u). Loading partial.\n",
               file_size, vsize);
        load_size = (uint32_t)file_size;
    } else if ((uint64_t)file_size > (uint64_t)vsize) {
        printf("Warning: dump is larger than expected (%ld > %u). Loading first %u bytes.\n",
               file_size, vsize, vsize);
        load_size = vsize;
    }

    bool was_running = false;
    xemu_mv_pause_vm(&was_running);

    const size_t kChunk = 0x10000; /* 64KB */
    uint8_t *buf = (uint8_t *)malloc(kChunk);
    if (!buf) {
        fclose(fp);
        xemu_mv_resume_vm(was_running);
        printf("Out of memory allocating load buffer\n");
        return false;
    }

    uint32_t off = 0;
    while (off < load_size) {
        size_t chunk = kChunk;
        if ((uint64_t)off + chunk > load_size) {
            chunk = (size_t)(load_size - off);
        }

        size_t rd = fread(buf, 1, chunk, fp);
        if (rd != chunk) {
            printf("Failed reading dump data at file offset 0x%X\n", off);
            free(buf);
            fclose(fp);
            xemu_mv_resume_vm(was_running);
            return false;
        }

        uint32_t addr = vstart + off;
        if (!xemu_memory_viewer_write_virt(addr, buf, chunk)) {
            printf("Failed writing virtual memory at vaddr=0x%08X\n", addr);
            free(buf);
            fclose(fp);
            xemu_mv_resume_vm(was_running);
            return false;
        }

        off += (uint32_t)chunk;
    }

    free(buf);
    fclose(fp);

    /* Make sure GPU sees changes (cheap sledgehammer) */
    memory_region_set_dirty(g_memory_viewer.xbox_ram, 0, (uint64_t)g_memory_viewer.ram_size);

    xemu_mv_resume_vm(was_running);

    printf("Virtual dump loaded: %s (%u bytes, vaddr 0x%08X..0x%08X)\n",
           filename, load_size, vstart, vstart + load_size);

    return true;
}

bool xemu_memory_viewer_load_dump_file_virt(const char *filename)
{
    return xemu_memory_viewer_load_dump_file_virt_range(filename,
                                                        XEMU_VIRT_DUMP_START,
                                                        XEMU_VIRT_DUMP_SIZE);
}
