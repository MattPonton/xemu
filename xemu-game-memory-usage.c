/*
 * XEMU Game Memory Usage Tracker Implementation
 *
 * This module analyzes Xbox memory usage patterns to provide insights
 * into how games are using the unified 64MB RAM.
 */

#include "xemu-game-memory-usage.h"
#include "xemu-memory-viewer.h"
#include "hw/xbox/nv2a/nv2a.h"
#include <time.h>

static struct {
    bool initialized;
    XboxGameMemoryUsage current_stats;
    XboxGameMemoryUsage peak_stats;
    time_t last_update;
    uint32_t update_count;
} g_memory_tracker = { 0 };

/* Simple x86 instruction detection for executable code */
static bool is_x86_instruction_start(uint8_t *data, size_t offset, size_t max_size) {
    if (offset >= max_size) return false;
    
    uint8_t byte = data[offset];
    
    // Common x86 instruction prefixes and opcodes
    switch (byte) {
        case 0x55: // PUSH EBP (common function prologue)
        case 0x8B: // MOV instructions
        case 0x89: // MOV instructions
        case 0xE8: // CALL near
        case 0xE9: // JMP near
        case 0xC3: // RET
        case 0x83: // ADD/SUB with immediate
        case 0x74: case 0x75: // JZ/JNZ
        case 0x90: // NOP
            return true;
        default:
            return false;
    }
}

bool xemu_game_memory_usage_init(void) {
    if (!xemu_memory_viewer_init()) {
        printf("Failed to initialize memory viewer for game usage tracking\n");
        return false;
    }
    
    memset(&g_memory_tracker, 0, sizeof(g_memory_tracker));
    g_memory_tracker.initialized = true;
    g_memory_tracker.last_update = time(NULL);
    
    // Get total memory size
    uint64_t total_size;
    if (xemu_memory_viewer_get_info(&total_size, NULL)) {
        g_memory_tracker.current_stats.total_memory = total_size;
        g_memory_tracker.peak_stats.total_memory = total_size;
    }
    
    printf("Game memory usage tracker initialized (Total RAM: %llu MB)\n", 
           g_memory_tracker.current_stats.total_memory / (1024 * 1024));
    
    return true;
}

void xemu_game_memory_usage_analyze_regions(void) {
    if (!g_memory_tracker.initialized) return;
    
    uint64_t total_size;
    void *ram_ptr;
    if (!xemu_memory_viewer_get_info(&total_size, &ram_ptr)) {
        return;
    }
    
    uint8_t *memory = (uint8_t*)ram_ptr;
    XboxGameMemoryUsage *stats = &g_memory_tracker.current_stats;
    
    // Reset counters
    stats->executable_size = 0;
    stats->heap_used = 0;
    stats->stack_used = 0;
    stats->free_memory = 0;
    
    // Scan memory in chunks to classify regions
    const size_t chunk_size = 4096; // 4KB chunks
    const size_t total_chunks = total_size / chunk_size;
    
    uint32_t consecutive_executable = 0;
    uint32_t consecutive_zeros = 0;
    bool found_executable_base = false;
    
    for (size_t chunk = 0; chunk < total_chunks; chunk++) {
        uint32_t addr = chunk * chunk_size;
        uint8_t *chunk_data = memory + addr;
        
        // Skip first 64KB (typically reserved/kernel)
        if (addr < 0x10000) continue;
        
        // Count zero bytes (likely free memory)
        size_t zero_count = 0;
        for (size_t i = 0; i < chunk_size; i++) {
            if (chunk_data[i] == 0) zero_count++;
        }
        
        if (zero_count > chunk_size * 0.9) {
            // Mostly zeros - likely free memory
            stats->free_memory += chunk_size;
            consecutive_zeros++;
            consecutive_executable = 0;
        } else {
            consecutive_zeros = 0;
            
            // Check for executable code patterns
            size_t instruction_count = 0;
            for (size_t i = 0; i < chunk_size - 16; i += 4) {
                if (is_x86_instruction_start(chunk_data, i, chunk_size)) {
                    instruction_count++;
                }
            }
            
            if (instruction_count > 10) {
                // Looks like executable code
                stats->executable_size += chunk_size;
                consecutive_executable++;
                
                if (!found_executable_base) {
                    stats->executable_base = addr;
                    found_executable_base = true;
                }
            } else {
                consecutive_executable = 0;
                
                // Check if this looks like heap (allocated data)
                bool has_pointers = false;
                for (size_t i = 0; i < chunk_size - 4; i += 4) {
                    uint32_t value = *(uint32_t*)(chunk_data + i);
                    // Check if value looks like a valid Xbox address
                    if ((value >= 0x10000 && value < total_size) || 
                        (value >= 0x80000000 && value < 0x80000000 + total_size)) {
                        has_pointers = true;
                        break;
                    }
                }
                
                if (has_pointers) {
                    stats->heap_used += chunk_size;
                } else {
                    // Could be stack or other data
                    stats->stack_used += chunk_size;
                }
            }
        }
    }
    
    // Estimate heap base (typically after executable)
    if (stats->executable_base > 0) {
        stats->heap_base = stats->executable_base + stats->executable_size;
        if (stats->heap_base < 0x100000) {
            stats->heap_base = 0x100000; // Common heap start
        }
    }
    
    // Get GPU and audio usage
    stats->gpu_buffers = xemu_game_memory_get_gpu_usage();
    stats->audio_buffers = xemu_game_memory_get_audio_usage();
    
    // Update peaks
    XboxGameMemoryUsage *peaks = &g_memory_tracker.peak_stats;
    if (stats->heap_used > peaks->peak_heap) peaks->peak_heap = stats->heap_used;
    if (stats->stack_used > peaks->peak_stack) peaks->peak_stack = stats->stack_used;
    if (stats->gpu_buffers > peaks->peak_gpu_buffers) peaks->peak_gpu_buffers = stats->gpu_buffers;
}

