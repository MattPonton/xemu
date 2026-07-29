/*
 * XEMU Memory Viewer Integration Example
 *
 * This file shows how to integrate the memory viewer into XEMU's UI system
 * and make it accessible via hotkeys or menu items.
 *
 * Copyright (c) 2025 Example
 */

#include "xemu-memory-viewer.h"
#include "ui/xemu.c"
#include "system/runstate.h"

// Global state for memory viewer UI
static struct {
    bool initialized;
    bool window_open;
    uint32_t current_address;
    uint32_t bytes_per_row;
    uint32_t visible_rows;
    char search_pattern[256];
    uint32_t last_search_addr;
} g_memory_viewer_ui = {
    .initialized = false,
    .window_open = false,
    .current_address = 0x00000000,
    .bytes_per_row = 16,
    .visible_rows = 32,
    .search_pattern = "",
    .last_search_addr = 0xFFFFFFFF
};

/*
 * Initialize memory viewer UI - call this during XEMU startup
 */
void xemu_memory_viewer_ui_init(void)
{
    if (xemu_memory_viewer_init()) {
        g_memory_viewer_ui.initialized = true;
        printf("Memory Viewer UI initialized successfully\n");
    } else {
        printf("Failed to initialize Memory Viewer UI\n");
    }
}

/*
 * Toggle memory viewer window visibility
 */
void xemu_memory_viewer_toggle(void)
{
    if (!g_memory_viewer_ui.initialized) {
        xemu_memory_viewer_ui_init();
        if (!g_memory_viewer_ui.initialized) {
            return;
        }
    }
    
    g_memory_viewer_ui.window_open = !g_memory_viewer_ui.window_open;
    printf("Memory Viewer window %s\n", 
           g_memory_viewer_ui.window_open ? "opened" : "closed");
}

/*
 * Handle memory viewer hotkeys - add this to your key handler
 */
bool xemu_memory_viewer_handle_key(SDL_Scancode scancode, bool ctrl, bool alt)
{
    if (!ctrl || !alt) return false;
    
    switch (scancode) {
    case SDL_SCANCODE_D:
        // Ctrl+Alt+D: Quick dump at current address
        if (g_memory_viewer_ui.initialized) {
            xemu_memory_viewer_dump(g_memory_viewer_ui.current_address, 256);
        }
        return true;
        
    case SDL_SCANCODE_S:
        // Ctrl+Alt+S: Save memory dump to file
        if (g_memory_viewer_ui.initialized) {
            xemu_memory_viewer_save_dump();
        }
        return true;
        
    case SDL_SCANCODE_L:
        // Ctrl+Alt+L: Load memory dump from file
        if (g_memory_viewer_ui.initialized) {
            xemu_memory_viewer_load_dump();
        }
        return true;
        
    default:
        return false;
    }
}

/*
 * Save current memory view to a file
 */
void xemu_memory_viewer_save_dump(void)
{
    if (!g_memory_viewer_ui.initialized) {
        printf("Memory viewer not initialized\n");
        return;
    }
    
    // Generate filename with timestamp
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char filename[256];
    strftime(filename, sizeof(filename), "xemu_memory_dump_%Y%m%d_%H%M%S.bin", tm_info);
    
    // Get full memory region
    uint64_t ram_size;
    void *ram_ptr;
    if (!xemu_memory_viewer_get_info(&ram_size, &ram_ptr)) {
        printf("Failed to get memory info\n");
        return;
    }
    
    // Save to file
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        printf("Failed to create dump file: %s\n", filename);
        return;
    }
    
    size_t written = fwrite(ram_ptr, 1, ram_size, fp);
    fclose(fp);
    
    if (written == ram_size) {
        printf("Memory dump saved: %s (%" PRIu64 " bytes)\n", filename, ram_size);
    } else {
        printf("Failed to write complete dump (wrote %zu/%" PRIu64 " bytes)\n", 
               written, ram_size);
    }
}

/*
 * Load memory dump from file and apply to Xbox RAM
 */
