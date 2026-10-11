#pragma once

#include "game_metadata.hpp"

#include <string>
#include <vector>
#include <cstddef>

namespace ps5_library {

enum class LibraryMode {
    InstalledGames,
    LibraryBrowser
};

const char* to_string(LibraryMode mode);

enum class ViewLayout {
    Coverflow,
    BlockGrid
};

const char* to_string(ViewLayout layout);

enum class FormatFilter {
    All,
    Pkg,
    ExFat,
    FfPkg,
    Folder
};

const char* to_string(FormatFilter filter);
FormatFilter next_filter(FormatFilter current);
FormatFilter prev_filter(FormatFilter current);

struct CardVisual {
    size_t index{0};
    float x{0.0f};
    float y{0.0f};
    float width{0.0f};
    float height{0.0f};
    float scale{1.0f};
    float opacity{1.0f};
    int z_order{0};
    bool is_focused{false};

    std::string title_id;
    std::string title;
    std::string primary_format;
    std::string formatted_size;
    std::string cover_path;
    size_t update_count{0};
    size_t dlc_count{0};

    std::string format_badge;
    std::string details_badge;
};

class ViewLayoutEngine {
public:
    ViewLayoutEngine();

    void set_viewport(float width, float height) {
        viewport_width_ = width;
        viewport_height_ = height;
    }

    float viewport_width() const { return viewport_width_; }
    float viewport_height() const { return viewport_height_; }

    // Computes positioning for all visible cards in Coverflow mode
    std::vector<CardVisual> compute_coverflow(const std::vector<GameEntry>& entries,
                                             size_t focused_index,
                                             float scroll_offset = 0.0f) const;

    // Computes positioning for all visible cards in Block Grid mode (5 columns by default)
    std::vector<CardVisual> compute_block_grid(const std::vector<GameEntry>& entries,
                                               size_t focused_index,
                                               int columns = 5,
                                               float scroll_offset = 0.0f) const;

    // Grid navigation helper: moves focus in 2D grid
    static size_t move_grid_focus(size_t current_index,
                                  int delta_x,
                                  int delta_y,
                                  size_t total_items,
                                  int columns = 5);

private:
    float viewport_width_{1920.0f};
    float viewport_height_{1080.0f};
};

} // namespace ps5_library
