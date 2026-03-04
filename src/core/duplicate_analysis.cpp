#include "fme/duplicate_analysis.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <cwctype>
#include <unordered_map>
#include <utility>

namespace {

std::wstring ExtensionLower(const std::wstring& path) {
    std::wstring ext = std::filesystem::path(path).extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return ext;
}

std::uint64_t Fnv1aFile64(const std::wstring& path) {
    constexpr std::uint64_t kOffset = 14695981039346656037ull;
    constexpr std::uint64_t kPrime = 1099511628211ull;

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return 0;
    }

    std::uint64_t h = kOffset;
    char buffer[64 * 1024];
    while (in) {
        in.read(buffer, sizeof(buffer));
        const std::streamsize n = in.gcount();
        for (std::streamsize i = 0; i < n; ++i) {
            h ^= static_cast<unsigned char>(buffer[i]);
            h *= kPrime;
        }
    }
    return h;
}

template <typename TKey>
std::vector<std::vector<size_t>> GroupsFromMap(const std::unordered_map<TKey, std::vector<size_t>>& map) {
    std::vector<std::vector<size_t>> out;
    out.reserve(map.size());
    for (const auto& kv : map) {
        if (kv.second.size() > 1) {
            out.push_back(kv.second);
        }
    }
    return out;
}

}  // namespace

namespace fme {

DuplicateAnalysisResult AnalyzeDuplicates(const std::vector<MediaFile>& files, const DuplicateAnalysisOptions& options) {
    std::unordered_map<std::uintmax_t, std::vector<size_t>> by_size;
    by_size.reserve(files.size());

    for (size_t i = 0; i < files.size(); ++i) {
        if (files[i].size < options.min_size) {
            continue;
        }
        by_size[files[i].size].push_back(i);
    }

    std::vector<std::vector<size_t>> size_groups = GroupsFromMap(by_size);
    std::vector<DuplicateGroup> groups;

    if (options.mode == DuplicateMode::SizeOnly) {
        groups.reserve(size_groups.size());
        for (auto& g : size_groups) {
            const size_t first_idx = g.front();
            const std::uintmax_t sz = files[first_idx].size;
            groups.push_back(DuplicateGroup{ std::move(g), sz });
        }
    } else if (options.mode == DuplicateMode::ExactHash) {
        for (const auto& base_group : size_groups) {
            std::unordered_map<std::uint64_t, std::vector<size_t>> by_hash;
            by_hash.reserve(base_group.size());
            for (size_t idx : base_group) {
                const std::uint64_t h = Fnv1aFile64(files[idx].full_path);
                if (h == 0) {
                    continue;
                }
                by_hash[h].push_back(idx);
            }
            auto hash_groups = GroupsFromMap(by_hash);
            for (auto& g : hash_groups) {
                const size_t first_idx = g.front();
                const std::uintmax_t sz = files[first_idx].size;
                groups.push_back(DuplicateGroup{ std::move(g), sz });
            }
        }
    } else {  // ExactHashNameExt
        for (const auto& base_group : size_groups) {
            std::unordered_map<std::wstring, std::vector<size_t>> by_name_ext;
            by_name_ext.reserve(base_group.size());
            for (size_t idx : base_group) {
                std::wstring key = files[idx].name + L"|" + ExtensionLower(files[idx].full_path);
                by_name_ext[std::move(key)].push_back(idx);
            }
            for (const auto& kv : by_name_ext) {
                if (kv.second.size() < 2) {
                    continue;
                }
                std::unordered_map<std::uint64_t, std::vector<size_t>> by_hash;
                by_hash.reserve(kv.second.size());
                for (size_t idx : kv.second) {
                    const std::uint64_t h = Fnv1aFile64(files[idx].full_path);
                    if (h == 0) {
                        continue;
                    }
                    by_hash[h].push_back(idx);
                }
                auto hash_groups = GroupsFromMap(by_hash);
                for (auto& g : hash_groups) {
                    const size_t first_idx = g.front();
                    const std::uintmax_t sz = files[first_idx].size;
                    groups.push_back(DuplicateGroup{ std::move(g), sz });
                }
            }
        }
    }

    std::sort(groups.begin(), groups.end(), [](const DuplicateGroup& a, const DuplicateGroup& b) {
        if (a.size != b.size) {
            return a.size > b.size;
        }
        return a.indices.size() > b.indices.size();
    });

    DuplicateAnalysisResult result{};
    result.groups = std::move(groups);
    for (const auto& g : result.groups) {
        result.duplicate_file_count += g.indices.size();
        if (g.indices.size() > 1) {
            result.reclaimable_bytes += static_cast<std::uintmax_t>(g.indices.size() - 1) * g.size;
        }
    }
    return result;
}

}  // namespace fme
