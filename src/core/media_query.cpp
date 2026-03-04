#include "fme/media_query.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <cwctype>
#include <filesystem>
#include <unordered_map>

namespace {

std::wstring ToLower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return value;
}

bool ContainsCaseInsensitive(const std::wstring& text, const std::wstring& query) {
    if (query.empty()) {
        return true;
    }
    return ToLower(text).find(ToLower(query)) != std::wstring::npos;
}

bool MatchesTypeFilter(fme::MediaType file_type, fme::MediaTypeFilter filter) {
    switch (filter) {
    case fme::MediaTypeFilter::All:
        return true;
    case fme::MediaTypeFilter::Image:
        return file_type == fme::MediaType::Image;
    case fme::MediaTypeFilter::Video:
        return file_type == fme::MediaType::Video;
    case fme::MediaTypeFilter::Audio:
        return file_type == fme::MediaType::Audio;
    case fme::MediaTypeFilter::Other:
        return file_type == fme::MediaType::Other;
    default:
        return true;
    }
}

std::chrono::system_clock::time_point ToSystemClock(std::filesystem::file_time_type file_time) {
    using namespace std::chrono;
    return time_point_cast<system_clock::duration>(
        file_time - std::filesystem::file_time_type::clock::now() + system_clock::now());
}

std::tm ToLocalTm(std::chrono::system_clock::time_point tp) {
    const std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm out{};
    localtime_s(&out, &t);
    return out;
}

int DayKey(const std::tm& tm) {
    const int year = tm.tm_year + 1900;
    const int month = tm.tm_mon + 1;
    const int day = tm.tm_mday;
    return (year * 10000) + (month * 100) + day;
}

bool MatchesDateFilter(const fme::MediaFile& file, const fme::MediaQuery& query) {
    using namespace std::chrono;
    if (query.date_filter == fme::DateFilter::All) {
        return true;
    }

    const auto now = system_clock::now();
    const auto file_time = ToSystemClock(file.last_write_time);
    if (file_time > now) {
        return true;
    }

    switch (query.date_filter) {
    case fme::DateFilter::Today: {
        const int today = DayKey(ToLocalTm(now));
        const int file_day = DayKey(ToLocalTm(file_time));
        return file_day == today;
    }
    case fme::DateFilter::Last7Days: {
        const auto age = now - file_time;
        return age <= hours(24 * 7);
    }
    case fme::DateFilter::Last30Days: {
        const auto age = now - file_time;
        return age <= hours(24 * 30);
    }
    case fme::DateFilter::CustomRange:
        return file_time >= query.date_from && file_time <= query.date_to;
    default:
        return true;
    }
}

bool MatchesSizeFilter(const fme::MediaFile& file, const fme::MediaQuery& query) {
    return file.size >= query.min_size && file.size <= query.max_size;
}

int CompareTextInsensitive(const std::wstring& a, const std::wstring& b) {
    const std::wstring al = ToLower(a);
    const std::wstring bl = ToLower(b);
    if (al < bl) return -1;
    if (al > bl) return 1;
    return 0;
}

std::wstring ExtensionFor(const fme::MediaFile& file) {
    return std::filesystem::path(file.full_path).extension().wstring();
}

}  // namespace

namespace fme {

std::vector<size_t> ApplyMediaQuery(const std::vector<MediaFile>& files, const MediaQuery& query) {
    std::vector<size_t> indices;
    indices.reserve(files.size());

    for (size_t i = 0; i < files.size(); ++i) {
        const auto& file = files[i];
        if (!MatchesTypeFilter(file.type, query.type_filter)) {
            continue;
        }
        if (!MatchesDateFilter(file, query)) {
            continue;
        }
        if (!MatchesSizeFilter(file, query)) {
            continue;
        }
        if (!ContainsCaseInsensitive(file.name, query.name_substring)) {
            continue;
        }
        indices.push_back(i);
    }

    if (query.duplicates_by_size_only) {
        std::unordered_map<std::uintmax_t, size_t> counts;
        counts.reserve(indices.size());
        for (size_t idx : indices) {
            ++counts[files[idx].size];
        }

        std::vector<size_t> dup;
        dup.reserve(indices.size());
        for (size_t idx : indices) {
            const auto it = counts.find(files[idx].size);
            if (it != counts.end() && it->second > 1) {
                dup.push_back(idx);
            }
        }
        indices.swap(dup);
    }

    std::sort(indices.begin(), indices.end(), [&files, &query](size_t li, size_t ri) {
        const auto& l = files[li];
        const auto& r = files[ri];
        int cmp = 0;
        switch (query.sort_field) {
        case SortField::Name:
            cmp = CompareTextInsensitive(l.name, r.name);
            break;
        case SortField::Type:
            cmp = CompareTextInsensitive(MediaTypeToText(l.type), MediaTypeToText(r.type));
            break;
        case SortField::Extension:
            cmp = CompareTextInsensitive(ExtensionFor(l), ExtensionFor(r));
            break;
        case SortField::Size:
            if (l.size < r.size) cmp = -1;
            else if (l.size > r.size) cmp = 1;
            else cmp = 0;
            break;
        default:
            cmp = CompareTextInsensitive(l.name, r.name);
            break;
        }

        if (cmp == 0) {
            cmp = CompareTextInsensitive(l.name, r.name);
        }
        return query.sort_ascending ? (cmp < 0) : (cmp > 0);
    });

    if (query.max_results > 0 && indices.size() > query.max_results) {
        indices.resize(query.max_results);
    }

    return indices;
}

std::wstring MediaTypeToText(MediaType type) {
    switch (type) {
    case MediaType::Image:
        return L"Image";
    case MediaType::Video:
        return L"Video";
    case MediaType::Audio:
        return L"Audio";
    default:
        return L"Other";
    }
}

}  // namespace fme
