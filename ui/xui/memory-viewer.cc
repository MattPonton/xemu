//
// xemu User Interface
//
// Copyright (C) 2020-2025 Matt Borgerson
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#include "debug.hh"
#include "common.hh"
#include "font-manager.hh"
#include <vector>
#include <algorithm> // optional, but handy

extern "C" {
#include "xemu-memory-viewer.h"
#include "xemu-game-memory-usage.h"
#include <time.h>
}

MemoryViewerWindow memory_viewer_window;

static constexpr uint32_t kVirtSearchStart = 0x00010000; // good default for Xbox Virtual space
static constexpr uint32_t kVirtSearchBytes = 0x04000000; // scan 64MB forward
static inline uint32_t ClampVirt(uint32_t addr)
{
    return (addr < kVirtSearchStart) ? kVirtSearchStart : addr;
}

MemoryViewerWindow::MemoryViewerWindow()
{
    m_is_open = false;
    m_initialized = false;
    m_current_address = kVirtSearchStart;
    m_bytes_per_row = 16;
    m_visible_rows = 16;
    m_search_pattern[0] = '\0';
    m_search_status[0] = '\0';
    m_search_results_count = 0;
    m_search_result_index = 0;
    m_search_cache_valid = false;
    m_search_cache_pattern[0] = '\0';
    m_search_cache_hex = false;
    m_write_addr_text[0] = '\0';
    m_write_value_text[0] = '\0';
    m_write_status[0] = '\0';
    m_last_search_addr = 0xFFFFFFFF;
    m_hex_search_mode = false;  // Default to ASCII search
    m_memory_usage_initialized = false;
    m_show_usage_tab = false;
    m_show_text_column = true;
    m_text_encoding = MemoryViewerWindow::TextEncoding::Ascii;
}

void MemoryViewerWindow::Initialize()
{
    if (!m_initialized) {
        if (xemu_memory_viewer_init()) {
            m_initialized = true;
            printf("Memory Viewer initialized successfully\n");
        } else {
            printf("Failed to initialize Memory Viewer\n");
        }
    }
}

static void UpdateByteChangeCache(MemoryViewerWindow& w,
                                 uint32_t base_addr,
                                 const uint8_t* data,
                                 uint32_t size)
{
    const float now = (float)ImGui::GetTime();

    // If the view window moved or resized, reset the cache so we don’t flash everything.
    if (!w.m_prev_view_valid || w.m_prev_view_base != base_addr || w.m_prev_view_size != size ||
        w.m_prev_view_bytes.size() != size || w.m_prev_view_changed_at.size() != size)
    {
        w.m_prev_view_bytes.assign(data, data + size);
        w.m_prev_view_changed_at.assign(size, -10000.0f);
        w.m_prev_view_base = base_addr;
        w.m_prev_view_size = size;
        w.m_prev_view_valid = true;
        return;
    }

    // Same base/size: track changes
    for (uint32_t i = 0; i < size; i++) {
        if (w.m_prev_view_bytes[i] != data[i]) {
            w.m_prev_view_bytes[i] = data[i];
            w.m_prev_view_changed_at[i] = now;
        }
    }
}


static bool ParseHexPatternBytes(const char* hex_pattern,
                                std::vector<uint8_t>& out_bytes,
                                char* out_err,
                                size_t out_err_sz)
{
    out_bytes.clear();

    if (!hex_pattern || !hex_pattern[0]) {
        snprintf(out_err, out_err_sz, "Empty pattern");
        return false;
    }

    char temp_pattern[256];
    strncpy(temp_pattern, hex_pattern, sizeof(temp_pattern) - 1);
    temp_pattern[sizeof(temp_pattern) - 1] = '\0';

    char* token = strtok(temp_pattern, " \t\n\r");
    while (token != nullptr) {
        const size_t len = strlen(token);

        if (len == 1) {
            char hex_str[3] = {'0', token[0], '\0'};
            unsigned int byte_val;
            if (sscanf(hex_str, "%x", &byte_val) == 1 && byte_val <= 0xFF) {
                out_bytes.push_back((uint8_t)byte_val);
            } else {
                snprintf(out_err, out_err_sz, "Invalid hex digit: %s", token);
                return false;
            }
        } else if (len == 2) {
            unsigned int byte_val;
            if (sscanf(token, "%x", &byte_val) == 1 && byte_val <= 0xFF) {
                out_bytes.push_back((uint8_t)byte_val);
            } else {
                snprintf(out_err, out_err_sz, "Invalid hex pair: %s", token);
                return false;
            }
        } else if ((len % 2) == 0) {
            for (size_t i = 0; i < len; i += 2) {
                char hex_pair[3] = {token[i], token[i+1], '\0'};
                unsigned int byte_val;
                if (sscanf(hex_pair, "%x", &byte_val) == 1 && byte_val <= 0xFF) {
                    out_bytes.push_back((uint8_t)byte_val);
                } else {
                    snprintf(out_err, out_err_sz, "Invalid hex pair in sequence: %s", hex_pair);
                    return false;
                }
            }
        } else {
            snprintf(out_err, out_err_sz, "Invalid hex token (odd # of chars): %s", token);
            return false;
        }

        token = strtok(nullptr, " \t\n\r");
    }

    if (out_bytes.empty()) {
        snprintf(out_err, out_err_sz, "No valid hex bytes found");
        return false;
    }

    out_err[0] = '\0';
    return true;
}


static bool BuildVirtSearchResults(const uint8_t* pattern,
                                  size_t pattern_len,
                                  uint32_t* out_addrs,
                                  uint32_t max_addrs,
                                  uint32_t* out_count)
{
    *out_count = 0;

    size_t n = xemu_memory_viewer_search_virt_all(kVirtSearchStart,
                                                 kVirtSearchBytes,
                                                 pattern,
                                                 pattern_len,
                                                 out_addrs,
                                                 (size_t)max_addrs);

    *out_count = (uint32_t)n;
    return (n > 0);
}

