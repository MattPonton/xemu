//
// xemu User Interface
//
// Copyright (C) 2020-2022 Matt Borgerson
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
#pragma once

#include <cstdint>
#include <vector>

class DebugApuWindow
{
public:
    bool m_is_open;
    DebugApuWindow();
    void Draw();
};

class DebugVideoWindow
{
public:
    bool m_is_open;
    bool m_transparent;
    bool m_position_restored;
    bool m_resize_init_complete;
    float m_prev_scale;

    DebugVideoWindow();
    void Draw();
};

class MemoryViewerWindow
{
public:
    static constexpr uint32_t kMaxSearchResults = 1024;
    static constexpr float kFlashSeconds = 0.35f;
    bool m_is_open;
    bool m_initialized;
    uint32_t m_current_address;
    uint32_t m_bytes_per_row;
    uint32_t m_visible_rows;
    char m_search_pattern[256];
    char m_search_status[256];
    uint32_t m_search_results[kMaxSearchResults];
    uint32_t m_search_results_count;
    uint32_t m_search_result_index;
    bool m_search_cache_valid;
    char m_search_cache_pattern[256];
    bool m_search_cache_hex;
    char m_write_addr_text[16];
    char m_write_value_text[256];
    char m_write_status[128];
    uint32_t m_last_search_addr;
    bool m_hex_search_mode;
    bool m_memory_usage_initialized;
    bool m_show_usage_tab;
    std::vector<uint8_t> m_prev_view_bytes;
    std::vector<float> m_prev_view_changed_at;
    uint32_t m_prev_view_base = 0;
    uint32_t m_prev_view_size = 0;
    bool m_prev_view_valid = false;

    MemoryViewerWindow();
    void Draw();
    void Toggle();
    enum class TextEncoding : int {
        Ascii = 0,
        Utf8,
        Utf16LE,
        //ShiftJIS, // (optional later if add iconv)
    };
    bool m_show_text_column = true; // show the right-hand text column
    TextEncoding m_text_encoding = TextEncoding::Ascii;

private:
    void Initialize();
    void SaveDump();
    void LoadDump();
    uint32_t SearchHexPattern(const char* hex_pattern, uint32_t start_addr);
    void DrawMemoryUsageTab();
    void DrawMemoryBrowserTab();
    void InitializeMemoryUsage();
};

extern DebugApuWindow apu_window;
extern DebugVideoWindow video_window;
extern MemoryViewerWindow memory_viewer_window;