void xemu_memory_viewer_load_dump(void)
{
    if (!g_memory_viewer_ui.initialized) {
        printf("Memory viewer not initialized\n");
        return;
    }
    
    printf("Please specify the path to the memory dump file to load:\n");
    printf("(This function would normally use a file dialog in a full UI)\n");
    
    // For this example, we'll load a file with a hardcoded name
    // In a real implementation, you'd use a file dialog
    const char *dump_filename = "xemu_memory_dump.bin";
    
    // Get expected RAM size
    uint64_t ram_size;
    void *ram_ptr;
    if (!xemu_memory_viewer_get_info(&ram_size, &ram_ptr)) {
        printf("Failed to get memory info\n");
        return;
    }
    
    // Open file
    FILE *fp = fopen(dump_filename, "rb");
    if (!fp) {
        printf("Failed to open dump file: %s\n", dump_filename);
        printf("Make sure the file exists or modify the filename in the code\n");
        return;
    }
    
    // Get file size
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    if (file_size < 0) {
        printf("Failed to get file size for: %s\n", dump_filename);
        fclose(fp);
        return;
    }
    
    // Validate file size
    if ((uint64_t)file_size > ram_size) {
        printf("Warning: Dump file (%ld bytes) is larger than Xbox RAM (%" PRIu64 " bytes)\n", 
               file_size, ram_size);
        printf("Only the first %" PRIu64 " bytes will be loaded\n", ram_size);
        file_size = ram_size;
    } else if ((uint64_t)file_size < ram_size) {
        printf("Warning: Dump file (%ld bytes) is smaller than Xbox RAM (%" PRIu64 " bytes)\n", 
               file_size, ram_size);
        printf("Only %ld bytes will be overwritten\n", file_size);
    }
    
    // Read file data
    uint8_t *file_data = (uint8_t*)malloc(file_size);
    if (!file_data) {
        printf("Failed to allocate memory for dump file\n");
        fclose(fp);
        return;
    }
    
    size_t bytes_read = fread(file_data, 1, file_size, fp);
    fclose(fp);
    
    if (bytes_read != (size_t)file_size) {
        printf("Failed to read complete dump file (read %zu/%ld bytes)\n", 
               bytes_read, file_size);
        free(file_data);
        return;
    }
    
    // Apply dump to Xbox memory
    bool success = xemu_memory_viewer_write(0x00000000, file_data, bytes_read);
    
    if (success) {
        printf("Memory dump applied successfully: %s (%zu bytes loaded)\n", 
               dump_filename, bytes_read);
        printf("Xbox memory has been overwritten with dump data\n");
    } else {
        printf("Failed to write dump data to Xbox memory\n");
    }
    
    free(file_data);
}

/*
 * Load memory dump from file and apply to Xbox RAM (safer version)
 */
void xemu_memory_viewer_load_dump_safe_example(void)
{
    if (!g_memory_viewer_ui.initialized) {
        printf("Memory viewer not initialized\n");
        return;
    }
    
    printf("Loading memory dump with safety features enabled...\n");
    
    // For this example, we'll load a file with a hardcoded name
    // In a real implementation, you'd use a file dialog
    const char *dump_filename = "xemu_memory_dump.bin";
    
    // Use the safe loading function with graphics reset
    bool success = xemu_memory_viewer_load_dump_file_safe(dump_filename, true);
    
    if (success) {
        printf("Memory dump safely applied: %s\n", dump_filename);
        printf("Emulator was paused/resumed automatically for safety\n");
    } else {
        printf("Failed to safely load memory dump from: %s\n", dump_filename);
        printf("Make sure the file exists and the emulator is in a stable state\n");
    }
}

/*
 * Render memory viewer ImGui window (if using ImGui)
 */