static void BuildAsciiRow(char* out, size_t out_sz, const uint8_t* row, size_t n)
{
    // out_sz should be >= n+1
    size_t k = 0;
    for (size_t i = 0; i < n && k + 1 < out_sz; i++) {
        uint8_t b = row[i];
        out[k++] = (b >= 32 && b <= 126) ? (char)b : '.';
    }
    out[k] = '\0';
}

static bool DrawHexCell(uint32_t addr,
                        uint8_t value,
                        float changed_at,
                        float now,
                        float flash_seconds)
{
    const ImVec2 cell_sz(22.0f, 18.0f);

    ImGui::PushID((int)addr);

    bool clicked = false;
    ImGui::InvisibleButton("##cell", cell_sz);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        clicked = true;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetItemRectMin();
    ImVec2 p1 = ImGui::GetItemRectMax();

    // Base background (dark square)
    ImU32 bg = IM_COL32(32, 32, 32, 255);
    dl->AddRectFilled(p0, p1, bg, 2.0f);

    // Green flash on change
    if (changed_at >= 0.0f) {
        float t = (now - changed_at) / flash_seconds;
        if (t >= 0.0f && t <= 1.0f) {
            float a = 1.0f - t;
            ImU32 flash = IM_COL32(80, 200, 80, (int)(a * 180.0f));
            dl->AddRectFilled(p0, p1, flash, 2.0f);
        }
    }

    // Hover tint
    if (ImGui::IsItemHovered()) {
        dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, 35), 2.0f);
    }

    // Centered hex text
    char txt[4];
    snprintf(txt, sizeof(txt), "%02X", value);
    ImVec2 ts = ImGui::CalcTextSize(txt);
    ImVec2 tp(p0.x + (cell_sz.x - ts.x) * 0.5f,
              p0.y + (cell_sz.y - ts.y) * 0.5f);
    dl->AddText(tp, IM_COL32(220, 220, 220, 255), txt);

    ImGui::PopID();
    return clicked;
}

void MemoryViewerWindow::Toggle()
{
    if (!m_initialized) {
        Initialize();
    }
    m_is_open = !m_is_open;
}

void MemoryViewerWindow::SaveDump()
{
    if (!m_initialized) {
        printf("Memory viewer not initialized\n");
        return;
    }
    
    /* Saves 64MB window of virtual memory 0x00010000..0x04010000 */
    xemu_memory_viewer_save_dump_file_virt();
}

void MemoryViewerWindow::LoadDump()
{
    if (!m_initialized) {
        printf("Memory viewer not initialized\n");
        return;
    }
    
    // Use noc_file_dialog to select file
    const char *filter_pattern = "Binary files (*.bin)\0*.bin\0All files (*.*)\0*.*\0\0";
    const char *selected_file = noc_file_dialog_open(
        NOC_FILE_DIALOG_OPEN,
        filter_pattern,
        NULL,
        "Select Memory Dump File"
    );
    
    if (!selected_file) {
        return; // User cancelled
    }
    
    if (!xemu_memory_viewer_load_dump_file_virt(selected_file)) {
        printf("Failed to load virtual dump: %s\n", selected_file);
    }
}

uint32_t MemoryViewerWindow::SearchHexPattern(const char* hex_pattern, uint32_t start_addr)
{
    if (!m_initialized) {
        return 0xFFFFFFFF;
    }
    
    // Parse hex pattern (e.g., "00 00 80 3F" or "0000803F")
    std::vector<uint8_t> pattern_bytes;
    char temp_pattern[256];
    strncpy(temp_pattern, hex_pattern, sizeof(temp_pattern) - 1);
    temp_pattern[sizeof(temp_pattern) - 1] = '\0';
    
    // Remove spaces and parse hex pairs
    char* token = strtok(temp_pattern, " \t\n\r");
    while (token != nullptr) {
        // Handle both single hex digits and hex pairs
        if (strlen(token) == 1) {
            // Single digit, treat as 0X
            char hex_str[3] = {'0', token[0], '\0'};
            unsigned int byte_val;
            if (sscanf(hex_str, "%x", &byte_val) == 1 && byte_val <= 0xFF) {
                pattern_bytes.push_back((uint8_t)byte_val);
            } else {
                printf("Invalid hex digit: %s\n", token);
                return 0xFFFFFFFF;
            }
        } else if (strlen(token) == 2) {
            // Hex pair
            unsigned int byte_val;
            if (sscanf(token, "%x", &byte_val) == 1 && byte_val <= 0xFF) {
                pattern_bytes.push_back((uint8_t)byte_val);
            } else {
                printf("Invalid hex pair: %s\n", token);
                return 0xFFFFFFFF;
            }
        } else if (strlen(token) % 2 == 0) {
            // Multiple hex pairs concatenated (e.g., "0000803F")
            for (size_t i = 0; i < strlen(token); i += 2) {
                char hex_pair[3] = {token[i], token[i+1], '\0'};
                unsigned int byte_val;
                if (sscanf(hex_pair, "%x", &byte_val) == 1 && byte_val <= 0xFF) {
                    pattern_bytes.push_back((uint8_t)byte_val);
                } else {
                    printf("Invalid hex pair in sequence: %s\n", hex_pair);
                    return 0xFFFFFFFF;
                }
            }
        } else {
            printf("Invalid hex token (odd number of characters): %s\n", token);
            return 0xFFFFFFFF;
        }
        token = strtok(nullptr, " \t\n\r");
    }
    
    if (pattern_bytes.empty()) {
        printf("No valid hex bytes found in pattern\n");
        return 0xFFFFFFFF;
    }
    
    // Search for the pattern using the existing C function
    uint32_t result = 0xFFFFFFFF;
    bool ok = xemu_memory_viewer_search_virt(start_addr,
                                             kVirtSearchBytes,
                                             pattern_bytes.data(),
                                             pattern_bytes.size(),
                                             &result);

    if (ok) {
        printf("Found hex pattern at 0x%08X (%zu bytes)\n", result, pattern_bytes.size());
    }

    return result;
}

