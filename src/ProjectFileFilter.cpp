#include "ProjectFileFilter.h"
#include <algorithm>
#include <cctype>

namespace misra {

ProjectFileFilter::ProjectFileFilter(const std::string& root)
    : root_(normalisePath(root)) {}

std::string ProjectFileFilter::normalisePath(const std::string& path) {
    std::string result = path;
    // Convert backslashes to forward slashes for consistency.
    std::replace(result.begin(), result.end(), '\\', '/');
    // Lowercase on Windows for case-insensitive comparison.
#ifdef _WIN32
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
#endif
    // Remove trailing slash.
    if (!result.empty() && result.back() == '/') {
        result.pop_back();
    }
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
