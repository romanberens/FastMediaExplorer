#pragma once

#include <cstddef>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "fme/media_file.hpp"

namespace fme {

enum class MediaTypeFilter {
    All,
    Image,
    Video,
    Audio,
    Other
};

enum class SortField {
    Name,
    Type,
    Extension,
    Size
};

enum class DateFilter {
    All,
    Today,
    Last7Days,
    Last30Days,
    CustomRange
};

struct MediaQuery {
    std::wstring name_substring;
    MediaTypeFilter type_filter = MediaTypeFilter::All;
    DateFilter date_filter = DateFilter::All;
    std::chrono::system_clock::time_point date_from{};
    std::chrono::system_clock::time_point date_to{};
    std::uintmax_t min_size = 0;
    std::uintmax_t max_size = (std::numeric_limits<std::uintmax_t>::max)();
    bool duplicates_by_size_only = false;
    size_t max_results = 0;
    SortField sort_field = SortField::Name;
    bool sort_ascending = true;
};

std::vector<size_t> ApplyMediaQuery(const std::vector<MediaFile>& files, const MediaQuery& query);
std::wstring MediaTypeToText(MediaType type);

}  // namespace fme
