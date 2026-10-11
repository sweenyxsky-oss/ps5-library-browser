#include "ps5_library/views.hpp"

#include <algorithm>
#include <cmath>

namespace ps5_library {

const char* to_string(LibraryMode mode) {
    switch (mode) {
        case LibraryMode::InstalledGames: return "Installed Games";
        case LibraryMode::LibraryBrowser: return "Library Browser";
        default:                          return "Unknown";
    }
}

const char* to_string(ViewLayout layout) {
    switch (layout) {
        case ViewLayout::Coverflow: return "Coverflow";
        case ViewLayout::BlockGrid: return "Block Grid";
        default:                    return "Unknown";
    }
}

const char* to_string(FormatFilter filter) {
    switch (filter) {
        case FormatFilter::All:    return "All Formats";
        case FormatFilter::Pkg:    return "PKG";
        case FormatFilter::ExFat:  return "exFAT";
        case FormatFilter::FfPkg:  return "FFPKG";
        case FormatFilter::Folder: return "Folder";
        default:                   return "All";
    }
}

FormatFilter next_filter(FormatFilter current) {
    switch (current) {
        case FormatFilter::All:    return FormatFilter::Pkg;
        case FormatFilter::Pkg:    return FormatFilter::ExFat;
        case FormatFilter::ExFat:  return FormatFilter::FfPkg;
        case FormatFilter::FfPkg:  return FormatFilter::Folder;
        case FormatFilter::Folder: return FormatFilter::All;
        default:                   return FormatFilter::All;
    }
}

FormatFilter prev_filter(FormatFilter current) {
    switch (current) {
        case FormatFilter::All:    return FormatFilter::Folder;
        case FormatFilter::Folder: return FormatFilter::FfPkg;
        case FormatFilter::FfPkg:  return FormatFilter::ExFat;
        case FormatFilter::ExFat:  return FormatFilter::Pkg;
        case FormatFilter::Pkg:    return FormatFilter::All;
        default:                   return FormatFilter::All;
    }
}

ViewLayoutEngine::ViewLayoutEngine() = default;

std::vector<CardVisual> ViewLayoutEngine::compute_coverflow(const std::vector<GameEntry>& entries,
                                                           size_t focused_index,
                                                           float scroll_offset) const {
    std::vector<CardVisual> visuals;
    if (entries.empty()) {
        return visuals;
    }

    if (focused_index >= entries.size()) {
        focused_index = entries.size() - 1;
    }

    const float center_x = viewport_width_ * 0.5f + scroll_offset;
    const float center_y = viewport_height_ * 0.48f;
    const float base_w = 340.0f;
    const float base_h = 480.0f;

    // View range limit to only compute cards near the center
    const int window = 8;
    int start_idx = std::max(0, static_cast<int>(focused_index) - window);
    int end_idx = std::min(static_cast<int>(entries.size()) - 1, static_cast<int>(focused_index) + window);

    for (int i = start_idx; i <= end_idx; ++i) {
        const auto& entry = entries[static_cast<size_t>(i)];
        int distance = i - static_cast<int>(focused_index);
        int abs_dist = std::abs(distance);

        CardVisual card;
        card.index = static_cast<size_t>(i);
        card.title_id = entry.title_id;
        card.title = entry.title;
        card.primary_format = entry.primary_format;
        card.formatted_size = entry.formatted_total_size;
        card.cover_path = entry.cover_path;
        card.update_count = entry.updates.size();
        card.dlc_count = entry.dlcs.size();
        card.is_focused = (distance == 0);

        // Badges
        std::string fmt_upper = entry.primary_format;
        std::transform(fmt_upper.begin(), fmt_upper.end(), fmt_upper.begin(), ::toupper);
        card.format_badge = fmt_upper.empty() ? "GAME" : fmt_upper;

        std::string extra;
        if (card.update_count > 0) extra += "+" + std::to_string(card.update_count) + " Patch ";
        if (card.dlc_count > 0) extra += "+" + std::to_string(card.dlc_count) + " DLC ";
        card.details_badge = card.formatted_size + (extra.empty() ? "" : (" (" + extra + ")"));

        if (distance == 0) {
            // Center focused card
            card.x = center_x;
            card.y = center_y;
            card.width = base_w;
            card.height = base_h;
            card.scale = 1.20f;
            card.opacity = 1.0f;
            card.z_order = 100;
        } else {
            float sign = (distance > 0) ? 1.0f : -1.0f;
            float step = 260.0f + (abs_dist - 1) * 170.0f;
            card.x = center_x + sign * step;
            card.y = center_y + 20.0f; // Slightly lowered to emphasize center card
            card.width = base_w;
            card.height = base_h;
            card.scale = std::max(0.70f, 0.85f - abs_dist * 0.03f);
            card.opacity = std::max(0.15f, 0.85f - abs_dist * 0.12f);
            card.z_order = 50 - abs_dist;
        }

        visuals.push_back(std::move(card));
    }

    // Sort by z_order ascending so painters can draw back-to-front
    std::sort(visuals.begin(), visuals.end(), [](const CardVisual& a, const CardVisual& b) {
        return a.z_order < b.z_order;
    });

    return visuals;
}

std::vector<CardVisual> ViewLayoutEngine::compute_block_grid(const std::vector<GameEntry>& entries,
                                                            size_t focused_index,
                                                            int columns,
                                                            float scroll_offset) const {
    std::vector<CardVisual> visuals;
    if (entries.empty() || columns <= 0) {
        return visuals;
    }

    if (focused_index >= entries.size()) {
        focused_index = entries.size() - 1;
    }

    const float card_w = 280.0f;
    const float card_h = 390.0f;
    const float spacing_x = 42.0f;
    const float spacing_y = 48.0f;

    const float total_grid_w = columns * card_w + (columns - 1) * spacing_x;
    const float start_x = (viewport_width_ - total_grid_w) * 0.5f + card_w * 0.5f;

    // Viewport row centering
    int focused_row = static_cast<int>(focused_index) / columns;
    float row_height = card_h + spacing_y;
    float base_start_y = 240.0f;
    float grid_scroll_y = base_start_y - (focused_row * row_height) + scroll_offset;

    // Keep top rows visible when near the top
    if (grid_scroll_y > base_start_y) {
        grid_scroll_y = base_start_y;
    }

    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        int col = static_cast<int>(i) % columns;
        int row = static_cast<int>(i) / columns;

        float cx = start_x + col * (card_w + spacing_x);
        float cy = grid_scroll_y + row * row_height;

        // Skip cards outside the vertical viewport margin
        if (cy < -card_h || cy > viewport_height_ + card_h) {
            continue;
        }

        CardVisual card;
        card.index = i;
        card.title_id = entry.title_id;
        card.title = entry.title;
        card.primary_format = entry.primary_format;
        card.formatted_size = entry.formatted_total_size;
        card.cover_path = entry.cover_path;
        card.update_count = entry.updates.size();
        card.dlc_count = entry.dlcs.size();
        card.is_focused = (i == focused_index);

        card.x = cx;
        card.y = cy;
        card.width = card_w;
        card.height = card_h;

        // Format and detail badges
        std::string fmt_upper = entry.primary_format;
        std::transform(fmt_upper.begin(), fmt_upper.end(), fmt_upper.begin(), ::toupper);
        card.format_badge = fmt_upper.empty() ? "GAME" : fmt_upper;
        card.details_badge = card.formatted_size;

        if (card.is_focused) {
            card.scale = 1.08f;
            card.opacity = 1.0f;
            card.z_order = 10;
        } else {
            card.scale = 1.0f;
            card.opacity = 0.85f;
            card.z_order = 1;
        }

        visuals.push_back(std::move(card));
    }

    return visuals;
}

size_t ViewLayoutEngine::move_grid_focus(size_t current_index,
                                        int delta_x,
                                        int delta_y,
                                        size_t total_items,
                                        int columns) {
    if (total_items == 0 || columns <= 0) return 0;

    int cur = static_cast<int>(current_index);
    int total = static_cast<int>(total_items);

    if (delta_x > 0) {
        if ((cur % columns) < (columns - 1) && (cur + 1 < total)) {
            cur += 1;
        }
    } else if (delta_x < 0) {
        if ((cur % columns) > 0) {
            cur -= 1;
        }
    }

    if (delta_y > 0) {
        if (cur + columns < total) {
            cur += columns;
        } else {
            // Move to last item if in bottom row
            cur = total - 1;
        }
    } else if (delta_y < 0) {
        if (cur - columns >= 0) {
            cur -= columns;
        }
    }

    return static_cast<size_t>(cur);
}

} // namespace ps5_library
