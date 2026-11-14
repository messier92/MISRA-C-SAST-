#include "ViolationStore.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace misra {

namespace {

namespace fs = std::filesystem;

/// Extract the numeric MISRA rule group from a rule ID (e.g. "MISRA-15-3-5" -> 15).
int extractRuleGroup(const std::string& ruleId) {
    size_t i = 0;
    while (i < ruleId.size() && !std::isdigit(static_cast<unsigned char>(ruleId[i]))) {
        ++i;
    }
    if (i >= ruleId.size()) return -1;

    int group = 0;
    while (i < ruleId.size() && std::isdigit(static_cast<unsigned char>(ruleId[i]))) {
        group = group * 10 + (ruleId[i] - '0');
        ++i;
    }
    return group;
}

/// Returns true if `ruleId` is a Rule 0-1-x rule (e.g. "MISRA-0-1-1").
bool isRule01x(const std::string& ruleId) {
    return extractRuleGroup(ruleId) == 0 &&
           ruleId.find("0-1-") != std::string::npos;
}

/// Returns true if `ruleId` is Rule 6-6-5 (e.g. "MISRA-6-6-5").
bool isRule665(const std::string& ruleId) {
    return extractRuleGroup(ruleId) == 6 &&
           ruleId.find("6-6-5") != std::string::npos;
}

bool isIdentStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool isIdentChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

/// Extract the first single-quoted identifier from a diagnostic message,
/// e.g. "Variable 'foo' is ..." -> "foo".
std::string extractQuotedIdentifier(const std::string& msg) {
    size_t first = msg.find('\'');
    if (first == std::string::npos) return "";
    size_t second = msg.find('\'', first + 1);
    if (second == std::string::npos || second <= first + 1) return "";
    std::string candidate = msg.substr(first + 1, second - first - 1);
    // Avoid returning operator symbols like '!' or '&&'.
    if (!candidate.empty() && !isIdentStart(candidate[0])) {
        return "";
    }
    return candidate;
}

/// Read all lines of a file on disk (1-indexed via vector index + 1).
const std::vector<std::string>& getFileLinesCached(const std::string& filePath) {
    static std::mutex cacheMutex;
    static std::unordered_map<std::string, std::vector<std::string>> cache;

    std::string norm = ProjectFileFilter::normalisePath(filePath);
    std::lock_guard<std::mutex> lock(cacheMutex);
    auto it = cache.find(norm);
    if (it != cache.end()) {
        return it->second;
    }

    std::vector<std::string>& lines = cache[norm];
    std::ifstream ifs(filePath);
    if (ifs.is_open()) {
        std::string line;
        while (std::getline(ifs, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            lines.push_back(std::move(line));
        }
    }
    return lines;
}

/// Strip comments and string/character literals from C++ source code while
/// preserving line numbers (newlines are kept intact). String literals inside
/// Qt `SIGNAL(...)` / `SLOT(...)` macros keep their inner text so signal/slot
/// identifiers remain visible.
std::string stripCommentsAndStringsPreserveLines(const std::string& src) {
    std::string out(src.size(), ' ');
    size_t n = src.size();
    size_t i = 0;
    while (i < n) {
        if (src[i] == '\n' || src[i] == '\r') {
            out[i] = src[i];
            ++i;
            continue;
        }
        // Line comment.
        if (i + 1 < n && src[i] == '/' && src[i + 1] == '/') {
            while (i < n && src[i] != '\n' && src[i] != '\r') {
                ++i;
            }
            continue;
        }
        // Block comment.
        if (i + 1 < n && src[i] == '/' && src[i + 1] == '*') {
            i += 2;
            while (i < n) {
                if (i + 1 < n && src[i] == '*' && src[i + 1] == '/') {
                    i += 2;
                    break;
                }
                if (src[i] == '\n' || src[i] == '\r') {
                    out[i] = src[i];
                }
                ++i;
            }
            continue;
        }
        // String literal.
        if (src[i] == '"') {
            ++i;
            while (i < n && src[i] != '"') {
                if (src[i] == '\\' && i + 1 < n) {
                    if (src[i + 1] == '\n' || src[i + 1] == '\r') {
                        out[i + 1] = src[i + 1];
                    }
                    i += 2;
                    continue;
                }
                if (src[i] == '\n' || src[i] == '\r') {
                    out[i] = src[i];
                }
                ++i;
            }
            if (i < n) ++i;
            continue;
        }
        // Character literal.
        if (src[i] == '\'') {
            ++i;
            while (i < n && src[i] != '\'') {
                if (src[i] == '\\' && i + 1 < n) {
                    i += 2;
                    continue;
                }
                if (src[i] == '\n' || src[i] == '\r') {
                    out[i] = src[i];
                }
                ++i;
            }
            if (i < n) ++i;
            continue;
        }
        out[i] = src[i];
        ++i;
    }
    return out;
}

/// Blank out constructor member-initializer targets `: memberName(...)` and
/// `, memberName(...)` (between `)` of a constructor parameter list and `{` of
/// the constructor body) while preserving arguments inside `(...)` / `{...}`
/// and preserving line numbers.
void blankConstructorInitializerTargets(std::string& cleanSrc) {
    size_t n = cleanSrc.size();
    size_t i = 0;
    while (i < n) {
        if (cleanSrc[i] == ')') {
            size_t j = i + 1;
            // Skip whitespace and optional `const` / `noexcept`.
            while (j < n && std::isspace(static_cast<unsigned char>(cleanSrc[j]))) ++j;
            // Look for single ':' (not '::').
            if (j < n && cleanSrc[j] == ':' &&
                (j + 1 >= n || cleanSrc[j + 1] != ':') &&
                (j == 0 || cleanSrc[j - 1] != ':')) {
                ++j; // past ':'
                // Scan constructor initializer list until '{' or ';' at parenDepth 0.
                while (j < n) {
                    while (j < n && std::isspace(static_cast<unsigned char>(cleanSrc[j]))) ++j;
                    if (j >= n || cleanSrc[j] == '{' || cleanSrc[j] == ';') break;

                    // Read target identifier (or qualified Base::Name) before '(' or '{'.
                    size_t idStart = j;
                    while (j < n && (isIdentChar(cleanSrc[j]) || cleanSrc[j] == ':')) {
                        ++j;
                    }
                    size_t idEnd = j;
                    while (j < n && std::isspace(static_cast<unsigned char>(cleanSrc[j]))) ++j;

                    if (j < n && (cleanSrc[j] == '(' || cleanSrc[j] == '{')) {
                        // Blank out the target member/base name so `: m_unused(0)` is
                        // not treated as reading or writing `m_unused`.
                        for (size_t k = idStart; k < idEnd; ++k) {
                            if (cleanSrc[k] != '\n' && cleanSrc[k] != '\r') {
                                cleanSrc[k] = ' ';
                            }
                        }
                        char openCh = cleanSrc[j];
                        char closeCh = (openCh == '(') ? ')' : '}';
                        int depth = 1;
                        ++j;
                        while (j < n && depth > 0) {
                            if (cleanSrc[j] == openCh) ++depth;
                            else if (cleanSrc[j] == closeCh) --depth;
                            ++j;
                        }
                        while (j < n && std::isspace(static_cast<unsigned char>(cleanSrc[j]))) ++j;
                        if (j < n && cleanSrc[j] == ',') {
                            ++j;
                            continue;
                        }
                    } else {
                        break;
                    }
                }
                i = j;
                continue;
            }
        }
        ++i;
    }
}

/// Returns true if `line` contains `emit <ident>(` or `Q_EMIT <ident>(`.
/// If `outSignalName` is non-null, populates the emitted signal identifier.
bool isQtSignalEmissionLine(const std::string& line, std::string* outSignalName = nullptr) {
    size_t n = line.size();
    for (size_t i = 0; i < n;) {
        if (isIdentStart(line[i])) {
            size_t start = i;
            while (i < n && isIdentChar(line[i])) ++i;
            std::string kw = line.substr(start, i - start);
            if (kw == "emit" || kw == "Q_EMIT") {
                size_t j = i;
                while (j < n && std::isspace(static_cast<unsigned char>(line[j]))) ++j;
                if (j == i) continue; // must have whitespace after emit

                // Skip optional `this->`.
                if (j + 4 <= n && line.compare(j, 4, "this") == 0 &&
                    (j + 4 == n || !isIdentChar(line[j + 4]))) {
                    size_t k = j + 4;
                    while (k < n && std::isspace(static_cast<unsigned char>(line[k]))) ++k;
                    if (k + 2 <= n && line[k] == '-' && line[k + 1] == '>') {
                        j = k + 2;
                        while (j < n && std::isspace(static_cast<unsigned char>(line[j]))) ++j;
                    }
                }

                // Read identifier (and skip optional `ClassName::` qualifier).
                while (j < n && isIdentStart(line[j])) {
                    size_t idStart = j;
                    while (j < n && isIdentChar(line[j])) ++j;
                    std::string ident = line.substr(idStart, j - idStart);
                    size_t k = j;
                    while (k < n && std::isspace(static_cast<unsigned char>(line[k]))) ++k;
                    if (k + 2 <= n && line[k] == ':' && line[k + 1] == ':') {
                        j = k + 2;
                        while (j < n && std::isspace(static_cast<unsigned char>(line[j]))) ++j;
                        continue;
                    }
                    if (k < n && line[k] == '(') {
                        if (outSignalName) *outSignalName = ident;
                        return true;
                    }
                    break;
                }
            }
        } else {
            ++i;
        }
    }
    return false;
}

/// Collect all project source and header files under `projectRoot` and from `scannedFiles`.
std::vector<std::string> collectProjectFiles(
    const std::string& projectRoot,
    const std::vector<std::string>& scannedFiles,
    const std::string& extraFile = "") {
    std::vector<std::string> result;
    std::set<std::string> seenNorm;

    auto addFile = [&](const std::string& p) {
        if (p.empty()) return;
        std::error_code ec;
        if (!fs::exists(p, ec) || !fs::is_regular_file(p, ec)) return;
        if (ProjectFileFilter::isExcludedExternalOrThirdPartyPath(p)) return;
        std::string canon = ProjectFileFilter::canonicalisePath(p);
        std::string norm = ProjectFileFilter::normalisePath(canon);
        if (!canon.empty() && seenNorm.insert(norm).second) {
            result.push_back(std::move(canon));
        }
    };

    for (const auto& f : scannedFiles) {
        addFile(f);
        fs::path dir = fs::path(f).parent_path();
        std::error_code ec;
        if (fs::exists(dir, ec) && fs::is_directory(dir, ec)) {
            for (const auto& entry : fs::directory_iterator(dir, ec)) {
                if (entry.is_regular_file(ec)) {
                    auto ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (ext == ".cpp" || ext == ".c" || ext == ".cc" || ext == ".cxx" ||
                        ext == ".h" || ext == ".hpp" || ext == ".hh" || ext == ".hxx") {
                        addFile(entry.path().string());
                    }
                }
            }
        }
    }

    if (!extraFile.empty()) {
        addFile(extraFile);
        fs::path dir = fs::path(extraFile).parent_path();
        std::error_code ec;
        if (fs::exists(dir, ec) && fs::is_directory(dir, ec)) {
            for (const auto& entry : fs::directory_iterator(dir, ec)) {
                if (entry.is_regular_file(ec)) {
                    auto ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (ext == ".cpp" || ext == ".c" || ext == ".cc" || ext == ".cxx" ||
                        ext == ".h" || ext == ".hpp" || ext == ".hh" || ext == ".hxx") {
                        addFile(entry.path().string());
                    }
                }
            }
        }
    }

    if (!projectRoot.empty()) {
        std::error_code ec;
        if (fs::exists(projectRoot, ec) && fs::is_directory(projectRoot, ec)) {
            for (auto it = fs::recursive_directory_iterator(
                     projectRoot, fs::directory_options::skip_permission_denied, ec);
                 it != fs::recursive_directory_iterator();
                 it.increment(ec)) {
                if (ec) {
                    ec.clear();
                    continue;
                }
                if (!it->is_regular_file(ec)) continue;
                auto ext = it->path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (ext == ".cpp" || ext == ".c" || ext == ".cc" || ext == ".cxx" ||
                    ext == ".h" || ext == ".hpp" || ext == ".hh" || ext == ".hxx") {
                    addFile(it->path().string());
                }
            }
        }
    }

    return result;
}

/// Extract all Qt signal names declared under `signals:` / `Q_SIGNALS:` or
/// emitted via `emit <name>(` / `Q_EMIT <name>(` / `SIGNAL(<name>(...))` in `files`.
std::unordered_set<std::string> collectProjectSignalNames(
    const std::vector<std::string>& files) {
    std::unordered_set<std::string> signalsSet;

    for (const auto& f : files) {
        const auto& lines = getFileLinesCached(f);
        bool inSignalsBlock = false;
        for (const auto& line : lines) {
            std::string sigName;
            if (isQtSignalEmissionLine(line, &sigName) && !sigName.empty()) {
                signalsSet.insert(sigName);
            }

            // Check `SIGNAL(sigName(...)`
            size_t sigMacroPos = line.find("SIGNAL");
            if (sigMacroPos != std::string::npos &&
                (sigMacroPos == 0 || !isIdentChar(line[sigMacroPos - 1]))) {
                size_t j = sigMacroPos + 6;
                while (j < line.size() && std::isspace(static_cast<unsigned char>(line[j]))) ++j;
                if (j < line.size() && line[j] == '(') {
                    ++j;
                    while (j < line.size() && std::isspace(static_cast<unsigned char>(line[j]))) ++j;
                    if (j < line.size() && isIdentStart(line[j])) {
                        size_t idStart = j;
                        while (j < line.size() && isIdentChar(line[j])) ++j;
                        signalsSet.insert(line.substr(idStart, j - idStart));
                    }
                }
            }

            // Trim leading whitespace.
            size_t first = line.find_first_not_of(" \t");
            if (first == std::string::npos) continue;
            std::string trimmed = line.substr(first);

            if (trimmed.rfind("signals:", 0) == 0 ||
                trimmed.rfind("Q_SIGNALS:", 0) == 0 ||
                trimmed.rfind("signals :", 0) == 0 ||
                trimmed.rfind("Q_SIGNALS :", 0) == 0 ||
                trimmed.rfind("public signals:", 0) == 0 ||
                trimmed.rfind("public Q_SIGNALS:", 0) == 0) {
                inSignalsBlock = true;
                continue;
            }
            if (inSignalsBlock) {
                if (trimmed.rfind("public:", 0) == 0 ||
                    trimmed.rfind("protected:", 0) == 0 ||
                    trimmed.rfind("private:", 0) == 0 ||
                    trimmed.rfind("public slots:", 0) == 0 ||
                    trimmed.rfind("protected slots:", 0) == 0 ||
                    trimmed.rfind("private slots:", 0) == 0 ||
                    trimmed.rfind("public Q_SLOTS:", 0) == 0 ||
                    trimmed.rfind("protected Q_SLOTS:", 0) == 0 ||
                    trimmed.rfind("private Q_SLOTS:", 0) == 0 ||
                    trimmed.rfind("};", 0) == 0) {
                    inSignalsBlock = false;
                    continue;
                }
                // Extract function name immediately preceding '(' in signal declaration.
                size_t parenPos = trimmed.find('(');
                if (parenPos != std::string::npos && parenPos > 0) {
                    size_t endId = parenPos;
                    while (endId > 0 && std::isspace(static_cast<unsigned char>(trimmed[endId - 1]))) {
                        --endId;
                    }
                    size_t startId = endId;
                    while (startId > 0 && isIdentChar(trimmed[startId - 1])) {
                        --startId;
                    }
                    if (startId < endId && isIdentStart(trimmed[startId])) {
                        std::string fnName = trimmed.substr(startId, endId - startId);
                        if (fnName != "void" && fnName != "bool" && fnName != "int") {
                            signalsSet.insert(fnName);
                        }
                    }
                }
            }
        }
    }
    return signalsSet;
}

/// Returns true if `file` (at `declLine`) declares a member inside a Q_OBJECT
/// derived class or Qt class, or inside a header file (.h/.hpp).
bool isQObjectDerivedOrHeaderMember(const std::string& file, uint32_t declLine) {
    fs::path p(file);
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    bool isHeader = (ext == ".h" || ext == ".hpp" || ext == ".hh" || ext == ".hxx");

    const auto& lines = getFileLinesCached(file);
    if (lines.empty()) {
        return isHeader;
    }

    static const std::unordered_set<std::string> kQtMarkers = {
        "Q_OBJECT", "Q_GADGET", "Q_SIGNALS", "Q_SLOTS", "signals", "slots",
        "Q_PROPERTY", "Q_INVOKABLE", "QObject", "QWidget", "QMainWindow",
        "QDialog", "QThread", "QFrame"
    };

    for (const auto& line : lines) {
        size_t n = line.size();
        for (size_t i = 0; i < n;) {
            if (isIdentStart(line[i])) {
                size_t start = i;
                while (i < n && isIdentChar(line[i])) ++i;
                std::string tok = line.substr(start, i - start);
                if (kQtMarkers.count(tok) > 0) {
                    return true;
                }
                if (tok.size() >= 2 && tok[0] == 'Q' &&
                    std::isupper(static_cast<unsigned char>(tok[1]))) {
                    return true;
                }
            } else {
                ++i;
            }
        }
    }
    (void)declLine;
    return isHeader;
}

/// Verify whether member variable `memberName` (declared at `declFile:declLine`)
/// is actually used in any `.cpp`/`.cc`/`.cxx` implementation file (inside a
/// method, slot, constructor body, or QObject macro, excluding its own
/// declaration and excluding constructor member-initializer targets `: member(...)`).
/// If used, returns true and sets `outUsedAt` to `"<cppFile>:<line>"`.
bool isMemberUsedInCppFiles(const std::string& memberName,
                            const std::string& declFile,
                            uint32_t declLine,
                            const std::vector<std::string>& projectFiles,
                            std::string* outUsedAt = nullptr) {
    if (memberName.empty()) return false;

    std::string normDeclFile = ProjectFileFilter::normalisePath(declFile);

    for (const auto& f : projectFiles) {
        fs::path p(f);
        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        bool isCpp = (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c");
        bool isHeader = (ext == ".h" || ext == ".hpp" || ext == ".hh" || ext == ".hxx");
        if (!isCpp && !isHeader) continue;

        const auto& rawLines = getFileLinesCached(f);
        if (rawLines.empty()) continue;

        // Also check Q_PROPERTY(... MEMBER memberName ...) in headers or sources.
        for (size_t lineIdx = 0; lineIdx < rawLines.size(); ++lineIdx) {
            const std::string& raw = rawLines[lineIdx];
            if (raw.find("Q_PROPERTY") != std::string::npos ||
                raw.find("connect") != std::string::npos) {
                // Check whole-word match of `memberName`.
                size_t pos = 0;
                while ((pos = raw.find(memberName, pos)) != std::string::npos) {
                    bool leftOk = (pos == 0 || !isIdentChar(raw[pos - 1]));
                    bool rightOk = (pos + memberName.size() >= raw.size() ||
                                    !isIdentChar(raw[pos + memberName.size()]));
                    if (leftOk && rightOk) {
                        if (outUsedAt) {
                            *outUsedAt = fs::path(f).filename().string() + ":" +
                                         std::to_string(lineIdx + 1);
                        }
                        return true;
                    }
                    pos += memberName.size();
                }
            }
        }

        if (!isCpp) continue;

        // Reconstruct full file text to strip comments, string literals, and
        // constructor initializer targets `: memberName(...)` cleanly.
        std::ostringstream oss;
        for (size_t idx = 0; idx < rawLines.size(); ++idx) {
            oss << rawLines[idx] << "\n";
        }
        std::string clean = stripCommentsAndStringsPreserveLines(oss.str());
        blankConstructorInitializerTargets(clean);

        std::istringstream iss(clean);
        std::string cleanLine;
        uint32_t lineNo = 0;
        std::string normF = ProjectFileFilter::normalisePath(f);

        while (std::getline(iss, cleanLine)) {
            ++lineNo;
            if (normF == normDeclFile && lineNo == declLine) {
                continue;
            }
            // Skip preprocessor directives.
            size_t firstNonSpace = cleanLine.find_first_not_of(" \t");
            if (firstNonSpace == std::string::npos || cleanLine[firstNonSpace] == '#') {
                continue;
            }

            size_t pos = 0;
            while ((pos = cleanLine.find(memberName, pos)) != std::string::npos) {
                bool leftOk = (pos == 0 || !isIdentChar(cleanLine[pos - 1]));
                bool rightOk = (pos + memberName.size() >= cleanLine.size() ||
                                !isIdentChar(cleanLine[pos + memberName.size()]));
                if (leftOk && rightOk) {
                    if (outUsedAt) {
                        *outUsedAt = fs::path(f).filename().string() + ":" +
                                     std::to_string(lineNo);
                    }
                    return true;
                }
                pos += memberName.size();
            }
        }
    }
    return false;
}

/// Returns true if `line` (or the expression at `!`) is a Qt idiom / Qt API
/// expression guaranteed to convert to `bool` under MISRA 5-3-1.
bool isQtBooleanIdiomLine(const std::string& line,
                          const std::string& file,
                          const std::vector<std::string>& projectFiles,
                          std::string* outExprDesc = nullptr) {
    if (line.empty()) return false;

    static const std::unordered_set<std::string> kQtBoolMethods = {
        "isEmpty", "isNull", "isValid", "isOpen", "isReadable", "isWritable",
        "isExecutable", "isDir", "isFile", "isRelative", "isAbsolute", "isRoot",
        "isSymLink", "isHidden", "isBundle", "isActive", "isRunning", "isFinished",
        "isInterruptionRequested", "isSignalConnected", "isWidgetType", "isWindowType",
        "isEnabled", "isVisible", "isChecked", "isDown", "isModified", "isReadOnly",
        "isModal", "isMinimized", "isMaximized", "isFullScreen", "isSequential",
        "isTextModeEnabled", "isTransaction", "isSelect", "isForwardOnly",
        "open", "exists", "remove", "rename", "copy", "link", "mkdir", "mkpath",
        "rmdir", "rmpath", "cd", "cdUp", "flush", "reset", "seek", "atEnd",
        "canReadLine", "waitForConnected", "waitForReadyRead", "waitForBytesWritten",
        "waitForDisconnected", "waitForStarted", "waitForFinished",
        "contains", "startsWith", "endsWith", "compare", "exactMatch",
        "hasMatch", "hasPartialMatch", "hasNext", "hasPrevious", "hasError",
        "hasPendingConnections", "hasPendingDatagrams", "hasFormat", "hasUrls",
        "hasText", "hasImage", "hasHtml", "hasColor", "hasFocus",
        "hasAcceptableInput", "hasSelectedText", "hasSelection", "hasChildren",
        "hasIndex", "exec", "prepare", "next", "previous", "first", "last",
        "transaction", "commit", "rollback", "load", "save", "loadFromData",
        "setContent", "parse", "validate", "bind", "listen", "setData",
        "insertRow", "insertColumn", "removeRow", "removeColumn", "moveRow",
        "canFetchMore", "canConvert", "convert", "toBool", "testFlag",
        "connect", "disconnect", "invokeMethod",
        "qEnvironmentVariableIsEmpty", "qEnvironmentVariableIsSet",
        "qFuzzyCompare", "qFuzzyIsNull", "qIsNull", "qIsInf", "qIsNaN", "qIsFinite"
    };

    // 1. Scan identifiers followed by '(' on the line.
    size_t n = line.size();
    for (size_t i = 0; i < n;) {
        if (isIdentChar(line[i]) && !std::isdigit(static_cast<unsigned char>(line[i]))) {
            size_t start = i;
            while (i < n && isIdentChar(line[i])) ++i;
            std::string ident = line.substr(start, i - start);
            size_t j = i;
            while (j < n && std::isspace(static_cast<unsigned char>(line[j]))) ++j;
            if (j < n && line[j] == '(') {
                if (kQtBoolMethods.count(ident) > 0) {
                    if (outExprDesc) *outExprDesc = ident + "(...)";
                    return true;
                }
            }
        } else {
            ++i;
        }
    }

    // 2. Scan operands of '!' on the line.
    for (size_t bangPos = line.find('!'); bangPos != std::string::npos;
         bangPos = line.find('!', bangPos + 1)) {
        if (bangPos + 1 < n && line[bangPos + 1] == '=') continue; // skip '!='
        size_t j = bangPos + 1;
        while (j < n && (std::isspace(static_cast<unsigned char>(line[j])) || line[j] == '(')) {
            ++j;
        }
        // Skip optional `this->`.
        if (j + 6 <= n && line.compare(j, 4, "this") == 0) {
            size_t k = j + 4;
            while (k < n && std::isspace(static_cast<unsigned char>(line[k]))) ++k;
            if (k + 2 <= n && line[k] == '-' && line[k + 1] == '>') {
                j = k + 2;
                while (j < n && std::isspace(static_cast<unsigned char>(line[j]))) ++j;
            }
        }
        if (j < n && isIdentChar(line[j]) && !std::isdigit(static_cast<unsigned char>(line[j]))) {
            size_t idStart = j;
            while (j < n && isIdentChar(line[j])) ++j;
            std::string ident = line.substr(idStart, j - idStart);

            // Direct Qt class/type/function name after `!` (e.g. `!QVariant(...)`, `!Qt::...`, `!qFuzzyCompare(...)`).
            if ((ident.size() >= 2 && (ident[0] == 'Q' || ident[0] == 'q') &&
                 std::isupper(static_cast<unsigned char>(ident[1]))) ||
                ident == "Qt") {
                if (outExprDesc) *outExprDesc = "!" + ident;
                return true;
            }

            // Check if `ident` is declared with a Qt type `Q[A-Z]...` in `file` or `projectFiles`.
            auto lineDeclaresQtIdent = [&](const std::string& srcLine) -> bool {
                size_t idPos = srcLine.find(ident);
                if (idPos == std::string::npos) return false;
                bool leftOk = (idPos == 0 || !isIdentChar(srcLine[idPos - 1]));
                bool rightOk = (idPos + ident.size() >= srcLine.size() ||
                                !isIdentChar(srcLine[idPos + ident.size()]));
                if (!leftOk || !rightOk) return false;
                for (size_t p = 0; p < idPos; ++p) {
                    if (srcLine[p] == 'Q' && p + 1 < idPos &&
                        std::isupper(static_cast<unsigned char>(srcLine[p + 1])) &&
                        (p == 0 || !isIdentChar(srcLine[p - 1]))) {
                        return true;
                    }
                }
                return false;
            };

            for (const auto& l : getFileLinesCached(file)) {
                if (lineDeclaresQtIdent(l)) {
                    if (outExprDesc) *outExprDesc = "!" + ident;
                    return true;
                }
            }
            for (const auto& pf : projectFiles) {
                for (const auto& l : getFileLinesCached(pf)) {
                    if (lineDeclaresQtIdent(l)) {
                        if (outExprDesc) *outExprDesc = "!" + ident;
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

} // namespace

// ── Violation ordering ─────────────────────────────────────────

bool Violation::operator<(const Violation& rhs) const {
    std::string normL = ProjectFileFilter::normalisePath(file);
    std::string normR = ProjectFileFilter::normalisePath(rhs.file);
    if (normL != normR) return normL < normR;
    if (line != rhs.line) return line < rhs.line;
    if (col  != rhs.col)  return col  < rhs.col;
    return ruleId < rhs.ruleId;
}

// ── ViolationStore ─────────────────────────────────────────────

ViolationStore::ViolationStore(const std::string& projectRoot)
    : rawProjectRoot_(projectRoot), filter_(projectRoot) {}

void ViolationStore::addViolation(const Violation& v) {
    Violation canonV = v;
    canonV.file = ProjectFileFilter::canonicalisePath(v.file);

    // Skip files outside the project root.
    if (!filter_.isProjectFile(canonV.file)) {
        return;
    }

    std::string normFile = ProjectFileFilter::normalisePath(canonV.file);

    std::lock_guard<std::mutex> lock(mutex_);
    // Deduplicate by canonical (file, line, ruleId), collapsing repeated
    // reports from headers included under different path spellings across TUs.
    for (auto& existing : violations_) {
        if (existing.line   == canonV.line   &&
            existing.ruleId == canonV.ruleId &&
            ProjectFileFilter::normalisePath(existing.file) == normFile) {
            if (canonV < existing) {
                existing = std::move(canonV);
            }
            return;
        }
    }
    violations_.push_back(std::move(canonV));
}

void ViolationStore::addExcludedViolation(const Violation& v,
                                          const std::string& justification) {
    Violation canonV = v;
    canonV.file = ProjectFileFilter::canonicalisePath(v.file);

    if (!filter_.isProjectFile(canonV.file)) {
        return;
    }

    std::string normFile = ProjectFileFilter::normalisePath(canonV.file);

    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& existing : excludedViolations_) {
        if (existing.violation.line   == canonV.line   &&
            existing.violation.ruleId == canonV.ruleId &&
            ProjectFileFilter::normalisePath(existing.violation.file) == normFile) {
            if (canonV < existing.violation) {
                existing.violation     = std::move(canonV);
                existing.justification = justification;
            }
            return;
        }
    }
    excludedViolations_.push_back(ExcludedViolation{std::move(canonV), justification});
}

void ViolationStore::registerRule(const std::string& ruleId) {
    std::lock_guard<std::mutex> lock(mutex_);
    registeredRules_.insert(ruleId);
}

void ViolationStore::setScannedFiles(const std::vector<std::string>& files) {
    std::lock_guard<std::mutex> lock(mutex_);
    scannedFiles_.clear();
    std::set<std::string> seen;
    for (const auto& f : files) {
        std::string canon = ProjectFileFilter::canonicalisePath(f);
        std::string norm  = ProjectFileFilter::normalisePath(canon);
        if (!canon.empty() && seen.insert(norm).second) {
            scannedFiles_.push_back(std::move(canon));
        }
    }
}

void ViolationStore::recordFileFailure(const std::string& file,
                                       const std::string& reason) {
    std::string canon = ProjectFileFilter::canonicalisePath(file);
    std::lock_guard<std::mutex> lock(mutex_);
    failedFiles_.emplace_back(canon.empty() ? file : canon, reason);
}

void ViolationStore::setReportTag(const std::string& tag) {
    std::lock_guard<std::mutex> lock(mutex_);
    reportTag_ = tag;
}

// ── False-positive evaluation ──────────────────────────────────

bool ViolationStore::isFalsePositiveViolation(
    const Violation& v,
    std::string* outJustification) const {
    std::string srcLine = v.sourceLine;
    if (srcLine.empty()) {
        const auto& fileLines = getFileLinesCached(v.file);
        if (v.line >= 1 && v.line <= fileLines.size()) {
            srcLine = fileLines[v.line - 1];
        }
    }

    // 1. Qt signal emissions: `emit signalName(args)` is NOT a variable use.
    //    `emit` is a Qt macro that triggers a signal. Ignore any finding about
    //    a signal name under MISRA-0-1-6 and MISRA-0-1-3.
    if (v.ruleId == "MISRA-0-1-6" || v.ruleId == "MISRA-0-1-3") {
        std::string ident = extractQuotedIdentifier(v.message);
        std::string emittedSignal;
        if (isQtSignalEmissionLine(srcLine, &emittedSignal)) {
            std::string sigLabel = !ident.empty() ? ident : emittedSignal;
            if (outJustification) {
                *outJustification =
                    "Excluded under " + v.ruleId +
                    ": Qt signal emission — 'emit " + sigLabel +
                    "(...)' is a Qt macro that triggers a signal, not a variable use.";
            }
            return true;
        }
        if (!ident.empty()) {
            std::vector<std::string> projFiles =
                collectProjectFiles(rawProjectRoot_, scannedFiles_, v.file);
            std::unordered_set<std::string> sigNames =
                collectProjectSignalNames(projFiles);
            if (sigNames.count(ident) > 0) {
                if (outJustification) {
                    *outJustification =
                        "Excluded under " + v.ruleId +
                        ": Identifier '" + ident +
                        "' is a Qt signal name triggered via Qt signal emission ('emit'), not a variable.";
                }
                return true;
            }
        }
    }

    // 2. Q_OBJECT member variables reported as unused under MISRA-0-1-3:
    //    Member variables of a Q_OBJECT derived class that are used inside slots
    //    or via QObject macros may be reported as "declared but never read or written".
    //    Before reporting, verify the member is not actually used in the .cpp files.
    if (v.ruleId == "MISRA-0-1-3" &&
        (v.message.find("Member variable '") != std::string::npos ||
         v.message.find("declared but never read or written") != std::string::npos)) {
        std::string memberName = extractQuotedIdentifier(v.message);
        if (!memberName.empty() && isQObjectDerivedOrHeaderMember(v.file, v.line)) {
            std::vector<std::string> projFiles =
                collectProjectFiles(rawProjectRoot_, scannedFiles_, v.file);
            std::string usedAt;
            if (isMemberUsedInCppFiles(memberName, v.file, v.line, projFiles, &usedAt)) {
                if (outJustification) {
                    *outJustification =
                        "Excluded under MISRA-0-1-3: Member variable '" + memberName +
                        "' of Q_OBJECT derived class was verified to be used in " +
                        usedAt + " (inside a slot, method, or QObject macro).";
                }
                return true;
            }
        }
    }

    // 3. "!" on non-bool Qt expressions under MISRA-5-3-1:
    //    Operand of logical '!' does not have type bool on Qt idioms is a false
    //    positive. Qt API methods used this way are guaranteed to convert to bool.
    if (v.ruleId == "MISRA-5-3-1") {
        std::vector<std::string> projFiles =
            collectProjectFiles(rawProjectRoot_, scannedFiles_, v.file);
        std::string exprDesc;
        if (isQtBooleanIdiomLine(srcLine, v.file, projFiles, &exprDesc)) {
            if (outJustification) {
                *outJustification =
                    "Excluded under MISRA-5-3-1: Operand of logical '!' on Qt idiom (" +
                    (exprDesc.empty() ? srcLine : exprDesc) +
                    ") is guaranteed by the Qt API to convert to bool.";
            }
            return true;
        }
    }

    return false;
}

void ViolationStore::evaluateExclusionsAndPrecedence(
    std::vector<Violation>& vec,
    std::vector<ExcludedViolation>& excluded) const {
    std::vector<Violation> confirmed;
    confirmed.reserve(vec.size());

    auto addExcludedUnique = [&](const Violation& v, const std::string& reason) {
        std::string normFile = ProjectFileFilter::normalisePath(v.file);
        for (auto& ex : excluded) {
            if (ex.violation.line   == v.line   &&
                ex.violation.ruleId == v.ruleId &&
                ProjectFileFilter::normalisePath(ex.violation.file) == normFile) {
                if (v < ex.violation) {
                    ex.violation     = v;
                    ex.justification = reason;
                }
                return;
            }
        }
        excluded.push_back(ExcludedViolation{v, reason});
    };

    for (const auto& v : vec) {
        std::string reason;
        if (isFalsePositiveViolation(v, &reason)) {
            addExcludedUnique(v, reason);
        } else {
            confirmed.push_back(v);
        }
    }

    vec = std::move(confirmed);
    resolvePrecedenceOnVector(vec);
    std::sort(excluded.begin(), excluded.end());
}

// ── Precedence resolution ──────────────────────────────────────

int ViolationStore::getRulePrecedenceRank(const std::string& ruleId) {
    // Precedence order from highest (1) to lowest (9):
    // 1. Rule 15
    // 2. Rule 5
    // 3. Rule 3
    // 4. Rule 16
    // 5. Rule 7
    // 6. Rule 9
    // 7. Rule 2
    // 8. Rule 6
    // 9. Rule 0
    switch (extractRuleGroup(ruleId)) {
        case 15: return 1;
        case 5:  return 2;
        case 3:  return 3;
        case 16: return 4;
        case 7:  return 5;
        case 9:  return 6;
        case 2:  return 7;
        case 6:  return 8;
        case 0:  return 9;
        default: return 10;
    }
}

bool ViolationStore::hasHigherPrecedence(const Violation& lhs, const Violation& rhs) {
    // Exception: Any Rule 0-1-x violation takes precedence over Rule 6-6-5.
    if (isRule01x(lhs.ruleId) && isRule665(rhs.ruleId)) return true;
    if (isRule665(lhs.ruleId) && isRule01x(rhs.ruleId)) return false;

    int lhsRank = getRulePrecedenceRank(lhs.ruleId);
    int rhsRank = getRulePrecedenceRank(rhs.ruleId);
    if (lhsRank != rhsRank) {
        return lhsRank < rhsRank;
    }
    return lhs < rhs;
}

void ViolationStore::resolvePrecedenceOnVector(std::vector<Violation>& vec) {
    if (vec.size() <= 1) {
        std::sort(vec.begin(), vec.end());
        return;
    }

    using LineKey = std::pair<std::string, uint32_t>;
    std::map<LineKey, std::vector<Violation>> byLine;

    for (const auto& v : vec) {
        LineKey key{ProjectFileFilter::normalisePath(v.file), v.line};
        byLine[key].push_back(v);
    }

    std::vector<Violation> resolved;
    resolved.reserve(byLine.size());

    for (auto& [key, group] : byLine) {
        if (group.size() == 1) {
            resolved.push_back(group.front());
            continue;
        }

        // Exception: If any Rule 0-1-x violation is present on this line,
        // suppress Rule 6-6-5 on this line.
        bool has01x = std::any_of(group.begin(), group.end(),
                                  [](const Violation& v) { return isRule01x(v.ruleId); });
        if (has01x) {
            group.erase(
                std::remove_if(group.begin(), group.end(),
                               [](const Violation& v) { return isRule665(v.ruleId); }),
                group.end());
        }

        auto bestIt = std::min_element(group.begin(), group.end(), hasHigherPrecedence);
        if (bestIt != group.end()) {
            resolved.push_back(*bestIt);
        }
    }

    std::sort(resolved.begin(), resolved.end());
    vec = std::move(resolved);
}

void ViolationStore::applyPrecedence() {
    std::lock_guard<std::mutex> lock(mutex_);
    evaluateExclusionsAndPrecedence(violations_, excludedViolations_);
}

// ── Report generation ──────────────────────────────────────────

void ViolationStore::printReportFor(
    std::ostream& os,
    const std::vector<Violation>& violations,
    const std::vector<ExcludedViolation>& excluded) const {
    // Header.
    os << "================================================================\n";
    os << "  MISRA C++:2008 Static Analysis Report\n";
    if (!reportTag_.empty()) {
        os << "  Tag: " << reportTag_ << "\n";
    }
    os << "================================================================\n\n";

    // Scanned files.
    if (!scannedFiles_.empty()) {
        os << "Scanned files (" << scannedFiles_.size() << "):\n";
        for (const auto& f : scannedFiles_) {
            os << "  " << f << "\n";
        }
        os << "\n";
    }

    // Failed or timed-out files (if any).
    if (!failedFiles_.empty()) {
        os << "Failed / Timed-out files (" << failedFiles_.size() << "):\n";
        for (const auto& [file, reason] : failedFiles_) {
            os << "  " << file << "  [" << reason << "]\n";
        }
        os << "\n";
    }

    // Section 1: Confirmed Findings (already sorted).
    if (violations.empty()) {
        os << "Confirmed Findings: No violations found.\n\n";
    } else {
        os << "Confirmed Findings — Violations (" << violations.size() << "):\n";
        os << std::string(72, '-') << "\n";
        for (const auto& v : violations) {
            os << v.file << ":" << v.line << ":" << v.col
               << "  [" << v.ruleId << "]  " << v.message << "\n";
            if (!v.sourceLine.empty()) {
                os << "    | " << v.sourceLine << "\n";
            }
        }
        os << std::string(72, '-') << "\n\n";
    }

    // Section 2: Excluded Findings (with rule number and justification for auditability).
    os << "Excluded Findings (" << excluded.size() << "):\n";
    os << std::string(72, '-') << "\n";
    if (excluded.empty()) {
        os << "  None.\n";
    } else {
        for (const auto& ex : excluded) {
            const auto& v = ex.violation;
            os << v.file << ":" << v.line << ":" << v.col
               << "  [" << v.ruleId << "]  " << v.message << "\n";
            if (!v.sourceLine.empty()) {
                os << "    | " << v.sourceLine << "\n";
            }
            os << "    -> Exclusion Justification [" << v.ruleId << "]: "
               << ex.justification << "\n";
        }
    }
    os << std::string(72, '-') << "\n\n";

    // Per-rule summary.
    // Count confirmed violations per rule (including registered rules with 0 hits).
    std::map<std::string, size_t> counts;
    for (const auto& ruleId : registeredRules_) {
        counts[ruleId] = 0;
    }
    for (const auto& v : violations) {
        counts[v.ruleId]++;
    }

    os << "Summary by rule:\n";
    for (const auto& [ruleId, count] : counts) {
        os << "  " << std::left << std::setw(20) << ruleId
           << count << " violation" << (count == 1 ? "" : "s") << "\n";
    }

    if (!excluded.empty()) {
        std::map<std::string, size_t> excludedCounts;
        for (const auto& ex : excluded) {
            excludedCounts[ex.violation.ruleId]++;
        }
        os << "\nExcluded summary by rule:\n";
        for (const auto& [ruleId, count] : excludedCounts) {
            os << "  " << std::left << std::setw(20) << ruleId
               << count << " excluded finding" << (count == 1 ? "" : "s") << "\n";
        }
    }

    os << "\n  Total: " << violations.size() << " violation"
       << (violations.size() == 1 ? "" : "s")
       << " (" << excluded.size() << " excluded finding"
       << (excluded.size() == 1 ? "" : "s") << ")\n";
    os << "================================================================\n";
}

bool ViolationStore::writeFileAtomically(const std::string& outputPath,
                                         const std::string& content) {
    if (outputPath.empty()) return false;

    std::string tmpPath = outputPath + ".tmp";
    {
        std::ofstream tmpOut(tmpPath, std::ios::out | std::ios::trunc);
        if (tmpOut.is_open()) {
            tmpOut << content;
            tmpOut.flush();
            if (tmpOut.good()) {
                tmpOut.close();
                std::error_code ec;
                std::filesystem::rename(tmpPath, outputPath, ec);
                if (!ec) {
                    return true;
                }
                std::filesystem::remove(tmpPath, ec);
            }
        }
    }

    std::ofstream ofs(outputPath, std::ios::out | std::ios::trunc);
    if (!ofs.is_open()) return false;
    ofs << content;
    ofs.flush();
    return ofs.good();
}

void ViolationStore::writeSnapshotReport(const std::string& outputPath) const {
    if (outputPath.empty()) return;

    std::string reportContent;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Violation> snapshot = violations_;
        std::vector<ExcludedViolation> excludedSnapshot = excludedViolations_;
        evaluateExclusionsAndPrecedence(snapshot, excludedSnapshot);

        std::ostringstream oss;
        printReportFor(oss, snapshot, excludedSnapshot);
        reportContent = oss.str();
    }

    writeFileAtomically(outputPath, reportContent);
}

size_t ViolationStore::finalize(const std::string& outputPath) {
    std::string reportContent;
    size_t total = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        evaluateExclusionsAndPrecedence(violations_, excludedViolations_);
        total = violations_.size();

        std::ostringstream oss;
        printReportFor(oss, violations_, excludedViolations_);
        reportContent = oss.str();
    }

    // Console output.
    std::cout << reportContent;

    // File output.
    if (writeFileAtomically(outputPath, reportContent)) {
        std::cout << "\nReport written to: " << outputPath << "\n";
    } else {
        std::cerr << "Warning: could not write report to " << outputPath << "\n";
    }

    return total;
}

} // namespace misra
