/*
 * XEMU Memory Viewer - Header file
 *
 * This header provides the interface for accessing Xbox's UMA (64MB)
 * for implementing memory viewer and debugging features.
 *
 * Copyright (c) 2025 Example
 */

#ifndef XEMU_MEMORY_VIEWER_H
#define XEMU_MEMORY_VIEWER_H

#include "qemu/osdep.h"

/*
 * Initialize the memory viewer to access Xbox RAM
 * Returns true on success, false if Xbox RAM region couldn't be found
 */
bool xemu_memory_viewer_init(void);

/*
 * Read a block of memory from Xbox RAM
 * 
 * @xbox_addr: Address within Xbox's 64MB RAM space (0x00000000 - 0x03FFFFFF for 64MB)
 * @buffer: Destination buffer to copy data to
 * @size: Number of bytes to read
 * 
 * Returns true on success, false on error (out of bounds, not initialized)
 */
bool xemu_memory_viewer_read(uint32_t xbox_addr, void *buffer, size_t size);
bool xemu_memory_viewer_read_virt(uint32_t vaddr, void *buffer, size_t size);

/*
 * Write a block of memory to Xbox RAM
 * 
 * @xbox_addr: Address within Xbox's 64MB RAM space
 * @buffer: Source buffer to copy data from
 * @size: Number of bytes to write
 * 
 * Returns true on success, false on error
 * Note: This will mark the memory region as dirty for graphics updates
 */
bool xemu_memory_viewer_write(uint32_t xbox_addr, const void *buffer, size_t size);
bool xemu_memory_viewer_write_virt(uint32_t vaddr, const void *data, size_t size);

/*
 * Get a direct pointer to Xbox memory for performance-critical operations
 * 
 * @xbox_addr: Address within Xbox RAM
 * @size: Size of the region you plan to access (for bounds checking)
 * 
 * Returns pointer to Xbox memory, or NULL on error
 * WARNING: Use with extreme caution! Direct memory access bypasses safety checks.
 */
void *xemu_memory_viewer_get_ptr(uint32_t xbox_addr, size_t size);

/*
 * Get information about Xbox memory region
 * 
 * @size: Optional pointer to receive the total RAM size
 * @ptr: Optional pointer to receive the base RAM pointer
 * 
 * Returns true if memory viewer is initialized, false otherwise
 */
bool xemu_memory_viewer_get_info(uint64_t *size, void **ptr);

/*
 * Search for a pattern in Xbox memory
 * 
 * @pattern: The byte pattern to search for
 * @pattern_size: Size of the pattern in bytes
 * @start_addr: Xbox address to start searching from
 * 
 * Returns the Xbox address where pattern was found, or 0xFFFFFFFF if not found
 */
bool xemu_memory_viewer_search(uint32_t start_addr,
                               const uint8_t *pattern,
                               size_t pattern_len,
                               uint32_t *out_found_addr);
                               
bool xemu_memory_viewer_search_virt(uint32_t start_vaddr,
                                    uint32_t bytes_to_scan,
                                    const uint8_t *pattern,
                                    size_t pattern_len,
                                    uint32_t *out_found_vaddr);

size_t xemu_memory_viewer_search_virt_all(uint32_t start_vaddr,
                                        uint32_t bytes_to_scan,
                                        const uint8_t *pattern,
                                        size_t pattern_len,
                                        uint32_t *out_found_vaddrs,
                                        size_t max_found);
/*
 * Dump memory region to console in hex format (for debugging)
 * 
 * @xbox_addr: Starting Xbox address to dump
 * @size: Number of bytes to dump
 */
void xemu_memory_viewer_dump(uint32_t xbox_addr, size_t size);

/*
 * Save Xbox memory to a dump file with timestamp
 * Saves the entire Xbox RAM to a binary file for later restoration
 */
void xemu_memory_viewer_save_dump_file(void);

/*
 * Save Xbox memory to a dump file excluding GPU-sensitive regions
 * Creates a safer dump that can be restored without causing GPU crashes
 */
void xemu_memory_viewer_save_dump_file_safe(void);

/*
 * Load Xbox memory from a dump file
 * 
 * @filename: Path to the dump file to load
 * 
 * Returns true on success, false on error
 * WARNING: This will overwrite Xbox memory with the dump data!
 */
bool xemu_memory_viewer_load_dump_file(const char *filename);

/*
 * Load Xbox memory from a dump file with enhanced safety
 * 
 * @filename: Path to the dump file to load
 * @reset_graphics: If true, attempts to reset graphics state for safer restoration
 * 
 * Returns true on success, false on error
 * This version pauses the emulator during load and optionally resets graphics state
 */
bool xemu_memory_viewer_load_dump_file_safe(const char *filename, bool reset_graphics);

/*
 * Save/Load a VIRTUAL memory dump (user VA space window)
 * Default range is 64MB starting at 0x00010000 (end exclusive = 0x04010000).
 */
void xemu_memory_viewer_save_dump_file_virt(void);
bool xemu_memory_viewer_load_dump_file_virt(const char *filename);

/* Optional range-based variants (handy if you later want different windows) */
bool xemu_memory_viewer_save_dump_file_virt_range(const char *filename,
                                                  uint32_t vstart,
                                                  uint32_t vsize);
bool xemu_memory_viewer_load_dump_file_virt_range(const char *filename,
                                                  uint32_t vstart,
                                                  uint32_t vsize);

/*
 * Reset NV2A GPU state to prevent crashes and corruption
 * 
 * This function clears GPU command buffers, PFIFO areas, and other critical
 * regions that can cause assertion failures and crashes when corrupted.
 * Safe to call manually for troubleshooting GPU issues.
 */
void xemu_memory_viewer_reset_nv2a_state(void);

/*
 * Load partial Xbox memory dump to specific address
 * 
 * @filename: Path to the dump file to load
 * @target_addr: Xbox address where to load the dump data
 * @max_size: Maximum number of bytes to load
 * 
 * Returns true on success, false on error
 * Safer alternative that loads dump to specific memory region instead of complete overwrite
 */
bool xemu_memory_viewer_load_dump_partial(const char *filename, uint32_t target_addr, uint32_t max_size);

/*
 * Example function demonstrating usage
 */
void xemu_memory_viewer_example(void);

/* Common Xbox memory layout constants */
#define XBOX_RAM_SIZE_64MB    (64 * 1024 * 1024)
#define XBOX_RAM_SIZE_128MB   (128 * 1024 * 1024)  // Some modified Xboxes

/* Xbox memory regions (typical layout) */
#define XBOX_KERNEL_BASE      0x80000000    // Kernel virtual base
#define XBOX_USER_BASE        0x00010000    // User virtual base
#define XBOX_PHYSICAL_BASE    0x00000000    // Physical memory base

/* Utility macros */
#define XBOX_ADDR_VALID(addr, size, max_size) \
    ((addr) < (max_size) && (addr) + (size) <= (max_size))

#define XBOX_ADDR_ALIGN_4K(addr) \
    ((addr) & ~0xFFF)

#define XBOX_ADDR_ALIGN_PAGE(addr) \
    XBOX_ADDR_ALIGN_4K(addr)

#endif /* XEMU_MEMORY_VIEWER_H */
