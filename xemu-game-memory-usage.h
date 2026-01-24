/*
 * XEMU Game Memory Usage Tracker
 *
 * This module tracks memory usage patterns for running Xbox games
 * to provide insights into heap, stack, and executable memory usage.
 *
 * Copyright (c) 2025 XEMU Project
 */

#ifndef XEMU_GAME_MEMORY_USAGE_H
#define XEMU_GAME_MEMORY_USAGE_H

#include "qemu/osdep.h"

/* Xbox memory layout constants */
#define XBOX_KERNEL_VIRTUAL_BASE    0x80000000  // Kernel space starts here
#define XBOX_USER_VIRTUAL_BASE      0x00010000  // User space starts here  
#define XBOX_GAME_BASE_TYPICAL      0x00010000  // Typical game load address
#define XBOX_HEAP_START_TYPICAL     0x00100000  // Typical heap start
#define XBOX_STACK_SIZE_TYPICAL     0x00020000  // Typical stack size (128KB)

/* Memory region types */
typedef enum {
    XBOX_MEM_EXECUTABLE,    // .text sections
    XBOX_MEM_DATA,          // .data/.bss sections
    XBOX_MEM_HEAP,          // Dynamic allocations
    XBOX_MEM_STACK,         // Thread stacks
    XBOX_MEM_GPU_BUFFERS,   // Graphics/vertex buffers
    XBOX_MEM_AUDIO_BUFFERS, // Audio buffers
    XBOX_MEM_FREE,          // Unallocated memory
    XBOX_MEM_UNKNOWN        // Unclassified
} XboxMemoryType;

/* Memory usage statistics */
typedef struct {
    uint64_t total_memory;      // Total Xbox RAM (64MB)
    uint64_t executable_size;   // Size of game executable
    uint64_t heap_used;         // Estimated heap usage
    uint64_t stack_used;        // Estimated stack usage
    uint64_t gpu_buffers;       // GPU buffer usage
    uint64_t audio_buffers;     // Audio buffer usage
    uint64_t free_memory;       // Available memory
    uint64_t fragmented;        // Fragmented/unusable memory
    
    // Peak usage tracking
    uint64_t peak_heap;
    uint64_t peak_stack;
    uint64_t peak_gpu_buffers;
    
    // Memory regions
    uint32_t executable_base;   // Base address of main executable
    uint32_t heap_base;         // Estimated heap start
    uint32_t stack_base;        // Main thread stack base
} XboxGameMemoryUsage;

/*
 * Initialize the game memory usage tracker
 * Returns true on success, false on error
 */
bool xemu_game_memory_usage_init(void);

/*
 * Update memory usage statistics by analyzing current memory state
 * This should be called periodically to track usage patterns
 */
void xemu_game_memory_usage_update(void);

/*
 * Get current memory usage statistics
 * 
 * @stats: Pointer to structure to receive statistics
 * Returns true on success, false if not initialized
 */
bool xemu_game_memory_usage_get_stats(XboxGameMemoryUsage *stats);

/*
 * Analyze memory to determine region types
 * Uses heuristics to identify executable code, heap, stack, etc.
 */
void xemu_game_memory_usage_analyze_regions(void);

/*
 * Get formatted memory usage report as string
 * 
 * @buffer: Buffer to write report to
 * @size: Size of buffer
 * Returns number of bytes written
 */
int xemu_game_memory_usage_get_report(char *buffer, size_t size);

/*
 * Reset peak usage tracking
 */
void xemu_game_memory_usage_reset_peaks(void);

/*
 * Check if memory address appears to be executable code
 * Uses simple heuristics like checking for x86 instruction patterns
 */
bool xemu_game_memory_is_executable(uint32_t addr, size_t size);

/*
 * Check if memory region appears to be heap (dynamic allocations)
 * Looks for allocation patterns and heap metadata
 */
bool xemu_game_memory_is_heap(uint32_t addr, size_t size);

/*
 * Check if memory region appears to be stack
 * Looks for stack frame patterns and return addresses
 */
bool xemu_game_memory_is_stack(uint32_t addr, size_t size);

/*
 * Detect GPU-related memory usage (vertex buffers, textures, etc.)
 * Works with the NV2A implementation to track graphics memory
 */
uint64_t xemu_game_memory_get_gpu_usage(void);

/*
 * Detect audio-related memory usage
 * Works with the MCPX APU to track audio buffers
 */
uint64_t xemu_game_memory_get_audio_usage(void);

/*
 * Example UI integration function
 * Call this from your UI code to display memory usage
 */
void xemu_game_memory_usage_ui_display(void);

#endif /* XEMU_GAME_MEMORY_USAGE_H */