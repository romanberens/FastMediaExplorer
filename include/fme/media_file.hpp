#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace fme {

enum class MediaType {
    Image,
    Video,
    Audio,
    Other
};

struct MediaFile {
    std::wstring full_path;
    std::wstring name;
    std::uintmax_t size = 0;
    std::filesystem::file_time_type last_write_time{};
    MediaType type = MediaType::Other;
};

}  // namespace fme