uint64_t xemu_game_memory_get_gpu_usage(void) {
    // This would need to interface with the NV2A implementation
    // to get actual GPU buffer usage. For now, return estimated value.
    
    // Look for patterns in memory that suggest GPU buffers
    uint64_t estimated_gpu = 0;
    
    // Search for vertex buffer patterns (aligned data, repetitive structures)
    uint32_t search_patterns[] = {
        0x40000000, // Common vertex data patterns
        0x3F800000, // 1.0f in IEEE 754
        0x00000000, // Zero (common in padding)
    };
    
    for (size_t i = 0; i < sizeof(search_patterns)/sizeof(search_patterns[0]); i++) {
        uint32_t addr = 0xFFFFFFFF;
        if (xemu_memory_viewer_search(0x00010000,
                                    (const uint8_t *)&search_patterns[i],
                                    sizeof(uint32_t),
                                    &addr)) {
            estimated_gpu += 65536; // Rough estimate per buffer found
        }
    }
    
    return estimated_gpu;
}

uint64_t xemu_game_memory_get_audio_usage(void) {
    // This would interface with MCPX APU to get audio buffer usage
    // For now, return rough estimate based on audio data patterns
    
    uint64_t estimated_audio = 0;
    
    // Look for audio buffer patterns (PCM data, ADPCM headers, etc.)
    // Audio data often has specific byte patterns
    static const struct {
        const uint8_t *sig;
        size_t len;
    } audio_signatures[] = {
        { (const uint8_t *)"RIFF", 4 },
        { (const uint8_t *)"OggS", 4 },
        { (const uint8_t *)"\x00\x01\x00\x00", 4 },
    };    

    for (size_t i = 0; i < sizeof(audio_signatures)/sizeof(audio_signatures[0]); i++) {
        uint32_t addr = 0xFFFFFFFF;
        if (xemu_memory_viewer_search(0x00010000,
                                    audio_signatures[i].sig,
                                    audio_signatures[i].len,
                                    &addr)) {
            estimated_audio += 32768; // Rough estimate per buffer found
        }
    }
    
    return estimated_audio;
}

void xemu_game_memory_usage_update(void) {
    if (!g_memory_tracker.initialized) return;
    
    time_t now = time(NULL);
    // Only update every few seconds to avoid performance impact
    if (now - g_memory_tracker.last_update < 2) return;
    
    xemu_game_memory_usage_analyze_regions();
    g_memory_tracker.last_update = now;
    g_memory_tracker.update_count++;
}

bool xemu_game_memory_usage_get_stats(XboxGameMemoryUsage *stats) {
    if (!g_memory_tracker.initialized || !stats) return false;
    
    *stats = g_memory_tracker.current_stats;
    return true;
}

