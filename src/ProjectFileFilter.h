#ifndef MISRA_PROJECT_FILE_FILTER_H
#define MISRA_PROJECT_FILE_FILTER_H

#include <string>

namespace misra {

/// Filters source locations to include only files under the project root.
/// System headers, third-party includes, and compiler-generated locations
/// are excluded from violation reports.
class ProjectFileFilter {
public:
    /// Constructs a filter rooted at the given directory.
    /// If root is empty, the filter is disabled (all files pass).
    explicit ProjectFileFilter(const std::string& root = "");

    /// Returns true if the given file path is under the project root.
    bool isProjectFile(const std::string& filePath) const;

    /// Resolves relative segments (`.`, `..`), symlinks, and slash direction
    /// to produce a single canonical path string for a physical file on disk.
    static std::string canonicalisePath(const std::string& path);

    /// Normalizes a file path (canonicalizes, resolves separators to '/',
    /// lowercases on Windows) for case-insensitive path comparison and keying.
    static std::string normalisePath(const std::string& path);

    /// Returns the normalized root path.
    const std::string& getRoot() const { return root_; }

private:
    std::string root_;
};

} // namespace misra

#endif // MISRA_PROJECT_FILE_FILTER_H
