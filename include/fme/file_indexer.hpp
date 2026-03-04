#pragma once

#include <filesystem>
#include <vector>

#include "fme/media_file.hpp"

namespace fme {

class FileIndexer {
public:
    std::vector<MediaFile> ScanFolderRecursive(const std::filesystem::path& root) const;
    std::vector<MediaFile> ScanFolderShallow(const std::filesystem::path& folder) const;
};

}  // namespace fme