int xemu_game_memory_usage_get_report(char *buffer, size_t size) {
    if (!g_memory_tracker.initialized || !buffer) return 0;
    
    XboxGameMemoryUsage *stats = &g_memory_tracker.current_stats;
    XboxGameMemoryUsage *peaks = &g_memory_tracker.peak_stats;
    
    return snprintf(buffer, size,
        "=== Xbox Game Memory Usage Report ===\n"
        "Total RAM: %.1f MB\n"
        "Executable: %.1f MB (base: 0x%08X)\n"
        "Heap Used: %.1f MB (peak: %.1f MB)\n"
        "Stack Used: %.1f MB (peak: %.1f MB)\n"
        "GPU Buffers: %.1f MB (peak: %.1f MB)\n"
        "Audio Buffers: %.1f MB\n"
        "Free Memory: %.1f MB\n"
        "Memory Utilization: %.1f%%\n"
        "Updates: %u\n",
        stats->total_memory / (1024.0 * 1024.0),
        stats->executable_size / (1024.0 * 1024.0),
        stats->executable_base,
        stats->heap_used / (1024.0 * 1024.0),
        peaks->peak_heap / (1024.0 * 1024.0),
        stats->stack_used / (1024.0 * 1024.0),
        peaks->peak_stack / (1024.0 * 1024.0),
        stats->gpu_buffers / (1024.0 * 1024.0),
        peaks->peak_gpu_buffers / (1024.0 * 1024.0),
        stats->audio_buffers / (1024.0 * 1024.0),
        stats->free_memory / (1024.0 * 1024.0),
        ((stats->total_memory - stats->free_memory) * 100.0) / stats->total_memory,
        g_memory_tracker.update_count
    );
}

void xemu_game_memory_usage_reset_peaks(void) {
    memset(&g_memory_tracker.peak_stats, 0, sizeof(g_memory_tracker.peak_stats));
    g_memory_tracker.peak_stats.total_memory = g_memory_tracker.current_stats.total_memory;
}

bool xemu_game_memory_is_executable(uint32_t addr, size_t size) {
    uint8_t buffer[256];
    size_t check_size = (size > sizeof(buffer)) ? sizeof(buffer) : size;
    
    if (!xemu_memory_viewer_read(addr, buffer, check_size)) {
        return false;
    }
    
    // Count potential x86 instructions
    size_t instruction_count = 0;
    for (size_t i = 0; i < check_size - 4; i++) {
        if (is_x86_instruction_start(buffer, i, check_size)) {
            instruction_count++;
        }
    }
    
    // If more than 20% look like instructions, consider it executable
    return (instruction_count * 5) > check_size;
}

bool xemu_game_memory_is_heap(uint32_t addr, size_t size) {
    // Simple heap detection: look for pointer-like values
    uint8_t buffer[256];
    size_t check_size = (size > sizeof(buffer)) ? sizeof(buffer) : size;
    
    if (!xemu_memory_viewer_read(addr, buffer, check_size)) {
        return false;
    }
    
    size_t pointer_count = 0;
    for (size_t i = 0; i < check_size - 4; i += 4) {
        uint32_t value = *(uint32_t*)(buffer + i);
        // Check if value looks like a valid Xbox address
        if ((value >= 0x10000 && value < 0x04000000) || 
            (value >= 0x80000000 && value < 0x84000000)) {
            pointer_count++;
        }
    }
    
    // If more than 10% look like pointers, consider it heap
    return (pointer_count * 40) > (check_size / 4);
}

bool xemu_game_memory_is_stack(uint32_t addr, size_t size) {
    // Stack detection: look for return addresses and frame pointers
    uint8_t buffer[256];
    size_t check_size = (size > sizeof(buffer)) ? sizeof(buffer) : size;
    
    if (!xemu_memory_viewer_read(addr, buffer, check_size)) {
        return false;
    }
    
    // Look for patterns typical of stack frames
    size_t frame_patterns = 0;
    for (size_t i = 0; i < check_size - 8; i += 4) {
        uint32_t value1 = *(uint32_t*)(buffer + i);
        uint32_t value2 = *(uint32_t*)(buffer + i + 4);
        
        // Look for saved EBP followed by return address pattern
        if ((value1 >= addr && value1 < addr + 0x10000) && 
            (value2 >= 0x10000 && value2 < 0x04000000)) {
            frame_patterns++;
        }
    }
    
    return frame_patterns > 0;
}

void xemu_game_memory_usage_ui_display(void) {
    if (!g_memory_tracker.initialized) {
        printf("Game memory usage tracker not initialized\n");
        return;
    }
    
    char report[2048];
    int len = xemu_game_memory_usage_get_report(report, sizeof(report));
    if (len > 0) {
        printf("%s\n", report);
    }
}