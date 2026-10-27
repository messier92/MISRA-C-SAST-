#include "ViolationStore.h"

#include <algorithm>
#include <cctype>
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
    if (file != rhs.file) return file < rhs.file;
    if (line != rhs.line) return line < rhs.line;
    if (col  != rhs.col)  return col  < rhs.col;
    return ruleId < rhs.ruleId;
}

// ── ViolationStore ─────────────────────────────────────────────

ViolationStore::ViolationStore(const std::string& projectRoot)
    : filter_(projectRoot) {}

void ViolationStore::addViolation(const Violation& v) {
    // Skip files outside the project root.
    if (!filter_.isProjectFile(v.file)) {
        return;
    }
    // Deduplicate by (file, line, col, ruleId).
    for (const auto& existing : violations_) {
        if (existing.file   == v.file   &&
            existing.line   == v.line   &&
            existing.col    == v.col    &&
            existing.ruleId == v.ruleId) {
            return;
        }
    }
    violations_.push_back(v);
}

void ViolationStore::registerRule(const std::string& ruleId) {
    registeredRules_.insert(ruleId);
}

void ViolationStore::setScannedFiles(const std::vector<std::string>& files) {
    scannedFiles_ = files;
}

void ViolationStore::setReportTag(const std::string& tag) {
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

void ViolationStore::applyPrecedence() {
    if (violations_.size() <= 1) return;

    using LineKey = std::pair<std::string, uint32_t>;
    std::map<LineKey, std::vector<Violation>> byLine;

    for (const auto& v : violations_) {
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
    violations_ = std::move(resolved);
}

// ── Report generation ──────────────────────────────────────────

void ViolationStore::printReport(std::ostream& os) const {
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

    // Violations (already sorted).
    if (violations_.empty()) {
        os << "No violations found.\n\n";
    } else {
        os << "Violations (" << violations_.size() << "):\n";
        os << std::string(72, '-') << "\n";
        for (const auto& v : violations_) {
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
    for (const auto& v : violations_) {
        counts[v.ruleId]++;
    }

    os << "Summary by rule:\n";
    for (const auto& [ruleId, count] : counts) {
        os << "  " << std::left << std::setw(20) << ruleId
           << count << " violation" << (count == 1 ? "" : "s") << "\n";
    }
    os << "\n  Total: " << violations_.size() << " violation"
       << (violations_.size() == 1 ? "" : "s") << "\n";
    os << "================================================================\n";
}

size_t ViolationStore::finalize(const std::string& outputPath) {
    // Apply per-line MISRA rule precedence after the complete scan.
    applyPrecedence();

    // Sort violations.
    std::sort(violations_.begin(), violations_.end());

    // Console output.
    printReport(std::cout);

    // File output.
    std::ofstream ofs(outputPath);
    if (ofs.is_open()) {
        printReport(ofs);
        ofs.close();
        std::cout << "\nReport written to: " << outputPath << "\n";
    } else {
        std::cerr << "Warning: could not write report to " << outputPath << "\n";
    }

    return violations_.size();
}

} // namespace misra
