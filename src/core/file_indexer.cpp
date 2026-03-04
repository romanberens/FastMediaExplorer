#include "fme/file_indexer.hpp"

#include <algorithm>
#include <array>
#include <cwctype>
#include <system_error>

namespace {

fme::MediaType DetectMediaType(const std::wstring& extension) {
    static constexpr std::array<const wchar_t*, 5> kImages{L".jpg", L".jpeg", L".png", L".bmp", L".webp"};
    static constexpr std::array<const wchar_t*, 4> kVideos{L".mp4", L".avi", L".mov", L".mkv"};
    static constexpr std::array<const wchar_t*, 4> kAudio{L".mp3", L".wav", L".flac", L".aac"};

    auto lower = extension;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

    if (std::find(kImages.begin(), kImages.end(), lower) != kImages.end()) {
        return fme::MediaType::Image;
    }
    if (std::find(kVideos.begin(), kVideos.end(), lower) != kVideos.end()) {
        return fme::MediaType::Video;
    }
    if (std::find(kAudio.begin(), kAudio.end(), lower) != kAudio.end()) {
        return fme::MediaType::Audio;
    }
    return fme::MediaType::Other;
}

bool IsSupportedMediaFile(const std::filesystem::path& path) {
    return DetectMediaType(path.extension().wstring()) != fme::MediaType::Other;
}

bool TryBuildMediaFile(const std::filesystem::directory_entry& entry, fme::MediaFile* out_file) {
    std::error_code ec;
    if (!entry.is_regular_file(ec) || ec) {
        return false;
    }
    if (!IsSupportedMediaFile(entry.path())) {
        return false;
    }

    const auto media_type = DetectMediaType(entry.path().extension().wstring());
    if (media_type == fme::MediaType::Other) {
        return false;
    }

    std::error_code size_ec;
    const auto size = entry.file_size(size_ec);
    if (size_ec) {
        return false;
    }

    std::error_code time_ec;
    const auto last_write = entry.last_write_time(time_ec);
    if (time_ec) {
        return false;
    }

    fme::MediaFile file{};
    file.full_path = entry.path().wstring();
    file.name = entry.path().filename().wstring();
    file.type = media_type;
    file.size = size;
    file.last_write_time = last_write;
    *out_file = std::move(file);
    return true;
}

}  // namespace

namespace fme {

std::vector<MediaFile> FileIndexer::ScanFolderRecursive(const std::filesystem::path& root) const {
    std::vector<MediaFile> output;

    if (!std::filesystem::exists(root) || !std::filesystem::is_directory(root)) {
        return output;
    }

    std::error_code ec;
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (ec) {
            continue;
        }
        MediaFile file{};
        if (TryBuildMediaFile(entry, &file)) {
            output.push_back(std::move(file));
        }
    }

    return output;
}

std::vector<MediaFile> FileIndexer::ScanFolderShallow(const std::filesystem::path& folder) const {
    std::vector<MediaFile> output;
    if (!std::filesystem::exists(folder) || !std::filesystem::is_directory(folder)) {
        return output;
    }

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(folder, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (ec) {
            continue;
        }
        MediaFile file{};
        if (TryBuildMediaFile(entry, &file)) {
            output.push_back(std::move(file));
        }
    }
    return output;
}

}  // namespace fme