void MemoryViewerWindow::Draw()
{
    if (!m_is_open) {
        return;
    }
    
    if (!m_initialized) {
        Initialize();
        if (!m_initialized) {
            ImGui::SetNextWindowSize(ImVec2(400, 200), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Xbox Memory Viewer", &m_is_open)) {
                ImGui::Text("Failed to initialize memory viewer!");
                ImGui::Text("Xbox RAM region not found.");
                if (ImGui::Button("Retry")) {
                    Initialize();
                }
            }
            ImGui::End();
            return;
        }
    }
    
    ImGui::SetNextWindowSize(ImVec2(900, 700), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Xbox Memory Viewer", &m_is_open)) {
        DrawMemoryBrowserTab();
    }
    ImGui::End();
}

static void AppendUtf8Codepoint(std::string& out, uint32_t cp)
{
    // minimal UTF-8 encoder
    if (cp <= 0x7F) {
        out.push_back((char)cp);
    } else if (cp <= 0x7FF) {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
}

static std::string DecodeRowText(const uint8_t* bytes, size_t n,
                                 MemoryViewerWindow::TextEncoding enc)
{
    std::string out;
    out.reserve(n * 2);

    auto append_dot = [&]() { out.push_back('.'); };

    if (enc == MemoryViewerWindow::TextEncoding::Ascii) {
        for (size_t i = 0; i < n; i++) {
            uint8_t b = bytes[i];
            if (b >= 32 && b <= 126) out.push_back((char)b);
            else out.push_back('.');
        }
        return out;
    }

    if (enc == MemoryViewerWindow::TextEncoding::Utf8) {
        // best-effort UTF-8 decode; replace invalid with '.'
        size_t i = 0;
        while (i < n) {
            uint8_t b0 = bytes[i];

            if (b0 < 0x80) {
                // ASCII
                if (b0 >= 32 && b0 <= 126) out.push_back((char)b0);
                else out.push_back('.');
                i++;
                continue;
            }

            int need = 0;
            uint32_t cp = 0;
            if ((b0 & 0xE0) == 0xC0) { need = 1; cp = b0 & 0x1F; }
            else if ((b0 & 0xF0) == 0xE0) { need = 2; cp = b0 & 0x0F; }
            else if ((b0 & 0xF8) == 0xF0) { need = 3; cp = b0 & 0x07; }
            else { append_dot(); i++; continue; }

            if (i + (size_t)need >= n) { append_dot(); break; }

            bool ok = true;
            for (int k = 0; k < need; k++) {
                uint8_t bx = bytes[i + 1 + (size_t)k];
                if ((bx & 0xC0) != 0x80) { ok = false; break; }
                cp = (cp << 6) | (bx & 0x3F);
            }

            if (!ok) {
                append_dot();
                i++;
                continue;
            }

            // Put decoded codepoint in output (ImGui expects UTF-8)
            AppendUtf8Codepoint(out, cp);
            i += 1u + (size_t)need;
        }
        return out;
    }

    // UTF-16LE: treat as 2-byte units; display BMP (and basic surrogate pairs)
    if (enc == MemoryViewerWindow::TextEncoding::Utf16LE) {
        size_t i = 0;
        while (i + 1 < n) {
            uint16_t w1 = (uint16_t)bytes[i] | ((uint16_t)bytes[i + 1] << 8);
            i += 2;

            uint32_t cp = w1;

            // surrogate pair
            if (w1 >= 0xD800 && w1 <= 0xDBFF) {
                if (i + 1 >= n) { append_dot(); break; }
                uint16_t w2 = (uint16_t)bytes[i] | ((uint16_t)bytes[i + 1] << 8);
                i += 2;
                if (w2 >= 0xDC00 && w2 <= 0xDFFF) {
                    cp = 0x10000 + (((uint32_t)(w1 - 0xD800) << 10) | (uint32_t)(w2 - 0xDC00));
                } else {
                    append_dot();
                    continue;
                }
            }

            if (cp == 0) { append_dot(); continue; }
            AppendUtf8Codepoint(out, cp);
        }

        // if odd byte remaining
        if (i < n) append_dot();
        return out;
    }

    return out;
}

void MemoryViewerWindow::DrawMemoryBrowserTab()
{
    // Control bar
    ImGui::Text("Address:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::InputScalar("##addr", ImGuiDataType_U32, &m_current_address, 
                      NULL, NULL, "%08X", ImGuiInputTextFlags_CharsHexadecimal);
    
    // Navigation buttons
    ImGui::SameLine();
    if (ImGui::Button("- Page")) {
        uint32_t page_size = m_bytes_per_row * m_visible_rows;
        if (m_current_address >= page_size) {
            m_current_address -= page_size;
        } else {
            m_current_address = 0;
        }
        m_current_address = ClampVirt(m_current_address);
    }
    
    ImGui::SameLine();
    if (ImGui::Button("+ Page")) {
        uint32_t page_size = m_bytes_per_row * m_visible_rows;
        m_current_address += page_size;
        m_current_address = ClampVirt(m_current_address);
    }
    
    // Display Columns
    ImGui::SameLine();
    ImGui::Text("  Cols:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::SliderInt("##bpr", (int*)&m_bytes_per_row, 8, 32);

    // Display Rows
    ImGui::SameLine();
    ImGui::Text("  Rows:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    ImGui::SliderInt("##rows", (int*)&m_visible_rows, 8, 32);
    
    // Display ASCII
    ImGui::Separator();
    ImGui::TextUnformatted("Text View:");
    ImGui::SameLine();
    ImGui::Checkbox("Show Text Column", &m_show_text_column);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);

    const char* enc_items[] = { "ASCII", "UTF-8", "UTF-16LE" };
    int enc_idx = (int)m_text_encoding;
    if (ImGui::Combo("##textenc", &enc_idx, enc_items, IM_ARRAYSIZE(enc_items))) {
        m_text_encoding = (TextEncoding)enc_idx;
    }

    // Search functionality
    ImGui::Separator();
    ImGui::Text("Search:");
    ImGui::SameLine();
    bool hex_changed = ImGui::Checkbox("Hex", &m_hex_search_mode);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Search for hex bytes (e.g., \"00 00 80 3F\") instead of ASCII text");
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    bool pattern_changed = ImGui::InputText("##search", m_search_pattern, sizeof(m_search_pattern));
    
    if (hex_changed || pattern_changed) {
        m_search_cache_valid = false;
        m_search_results_count = 0;
        m_search_result_index = 0;
        m_last_search_addr = 0xFFFFFFFF;
        m_search_status[0] = '\0';
    }
    ImGui::SameLine();
    if (ImGui::Button("Find")) {
        m_search_status[0] = '\0';

        if (strlen(m_search_pattern) == 0) {
            snprintf(m_search_status, sizeof(m_search_status), "No results found");
        } else {
            // Rebuild cache if needed (pattern or hex mode changed)
            if (!m_search_cache_valid ||
                m_search_cache_hex != m_hex_search_mode ||
                strcmp(m_search_cache_pattern, m_search_pattern) != 0)
            {
                // Build result list
                uint32_t count = 0;

                if (m_hex_search_mode) {
                    std::vector<uint8_t> pattern_bytes;
                    char err[128];

                    if (!ParseHexPatternBytes(m_search_pattern, pattern_bytes, err, sizeof(err))) {
                        // Don’t mark cache valid; show why
                        m_search_cache_valid = false;
                        m_search_results_count = 0;
                        m_search_result_index = 0;
                        m_last_search_addr = 0xFFFFFFFF;
                        snprintf(m_search_status, sizeof(m_search_status), "%s", err);
                        return; // or `else`-wrap the rest
                    }

                    BuildVirtSearchResults(pattern_bytes.data(),
                                        pattern_bytes.size(),
                                        m_search_results,
                                        MemoryViewerWindow::kMaxSearchResults,
                                        &count);
                } else {
                    BuildVirtSearchResults((const uint8_t*)m_search_pattern,
                                        strlen(m_search_pattern),
                                        m_search_results,
                                        MemoryViewerWindow::kMaxSearchResults,
                                        &count);
                }

                m_search_results_count = count;
                m_search_result_index = 0;
                m_search_cache_valid = true;
                m_search_cache_hex = m_hex_search_mode;
                strncpy(m_search_cache_pattern, m_search_pattern, sizeof(m_search_cache_pattern));
                m_search_cache_pattern[sizeof(m_search_cache_pattern) - 1] = '\0';
            }

            if (m_search_results_count == 0) {
                m_last_search_addr = 0xFFFFFFFF;
                snprintf(m_search_status, sizeof(m_search_status), "No results found");
            } else {
                // Choose starting index based on "base start" (like your old behavior)
                uint32_t base = (m_last_search_addr != 0xFFFFFFFF)
                            ? (m_last_search_addr + 1)
                            : m_current_address;

                base = ClampVirt(base);

                uint32_t idx = 0;
                while (idx < m_search_results_count && m_search_results[idx] < base) {
                    idx++;
                }
                if (idx >= m_search_results_count) {
                    idx = 0; // wrap
                }

                m_search_result_index = idx;
                uint32_t found = m_search_results[m_search_result_index];

                m_current_address = found;
                m_last_search_addr = found;

                snprintf(m_search_status, sizeof(m_search_status),
                        "Found %u of %u", (m_search_result_index + 1), m_search_results_count);
            }
        }
    }
    
    if (m_search_status[0] != '\0') {
        ImGui::SameLine();
        if (ImGui::Button("Find Next")) {
            m_search_status[0] = '\0';

            if (m_search_results_count == 0 || !m_search_cache_valid) {
                snprintf(m_search_status, sizeof(m_search_status), "No results found");
            } else {
                m_search_result_index = (m_search_result_index + 1) % m_search_results_count;

                uint32_t found = m_search_results[m_search_result_index];
                m_current_address = found;
                m_last_search_addr = found;

                snprintf(m_search_status, sizeof(m_search_status),
                        "Found %u of %u", (m_search_result_index + 1), m_search_results_count);
            }
        }
    }

    // Display search status message next to button (blank if result found)
    if (m_search_status[0] != '\0') {
        ImGui::SameLine();
        ImGui::TextUnformatted(m_search_status);
    }
    
    // Write memory
    ImGui::Separator();
    ImGui::TextUnformatted("Write Memory (hex)");

    ImGui::SetNextItemWidth(130);
    ImGui::InputText("Offset", m_write_addr_text, sizeof(m_write_addr_text),
                    ImGuiInputTextFlags_CharsHexadecimal |
                    ImGuiInputTextFlags_CharsUppercase);

    ImGui::SameLine();

    ImGui::SetNextItemWidth(220);
    ImGui::InputText("Value", m_write_value_text, sizeof(m_write_value_text),
                    ImGuiInputTextFlags_CharsHexadecimal |
                    ImGuiInputTextFlags_CharsUppercase);

    ImGui::SameLine();

    if (ImGui::Button("Write")) {
        m_write_status[0] = '\0';

        // Parse address (accept optional 0x prefix)
        const char* addr_str = m_write_addr_text;
        if (addr_str[0] == '0' && (addr_str[1] == 'x' || addr_str[1] == 'X')) {
            addr_str += 2;
        }

        char* endp = nullptr;
        unsigned long addr_ul = strtoul(addr_str, &endp, 16);

        if (endp == addr_str || *endp != '\0') {
            snprintf(m_write_status, sizeof(m_write_status), "Invalid offset");
        } else {
            // Validate value string length: must be even # of hex chars
            size_t n = strlen(m_write_value_text);
            if (n == 0 || (n % 2) != 0) {
                snprintf(m_write_status, sizeof(m_write_status), "Value must have even # of hex digits");
            } else {
                // Convert hex string -> bytes
                const size_t byte_count = n / 2;
                if (byte_count > 256) {
                    snprintf(m_write_status, sizeof(m_write_status), "Value too long");
                } else {
                    uint8_t bytes[256];

                    bool ok = true;
                    for (size_t i = 0; i < byte_count; i++) {
                        char tmp[3];
                        tmp[0] = m_write_value_text[i * 2 + 0];
                        tmp[1] = m_write_value_text[i * 2 + 1];
                        tmp[2] = '\0';

                        char* endb = nullptr;
                        unsigned long b = strtoul(tmp, &endb, 16);
                        if (endb == tmp || *endb != '\0' || b > 0xFF) {
                            ok = false;
                            break;
                        }
                        bytes[i] = (uint8_t)b;
                    }

                    if (!ok) {
                        snprintf(m_write_status, sizeof(m_write_status), "Invalid hex in value");
                    } else {
                        uint32_t addr = (uint32_t)addr_ul;

                        // Write all bytes in the same order as typed
                        if (xemu_memory_viewer_write_virt(addr, bytes, byte_count)) {
                            snprintf(m_write_status, sizeof(m_write_status),
                                    "Wrote %zu byte(s) at 0x%08X", byte_count, addr);
                        } else {
                            snprintf(m_write_status, sizeof(m_write_status),
                                    "Write failed at 0x%08X", (uint32_t)addr_ul);
                        }
                    }
                }
            }
        }
    }

    if (m_write_status[0] != '\0') {
        ImGui::SameLine();
        ImGui::TextUnformatted(m_write_status);
    }

    // Memory dump controls at the bottom
    ImGui::Separator();
    ImGui::Text("Memory:");
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        SaveDump();
    }
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        LoadDump();
    }
    
    // Memory display
    ImGui::Separator();
    
    // Get memory info for status
    uint64_t total_ram;
    xemu_memory_viewer_get_info(&total_ram, NULL);
    
    ImGui::Text("Xbox RAM: %" PRIu64 " MB | Current: 0x%08X", 
               total_ram / (1024 * 1024), m_current_address);
    
    // Memory view child window
    ImGui::BeginChild("MemoryView", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
    
    // Handle mouse wheel scrolling when hovering over the memory view
    if (ImGui::IsWindowHovered()) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            int rows_to_move = (int)(-wheel); // Negative because wheel up should go up (decrease address)
            uint32_t bytes_to_move = rows_to_move * m_bytes_per_row;
            
            if (rows_to_move < 0) {
                // Moving up (decreasing address)
                uint32_t abs_bytes = -bytes_to_move;
                if (m_current_address >= abs_bytes) {
                    m_current_address -= abs_bytes;
                } else {
                    m_current_address = 0;
                }
            } else {
                // Moving down (increasing address)
                m_current_address += bytes_to_move;
            }
            m_current_address = ClampVirt(m_current_address);
        }
    }
    
    // Use fixed-width font for proper column alignment
    ImGui::PushFont(g_font_mgr.m_fixed_width_font);
    
    // Read memory data
    uint32_t display_size = m_bytes_per_row * m_visible_rows;
    uint8_t *memory_data = (uint8_t*)malloc(display_size);
    
    if (xemu_memory_viewer_read_virt(m_current_address, memory_data, display_size)) {

        // Track byte changes for flash-highlighting
        UpdateByteChangeCache(*this, m_current_address, memory_data, display_size);

        // Cell sizing (audio-debug-ish squares)
        const float pad_x = 6.0f;
        const float pad_y = 3.0f;
        const ImVec2 text_sz = ImGui::CalcTextSize("FF");
        const ImVec2 cell_sz(text_sz.x + pad_x * 2.0f, ImGui::GetTextLineHeight() + pad_y * 2.0f);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float now = (float)ImGui::GetTime();
        const bool show_ascii = m_show_text_column; // can probably be unified with the original..

        // Header should reflect the current base address "column offset"
        //const uint32_t header_start = (m_bytes_per_row != 0) ? (m_current_address % m_bytes_per_row) : 0;
        const uint32_t header_start = (m_current_address & 0x0Fu);

        ImGuiTableFlags flags =
            ImGuiTableFlags_SizingFixedFit 
            /*
            | ImGuiTableFlags_BordersOuter 
            | ImGuiTableFlags_BordersInnerV 
            | ImGuiTableFlags_BordersInnerH
            */
           ;

        const int total_cols = 1 + m_bytes_per_row + (show_ascii ? 1 : 0);

        // if (ImGui::BeginTable("MemGrid", 1 + (int)m_bytes_per_row, flags)) {

        //     // Address column
        //     ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 90.0f);

        //     // Byte columns
        //     for (uint32_t c = 0; c < m_bytes_per_row; c++) {
        //         char hdr[8];
        //         // Example: if base addr ends in ...A4 and bpr=16, header starts at 04
        //         const uint32_t h = (header_start + c) & 0xFF;
        //         snprintf(hdr, sizeof(hdr), "%02X", h);
        //         ImGui::TableSetupColumn(hdr, ImGuiTableColumnFlags_WidthFixed, cell_sz.x);
        //     }

        //     // Custom centered header row
        //     ImGui::TableNextRow(ImGuiTableRowFlags_Headers, cell_sz.y);

        //     // "Hex Address" header
        //     ImGui::TableSetColumnIndex(0);
        //     {
        //         ImVec2 p_min = ImGui::GetCursorScreenPos();
        //         ImVec2 p_max(p_min.x + 90.0f, p_min.y + cell_sz.y);

        //         const char* label = "Address";
        //         ImVec2 ts = ImGui::CalcTextSize(label);
        //         ImVec2 pos(p_min.x + (p_max.x - p_min.x - ts.x) * 0.5f,
        //                 p_min.y + (p_max.y - p_min.y - ts.y) * 0.5f);

        //         dl->AddText(pos, ImGui::GetColorU32(ImGuiCol_Text), label);
        //         ImGui::Dummy(ImVec2(p_max.x - p_min.x, cell_sz.y));
        //     }

        //     // Byte headers
        //     for (uint32_t c = 0; c < m_bytes_per_row; c++) {
        //         ImGui::TableSetColumnIndex(1 + (int)c);

        //         char hdr[8];
        //         const uint32_t h = (header_start + c) & 0xFF;
        //         snprintf(hdr, sizeof(hdr), "%02X", h);

        //         ImVec2 p_min = ImGui::GetCursorScreenPos();
        //         ImVec2 p_max(p_min.x + cell_sz.x, p_min.y + cell_sz.y);

        //         ImVec2 ts = ImGui::CalcTextSize(hdr);
        //         ImVec2 pos(p_min.x + (p_max.x - p_min.x - ts.x) * 0.5f,
        //                 p_min.y + (p_max.y - p_min.y - ts.y) * 0.5f);

        //         dl->AddText(pos, ImGui::GetColorU32(ImGuiCol_Text), hdr);
        //         ImGui::Dummy(ImVec2(cell_sz.x, cell_sz.y));
        //     }

        //     // Body
        //     for (uint32_t row = 0; row < m_visible_rows; row++) {
        //         ImGui::TableNextRow(ImGuiTableRowFlags_None, cell_sz.y);

        //         const uint32_t row_addr = m_current_address + row * m_bytes_per_row;

        //         // Address cell
        //         ImGui::TableSetColumnIndex(0);
        //         ImGui::Text("%08X", row_addr);

        //         // Byte cells
        //         for (uint32_t col = 0; col < m_bytes_per_row; col++) {
        //             const uint32_t idx = row * m_bytes_per_row + col;
        //             if (idx >= display_size) break;

        //             const uint32_t addr = row_addr + col;
        //             const uint8_t  b    = memory_data[idx];

        //             ImGui::TableSetColumnIndex(1 + (int)col);

        //             // Reserve the cell area
        //             ImVec2 p_min = ImGui::GetCursorScreenPos();
        //             ImVec2 p_max(p_min.x + cell_sz.x, p_min.y + cell_sz.y);

        //             // Base background
        //             ImU32 bg = ImGui::GetColorU32(ImGuiCol_FrameBg);
        //             dl->AddRectFilled(p_min, p_max, bg, 2.0f);

        //             // Flash overlay if changed recently
        //             const float changed_at = m_prev_view_changed_at[idx];
        //             const float dt = now - changed_at;
        //             if (dt >= 0.0f && dt < kFlashSeconds) {
        //                 float t = 1.0f - (dt / kFlashSeconds);      // 1 -> 0
        //                 float a = 0.65f * t;                        // fade out
        //                 ImVec4 flash = ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered);
        //                 flash.w = a;
        //                 dl->AddRectFilled(p_min, p_max, ImGui::GetColorU32(flash), 2.0f);
        //             }

        //             // Border
        //             ImU32 border = bg; // ImGui::GetColorU32(ImGuiCol_Border);
        //             dl->AddRect(p_min, p_max, border, 2.0f);

        //             // Optional: click to jump base to this exact byte
        //             ImGui::InvisibleButton(("##b" + std::to_string(addr)).c_str(), cell_sz);
        //             if (ImGui::IsItemClicked()) {
        //                 m_current_address = ClampVirt(addr);
        //             }

        //             // Text
        //             char buf[3];
        //             snprintf(buf, sizeof(buf), "%02X", b);

        //             ImU32 tc = (b == 0x00)
        //                 ? ImGui::GetColorU32(ImGuiCol_TextDisabled)
        //                 : ImGui::GetColorU32(ImGuiCol_Text);

        //             dl->AddText(ImVec2(p_min.x + pad_x, p_min.y + pad_y), tc, buf);

        //             if (m_show_text_column) {
        //                 ImGui::SameLine();
        //                 ImGui::TextUnformatted(" |");
        //                 ImGui::SameLine();

        //                 const uint8_t* row_ptr = memory_data + (row * m_bytes_per_row);
        //                 size_t row_len = (size_t)m_bytes_per_row;
        //                 if ((row * m_bytes_per_row + m_bytes_per_row) > display_size) {
        //                     row_len = display_size - (size_t)(row * m_bytes_per_row);
        //                 }

        //                 std::string text = DecodeRowText(row_ptr, row_len, m_text_encoding);
        //                 ImGui::TextUnformatted(text.c_str());

        //                 ImGui::SameLine();
        //                 ImGui::TextUnformatted("|");
        //             }
        //         }
        //     }

        //     ImGui::EndTable();
        // }
        if (ImGui::BeginTable("##memtable", total_cols, flags))
        {
            // Column setup
            ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 90.0f);

            // Hex byte columns
            for (int c = 0; c < m_bytes_per_row; c++) {
                char hdr[8];
                snprintf(hdr, sizeof(hdr), "%02X", (unsigned)c);
                ImGui::TableSetupColumn(hdr, ImGuiTableColumnFlags_WidthFixed, 22.0f);
            }

            if (show_ascii) {
                ImGui::TableSetupColumn("ASCII", ImGuiTableColumnFlags_WidthStretch);
            }

            // Header row
            ImGui::TableHeadersRow();

            // Rows
            for (uint32_t row = 0; row < m_visible_rows; row++) {
                ImGui::TableNextRow();

                uint32_t row_addr = m_current_address + row * m_bytes_per_row;
                uint32_t row_off  = row * m_bytes_per_row;

                // Address
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%08X", row_addr);

                // Hex cells
                for (uint32_t c = 0; c < m_bytes_per_row; c++) {
                    ImGui::TableSetColumnIndex(1 + (int)c);

                    uint32_t off = row_off + c;
                    if (off >= display_size) {
                        ImGui::Dummy(ImVec2(22.0f, 18.0f));
                        continue;
                    }

                    uint32_t addr = row_addr + c;
                    uint8_t  val  = memory_data[off];

                    float changed_at = -1.0f;
                    if (m_prev_view_valid && m_prev_view_base == m_current_address &&
                        off < m_prev_view_changed_at.size()) {
                        changed_at = m_prev_view_changed_at[off];
                    }

                    if (DrawHexCell(addr, val, changed_at, now, MemoryViewerWindow::kFlashSeconds)) {
                        
                    }
                }

                // ASCII column (unchanged)
                if (m_show_text_column) {
                    ImGui::TableSetColumnIndex(1 + (int)m_bytes_per_row);

                    char ascii_buf[256];
                    // (Use your existing ASCII builder or BuildAsciiRow)
                    BuildAsciiRow(ascii_buf, sizeof(ascii_buf), memory_data + row_off, m_bytes_per_row);
                    ImGui::TextUnformatted(ascii_buf);
                }
            }
            ImGui::EndTable();
        }

    } else {
        ImGui::Text("Failed to read memory at address 0x%08X", m_current_address);
        ImGui::Text("Address may be out of bounds for Xbox RAM (max: 0x%08X)",
                (uint32_t)(total_ram - 1));
    }
    
    ImGui::PopFont(); // Restore original font
    free(memory_data);
    ImGui::EndChild();
}

