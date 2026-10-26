#include "ViolationStore.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace misra {

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
