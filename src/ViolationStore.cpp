#include "ViolationStore.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace misra {

namespace {

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
    : filter_(projectRoot) {}

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
    resolvePrecedenceOnVector(violations_);
}

// ── Report generation ──────────────────────────────────────────

void ViolationStore::printReportFor(
    std::ostream& os,
    const std::vector<Violation>& violations) const {
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

    // Violations (already sorted).
    if (violations.empty()) {
        os << "No violations found.\n\n";
    } else {
        os << "Violations (" << violations.size() << "):\n";
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

    // Per-rule summary.
    // Count violations per rule (including registered rules with 0 hits).
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
    os << "\n  Total: " << violations.size() << " violation"
       << (violations.size() == 1 ? "" : "s") << "\n";
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
        resolvePrecedenceOnVector(snapshot);

        std::ostringstream oss;
        printReportFor(oss, snapshot);
        reportContent = oss.str();
    }

    writeFileAtomically(outputPath, reportContent);
}

size_t ViolationStore::finalize(const std::string& outputPath) {
    std::string reportContent;
    size_t total = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        resolvePrecedenceOnVector(violations_);
        total = violations_.size();

        std::ostringstream oss;
        printReportFor(oss, violations_);
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