void MemoryViewerWindow::DrawMemoryUsageTab()
{
    // Initialize memory usage tracking if needed
    if (!m_memory_usage_initialized) {
        InitializeMemoryUsage();
    }
    
    // Update memory stats periodically
    static float last_update_time = 0.0f;
    float current_time = ImGui::GetTime();
    if (current_time - last_update_time > 0.5f) { // Update every 500ms
        if (m_memory_usage_initialized) {
            xemu_game_memory_usage_update();
        }
        last_update_time = current_time;
    }
    
    if (!m_memory_usage_initialized) {
        ImGui::Text("Memory usage tracker not initialized");
        if (ImGui::Button("Initialize")) {
            InitializeMemoryUsage();
        }
        return;
    }
    
    XboxGameMemoryUsage stats;
    if (!xemu_game_memory_usage_get_stats(&stats)) {
        ImGui::Text("Failed to get memory usage statistics");
        ImGui::Text("Make sure a game is running");
        return;
    }
    
    // Memory overview
    ImGui::Text("Xbox UMA Memory Analysis");
    ImGui::Separator();
    
    // Total memory bar
    float total_mb = stats.total_memory / (1024.0f * 1024.0f);
    float free_mb = stats.free_memory / (1024.0f * 1024.0f);
    float used_mb = total_mb - free_mb;
    float usage_percent = (used_mb / total_mb) * 100.0f;
    
    ImGui::Text("Total RAM: %.1f MB", total_mb);
    ImGui::ProgressBar(usage_percent / 100.0f, ImVec2(0.0f, 0.0f), "");
    ImGui::SameLine();
    ImGui::Text("%.1f%% used (%.1f MB free)", usage_percent, free_mb);
    
    ImGui::Separator();
    
    // Memory breakdown
    ImGui::Text("Memory Breakdown:");
    ImGui::Columns(2, "MemoryColumns");
    
    // Executable
    ImGui::Text("Executable Code:");
    ImGui::NextColumn();
    ImGui::Text("%.1f MB", stats.executable_size / (1024.0f * 1024.0f));
    if (stats.executable_base > 0) {
        ImGui::SameLine();
        ImGui::Text("(0x%08X)", stats.executable_base);
    }
    ImGui::NextColumn();
    
    // Heap
    ImGui::Text("Heap (Dynamic):");
    ImGui::NextColumn();
    ImGui::Text("%.1f MB", stats.heap_used / (1024.0f * 1024.0f));
    if (stats.peak_heap > stats.heap_used) {
        ImGui::SameLine();
        ImGui::Text("(peak: %.1f MB)", stats.peak_heap / (1024.0f * 1024.0f));
    }
    ImGui::NextColumn();
    
    // Stack
    ImGui::Text("Stack:");
    ImGui::NextColumn();
    ImGui::Text("%.1f MB", stats.stack_used / (1024.0f * 1024.0f));
    if (stats.peak_stack > stats.stack_used) {
        ImGui::SameLine();
        ImGui::Text("(peak: %.1f MB)", stats.peak_stack / (1024.0f * 1024.0f));
    }
    ImGui::NextColumn();
    
    // GPU Buffers
    ImGui::Text("GPU Buffers:");
    ImGui::NextColumn();
    ImGui::Text("%.1f MB", stats.gpu_buffers / (1024.0f * 1024.0f));
    if (stats.peak_gpu_buffers > stats.gpu_buffers) {
        ImGui::SameLine();
        ImGui::Text("(peak: %.1f MB)", stats.peak_gpu_buffers / (1024.0f * 1024.0f));
    }
    ImGui::NextColumn();
    
    // Audio Buffers
    ImGui::Text("Audio Buffers:");
    ImGui::NextColumn();
    ImGui::Text("%.1f MB", stats.audio_buffers / (1024.0f * 1024.0f));
    ImGui::NextColumn();
    
    ImGui::Columns(1);
    ImGui::Separator();
    
    // Control buttons
    if (ImGui::Button("Reset Peak Values")) {
        xemu_game_memory_usage_reset_peaks();
    }
    ImGui::SameLine();
    if (ImGui::Button("Force Analysis")) {
        xemu_game_memory_usage_analyze_regions();
    }
    ImGui::SameLine();
    if (ImGui::Button("Jump to Executable")) {
        if (stats.executable_base > 0) {
            m_current_address = stats.executable_base;
        }
    }
    
    // Memory region visualization
    ImGui::Separator();
    ImGui::Text("Memory Map (simplified):");
    
    const float bar_width = 400.0f;
    const float bar_height = 25.0f;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    
    // Draw memory usage bar with different colors for different regions
    float current_x = 0.0f;
    
    // Executable (red)
    if (stats.executable_size > 0) {
        float width = (stats.executable_size / (float)stats.total_memory) * bar_width;
        draw_list->AddRectFilled(
            ImVec2(canvas_pos.x + current_x, canvas_pos.y),
            ImVec2(canvas_pos.x + current_x + width, canvas_pos.y + bar_height),
            IM_COL32(255, 100, 100, 180));
        current_x += width;
    }
    
    // Heap (green)
    if (stats.heap_used > 0) {
        float width = (stats.heap_used / (float)stats.total_memory) * bar_width;
        draw_list->AddRectFilled(
            ImVec2(canvas_pos.x + current_x, canvas_pos.y),
            ImVec2(canvas_pos.x + current_x + width, canvas_pos.y + bar_height),
            IM_COL32(100, 255, 100, 180));
        current_x += width;
    }
    
    // GPU buffers (blue)
    if (stats.gpu_buffers > 0) {
        float width = (stats.gpu_buffers / (float)stats.total_memory) * bar_width;
        draw_list->AddRectFilled(
            ImVec2(canvas_pos.x + current_x, canvas_pos.y),
            ImVec2(canvas_pos.x + current_x + width, canvas_pos.y + bar_height),
            IM_COL32(100, 100, 255, 180));
        current_x += width;
    }
    
    // Stack (yellow)
    if (stats.stack_used > 0) {
        float width = (stats.stack_used / (float)stats.total_memory) * bar_width;
        draw_list->AddRectFilled(
            ImVec2(canvas_pos.x + current_x, canvas_pos.y),
            ImVec2(canvas_pos.x + current_x + width, canvas_pos.y + bar_height),
            IM_COL32(255, 255, 100, 180));
        current_x += width;
    }
    
    // Audio buffers (purple)
    if (stats.audio_buffers > 0) {
        float width = (stats.audio_buffers / (float)stats.total_memory) * bar_width;
        draw_list->AddRectFilled(
            ImVec2(canvas_pos.x + current_x, canvas_pos.y),
            ImVec2(canvas_pos.x + current_x + width, canvas_pos.y + bar_height),
            IM_COL32(255, 100, 255, 180));
        current_x += width;
    }
    
    // Free space (gray)
    if (stats.free_memory > 0) {
        float width = (stats.free_memory / (float)stats.total_memory) * bar_width;
        draw_list->AddRectFilled(
            ImVec2(canvas_pos.x + current_x, canvas_pos.y),
            ImVec2(canvas_pos.x + current_x + width, canvas_pos.y + bar_height),
            IM_COL32(160, 160, 160, 120));
    }
    
    // Border
    draw_list->AddRect(canvas_pos, ImVec2(canvas_pos.x + bar_width, canvas_pos.y + bar_height), 
                       IM_COL32(255, 255, 255, 255));
    
    ImGui::Dummy(ImVec2(bar_width, bar_height + 10));
    
    // Legend
    ImGui::Text("Legend:");
    ImGui::SameLine(); ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "■ Executable");
    ImGui::SameLine(); ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "■ Heap");
    ImGui::SameLine(); ImGui::TextColored(ImVec4(0.4f, 0.4f, 1.0f, 1.0f), "■ GPU");
    ImGui::SameLine(); ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.4f, 1.0f), "■ Stack");
    ImGui::SameLine(); ImGui::TextColored(ImVec4(1.0f, 0.4f, 1.0f, 1.0f), "■ Audio");
    ImGui::SameLine(); ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "■ Free");
    
    // Text report
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Detailed Report")) {
        char report_buffer[2048];
        int report_size = xemu_game_memory_usage_get_report(report_buffer, sizeof(report_buffer));
        if (report_size > 0) {
            ImGui::PushFont(g_font_mgr.m_fixed_width_font);
            ImGui::TextUnformatted(report_buffer);
            ImGui::PopFont();
        } else {
            ImGui::Text("No detailed report available");
        }
    }
}

void MemoryViewerWindow::InitializeMemoryUsage()
{
    if (xemu_game_memory_usage_init()) {
        m_memory_usage_initialized = true;
        printf("Memory usage tracker initialized successfully\n");
    } else {
        printf("Failed to initialize memory usage tracker\n");
        m_memory_usage_initialized = false;
    }
}
