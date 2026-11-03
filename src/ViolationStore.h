#ifndef MISRA_VIOLATION_STORE_H
#define MISRA_VIOLATION_STORE_H

#include "ProjectFileFilter.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>
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
/// Thread-safe for concurrent per-file analysis and incremental crash-proof reporting.
class ViolationStore {
public:
    /// @param projectRoot  Root directory for the ProjectFileFilter.
    ///                     Pass "" to disable filtering.
    explicit ViolationStore(const std::string& projectRoot = "");

    // ── Recording ──────────────────────────────────────────────

    /// Add a violation. Duplicates (same file/line/rule) are collapsed.
    void addViolation(const Violation& v);

    /// Register a rule ID so it appears in the summary even with 0 violations.
    void registerRule(const std::string& ruleId);

    /// Set the list of scanned files (shown at the top of the report).
    void setScannedFiles(const std::vector<std::string>& files);

    /// Record a file that failed, crashed, or timed out during analysis.
    void recordFileFailure(const std::string& file, const std::string& reason);

    /// Set an optional tag for the report (e.g. timestamp, commit hash).
    void setReportTag(const std::string& tag);

    // ── Precedence & Output ────────────────────────────────────

    /// Resolve reporting conflicts when multiple violations flag the same
    /// source line, enforcing MISRA rule group precedence and the
    /// Rule 0-1-x over Rule 6-6-5 exception.
    void applyPrecedence();

    /// Returns the precedence rank (1 = highest, 9 = lowest) for a rule ID.
    static int getRulePrecedenceRank(const std::string& ruleId);

    /// Returns true if `lhs` takes reporting precedence over `rhs` on the same line.
    static bool hasHigherPrecedence(const Violation& lhs, const Violation& rhs);

    /// Atomically write a crash-proof snapshot report of all violations
    /// collected so far (with precedence applied on a snapshot copy) to `outputPath`.
    void writeSnapshotReport(const std::string& outputPath) const;

    /// Apply precedence, sort violations, print to console, and write to `outputPath`.
    /// Returns the total number of violations.
    size_t finalize(const std::string& outputPath = "misra_output.txt");

    // ── Accessors ──────────────────────────────────────────────

    const std::vector<Violation>& getViolations() const { return violations_; }
    const std::vector<std::pair<std::string, std::string>>& getFailedFiles() const {
        return failedFiles_;
    }
    const ProjectFileFilter& getFilter() const { return filter_; }

private:
    /// Apply precedence and sort on a vector of violations in place.
    static void resolvePrecedenceOnVector(std::vector<Violation>& vec);

    /// Print the full report for `violations` to the given stream.
    void printReportFor(std::ostream& os,
                        const std::vector<Violation>& violations) const;

    /// Write formatted report content atomically to `outputPath`.
    static bool writeFileAtomically(const std::string& outputPath,
                                    const std::string& content);

    mutable std::mutex                               mutex_;
    ProjectFileFilter                                filter_;
    std::vector<Violation>                           violations_;
    std::set<std::string>                            registeredRules_;
    std::vector<std::string>                         scannedFiles_;
    std::vector<std::pair<std::string, std::string>> failedFiles_;
    std::string                                      reportTag_;
};

} // namespace misra

#endif // MISRA_VIOLATION_STORE_H
