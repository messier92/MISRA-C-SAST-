#include "ProjectFileFilter.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <unordered_map>

namespace misra {

ProjectFileFilter::ProjectFileFilter(const std::string& root)
    : root_(root.empty() ? "" : normalisePath(root)) {}

std::string ProjectFileFilter::canonicalisePath(const std::string& path) {
    if (path.empty()) return "";

    static std::unordered_map<std::string, std::string> cache;
    auto it = cache.find(path);
    if (it != cache.end()) {
        return it->second;
    }

    std::error_code ec;
    std::filesystem::path p(path);
    p.make_preferred();

    std::filesystem::path canon;
    if (std::filesystem::exists(p, ec)) {
        ec.clear();
        canon = std::filesystem::weakly_canonical(p, ec);
        if (ec || canon.empty()) {
            ec.clear();
            canon = std::filesystem::absolute(p, ec).make_preferred().lexically_normal();
        } else {
            canon = canon.make_preferred().lexically_normal();
        }
    } else {
        canon = p.lexically_normal();
    }

    std::string result = canon.string();
#ifdef _WIN32
    if (result.size() >= 2 && result[1] == ':') {
        result[0] = static_cast<char>(
            std::toupper(static_cast<unsigned char>(result[0])));
    }
#endif
    while (result.size() > 3 &&
           (result.back() == '\\' || result.back() == '/')) {
        result.pop_back();
    }

    cache.emplace(path, result);
    return result;
}

std::string ProjectFileFilter::normalisePath(const std::string& path) {
    if (path.empty()) return "";

    static std::unordered_map<std::string, std::string> normCache;
    auto it = normCache.find(path);
    if (it != normCache.end()) {
        return it->second;
    }

    std::string result = canonicalisePath(path);
    // Convert backslashes to forward slashes for consistency.
    std::replace(result.begin(), result.end(), '\\', '/');
    // Lowercase on Windows for case-insensitive comparison.
#ifdef _WIN32
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
#endif
    // Remove trailing slash.
    if (!result.empty() && result.back() == '/') {
        result.pop_back();
    }

    normCache.emplace(path, result);
    return result;
}

bool ProjectFileFilter::isProjectFile(const std::string& filePath) const {
    if (root_.empty()) {
        return true; // Filter disabled.
    }
    std::string normalised = normalisePath(filePath);
    // File must start with root path followed by a separator.
    if (normalised.size() <= root_.size()) {
        return normalised == root_;
    }
    return normalised.substr(0, root_.size()) == root_ &&
           normalised[root_.size()] == '/';
}

} // namespace misra
