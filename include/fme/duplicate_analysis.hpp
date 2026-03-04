#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "fme/media_file.hpp"

namespace fme {

enum class DuplicateMode {
    SizeOnly,
    ExactHash,
    ExactHashNameExt
};

struct DuplicateAnalysisOptions {
    DuplicateMode mode = DuplicateMode::SizeOnly;
    std::uintmax_t min_size = 0;
};

struct DuplicateGroup {
    std::vector<size_t> indices;
    std::uintmax_t size = 0;
};

struct DuplicateAnalysisResult {
    std::vector<DuplicateGroup> groups;
    size_t duplicate_file_count = 0;
    std::uintmax_t reclaimable_bytes = 0;
};

DuplicateAnalysisResult AnalyzeDuplicates(const std::vector<MediaFile>& files, const DuplicateAnalysisOptions& options);

}  // namespace fme