#ifdef IMGUI_VERSION
void xemu_memory_viewer_render_imgui(void)
{
    if (!g_memory_viewer_ui.window_open || !g_memory_viewer_ui.initialized) {
        return;
    }
    
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Xbox Memory Viewer", &g_memory_viewer_ui.window_open)) {
        
        // Address input
        ImGui::Text("Address:");
        ImGui::SameLine();
        ImGui::InputScalar("##addr", ImGuiDataType_U32, &g_memory_viewer_ui.current_address, 
                          NULL, NULL, "%08X", ImGuiInputTextFlags_CharsHexadecimal);
        
        ImGui::SameLine();
        if (ImGui::Button("Go")) {
            // Address updated, will refresh display below
        }
        
        // Navigation buttons
        ImGui::SameLine();
        if (ImGui::Button("- Page")) {
            uint32_t page_size = g_memory_viewer_ui.bytes_per_row * g_memory_viewer_ui.visible_rows;
            if (g_memory_viewer_ui.current_address >= page_size) {
                g_memory_viewer_ui.current_address -= page_size;
            } else {
                g_memory_viewer_ui.current_address = 0;
            }
        }
        
        ImGui::SameLine();
        if (ImGui::Button("+ Page")) {
            uint32_t page_size = g_memory_viewer_ui.bytes_per_row * g_memory_viewer_ui.visible_rows;
            g_memory_viewer_ui.current_address += page_size;
        }
        
        // Dump controls
        ImGui::SameLine();
        if (ImGui::Button("Save Dump")) {
            xemu_memory_viewer_save_dump();
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Load Dump (Safe)")) {
            // For the example, we'll use the safe loading with graphics reset
            const char *dump_filename = "xemu_memory_dump.bin";
            if (xemu_memory_viewer_load_dump_file_safe(dump_filename, true)) {
                printf("Safe load completed successfully\n");
            } else {
                printf("Safe load failed - try using the file dialog in the full UI\n");
            }
        }
        
        // Search functionality
        ImGui::Separator();
        ImGui::Text("Search:");
        ImGui::SameLine();
        ImGui::InputText("##search", g_memory_viewer_ui.search_pattern, 
                        sizeof(g_memory_viewer_ui.search_pattern));
        
        ImGui::SameLine();
        if (ImGui::Button("Find")) {
            if (strlen(g_memory_viewer_ui.search_pattern) > 0) {
                uint32_t start_addr = (g_memory_viewer_ui.last_search_addr != 0xFFFFFFFF) ?
                                     g_memory_viewer_ui.last_search_addr + 1 : 
                                     g_memory_viewer_ui.current_address;
                
                uint32_t found = 0xFFFFFFFF;
                bool ok = xemu_memory_viewer_search(start_addr,
                                                    (const uint8_t *)g_memory_viewer_ui.search_pattern,
                                                    strlen(g_memory_viewer_ui.search_pattern),
                                                    &found);

                if (ok) {
                    g_memory_viewer_ui.current_address = found;
                    g_memory_viewer_ui.last_search_addr = found;
                    ImGui::SetScrollHereY(0.5f);  // Center on found result
                } else {
                    // Reset search and try from beginning
                    found = 0xFFFFFFFF;
                    ok = xemu_memory_viewer_search(0,
                                                (const uint8_t *)g_memory_viewer_ui.search_pattern,
                                                strlen(g_memory_viewer_ui.search_pattern),
                                                &found);

                    if (ok) {
                        g_memory_viewer_ui.current_address = found;
                        g_memory_viewer_ui.last_search_addr = found;
                    } else {
                        g_memory_viewer_ui.last_search_addr = 0xFFFFFFFF;
                    }
                }
            }
        }
        
        // Memory display
        ImGui::Separator();
        ImGui::BeginChild("MemoryView", ImVec2(0, 0), true);
        
        // Read memory data
        uint32_t display_size = g_memory_viewer_ui.bytes_per_row * g_memory_viewer_ui.visible_rows;
        uint8_t *memory_data = (uint8_t*)malloc(display_size);
        
        if (xemu_memory_viewer_read(g_memory_viewer_ui.current_address, memory_data, display_size)) {
            // Display hex dump
            for (uint32_t row = 0; row < g_memory_viewer_ui.visible_rows; row++) {
                uint32_t addr = g_memory_viewer_ui.current_address + (row * g_memory_viewer_ui.bytes_per_row);
                ImGui::Text("%08X:", addr);
                
                // Hex bytes
                ImGui::SameLine();
                for (uint32_t col = 0; col < g_memory_viewer_ui.bytes_per_row; col++) {
                    uint32_t offset = row * g_memory_viewer_ui.bytes_per_row + col;
                    if (offset < display_size) {
                        ImGui::SameLine();
                        ImGui::Text("%02X", memory_data[offset]);
                    }
                }
                
                // ASCII representation
                ImGui::SameLine();
                ImGui::Text(" |");
                ImGui::SameLine();
                for (uint32_t col = 0; col < g_memory_viewer_ui.bytes_per_row; col++) {
                    uint32_t offset = row * g_memory_viewer_ui.bytes_per_row + col;
                    if (offset < display_size) {
                        char c = memory_data[offset];
                        ImGui::SameLine();
                        ImGui::Text("%c", (c >= 32 && c <= 126) ? c : '.');
                    }
                }
                ImGui::SameLine();
                ImGui::Text("|");
            }
        } else {
            ImGui::Text("Failed to read memory at address 0x%08X", g_memory_viewer_ui.current_address);
        }
        
        free(memory_data);
        ImGui::EndChild();
        
        // Status bar
        uint64_t total_ram;
        if (xemu_memory_viewer_get_info(&total_ram, NULL)) {
            ImGui::Text("Xbox RAM: %" PRIu64 " MB | Current: 0x%08X", 
                       total_ram / (1024 * 1024), g_memory_viewer_ui.current_address);
        }
    }
    ImGui::End();
}
#endif

/*
 * Add this to your main key handler in xemu.c
 */
/*
// In handle_keydown function, add after existing hotkey checks:

case SDL_SCANCODE_M:
    if (xemu_memory_viewer_handle_key(ev->key.keysym.scancode, 
                                     gui_key_modifier_pressed, 
                                     gui_key_modifier_pressed)) {
        gui_keysym = 1;
    }
    break;

case SDL_SCANCODE_D:
case SDL_SCANCODE_S:
    if (xemu_memory_viewer_handle_key(ev->key.keysym.scancode, 
                                     gui_key_modifier_pressed, 
                                     gui_key_modifier_pressed)) {
        gui_keysym = 1;
    }
    break;
*/

/*
 * Add this to your main rendering loop (if using ImGui)
 */
/*
// In your main render function, add:
#ifdef IMGUI_VERSION
    xemu_memory_viewer_render_imgui();
#endif
*/
