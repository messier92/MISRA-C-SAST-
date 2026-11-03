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

    thread_local std::unordered_map<std::string, std::string> cache;
    auto it = cache.find(path);
    if (it != cache.end()) {
        return it->second;
    }

    std::error_code ec;
    std::filesystem::path p(path);
    std::filesystem::path canon;
    if (std::filesystem::exists(p, ec)) {
        ec.clear();
        canon = std::filesystem::weakly_canonical(p, ec);
        if (ec || canon.empty()) {
            ec.clear();
            canon = std::filesystem::absolute(p, ec).lexically_normal();
        } else {
            canon = canon.lexically_normal();
        }
    } else if (p.has_parent_path()) {
        ec.clear();
        if (p.is_absolute()) {
            canon = std::filesystem::weakly_canonical(p, ec);
            if (ec || canon.empty()) {
                canon = p.lexically_normal();
            } else {
                canon = canon.lexically_normal();
            }
        } else {
            canon = p.lexically_normal();
        }
    } else {
        canon = p.lexically_normal();
    }

    std::string result = canon.make_preferred().string();
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

    thread_local std::unordered_map<std::string, std::string> normCache;
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

bool ProjectFileFilter::isExcludedExternalOrThirdPartyPath(const std::string& path) {
    if (path.empty()) return true;
    if (path.front() == '<') return true; // <built-in>, <command line>, <scratch space>

    std::string norm = normalisePath(path);
    if (norm.empty() || norm.front() == '<') return true;

    // Pad with leading and trailing '/' for exact directory segment matching.
    std::string padded = "/" + norm + "/";

    static const char* const kExcludedSegments[] = {
        // Compiler builtin & LLVM/Clang internal headers
        "/lib/clang/",
        "/llvm/include/",
        "/clang/include/",
        // MSVC toolchain directories
        "/microsoft visual studio/",
        "/vc/tools/msvc/",
        "/vc/auxiliary/",
        "/vc/include/",
        "/atlmfc/",
        // Windows SDK directories
        "/windows kits/",
        "/microsoft sdks/",
        "/ucrt/",
        // Qt installation directories & modules
        "/qt/",
        "/qt5/",
        "/qt6/",
        "/qtcore/",
        "/qtgui/",
        "/qtwidgets/",
        "/qtnetwork/",
        "/qtqml/",
        "/qtquick/",
        "/qtsql/",
        "/qtxml/",
        "/qttest/",
        "/qtconcurrent/",
        "/qtmultimedia/",
        "/qtdbus/",
        "/qtopengl/",
        "/qtprintsupport/",
        "/qtsvg/",
        "/qtwebsockets/",
        "/qtserialport/",
        "/qtpositioning/",
        "/qtbluetooth/",
        "/qtwebengine/",
        "/qtcharts/",
        "/mkspecs/",
        "/include/qt/",
        "/include/qt5/",
        "/include/qt6/",
        // Third-party dependencies & package managers
        "/third_party/",
        "/thirdparty/",
        "/3rdparty/",
        "/3rd_party/",
        "/external/",
        "/extern/",
        "/vendor/",
        "/vendors/",
        "/deps/",
        "/dependencies/",
        "/vcpkg/",
        "/vcpkg_installed/",
        "/conan/",
        "/.conan/",
        "/node_modules/",
        "/boost/",
        "/googletest/",
        "/gtest/",
        "/gmock/",
        "/catch2/",
        "/nlohmann/",
        "/eigen/",
        "/eigen3/",
        "/fmt/",
        "/spdlog/",
        "/protobuf/",
        "/abseil/",
        "/CMakeFiles/",
        "/cmakefiles/"
    };

    for (const char* seg : kExcludedSegments) {
        if (padded.find(seg) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool ProjectFileFilter::isProjectFile(const std::string& filePath) const {
    if (filePath.empty()) return false;
    if (isExcludedExternalOrThirdPartyPath(filePath)) {
        return false;
    }
    if (root_.empty()) {
        return true; // Root filter disabled; external/third-party check above still applies.
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
