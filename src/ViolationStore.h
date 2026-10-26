#ifndef MISRA_VIOLATION_STORE_H
#define MISRA_VIOLATION_STORE_H

#include "ProjectFileFilter.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace misra {

/// A single MISRA violation found during analysis.
struct Violation {
    std::string file;       // Absolute path of the source file.
    uint32_t    line;       // 1-based line number.
    uint32_t    col;        // 1-based column number.
    std::string ruleId;     // e.g. "MISRA-0-1-1"
    std::string message;    // Human-readable description.
    std::string sourceLine; // The offending source line (trimmed).

    /// Ordering: file, then line, then column.
    bool operator<(const Violation& rhs) const;
};

/// Collects violations from all rules and all translation units,
/// then prints a sorted report to the console and writes it to a .txt file.
class ViolationStore {
public:
    /// @param projectRoot  Root directory for the ProjectFileFilter.
    ///                     Pass "" to disable filtering.
    explicit ViolationStore(const std::string& projectRoot = "");

    // ── Recording ──────────────────────────────────────────────

    /// Add a violation. Duplicates (same file/line/col/rule) are silently ignored.
    void addViolation(const Violation& v);

    /// Register a rule ID so it appears in the summary even with 0 violations.
    void registerRule(const std::string& ruleId);

    /// Set the list of scanned files (shown at the top of the report).
    void setScannedFiles(const std::vector<std::string>& files);

    /// Set an optional tag for the report (e.g. timestamp, commit hash).
    void setReportTag(const std::string& tag);

    // ── Output ─────────────────────────────────────────────────

    /// Sort violations, print to console, and write to `outputPath`.
    /// Returns the total number of violations.
    size_t finalize(const std::string& outputPath = "misra_output.txt");

    // ── Accessors ──────────────────────────────────────────────

    const std::vector<Violation>& getViolations() const { return violations_; }
    const ProjectFileFilter& getFilter() const { return filter_; }

private:
    /// Print the full report to the given stream.
    void printReport(std::ostream& os) const;

    ProjectFileFilter          filter_;
    std::vector<Violation>     violations_;
    std::set<std::string>      registeredRules_;
    std::vector<std::string>   scannedFiles_;
    std::string                reportTag_;
};

} // namespace misra

#endif // MISRA_VIOLATION_STORE_H
