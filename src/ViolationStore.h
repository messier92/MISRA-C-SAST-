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

/// A finding excluded by false-positive suppression rules, retained with its
/// MISRA rule ID and justification so all exclusions are auditable.
struct ExcludedViolation {
    Violation   violation;
    std::string justification;

    bool operator<(const ExcludedViolation& rhs) const {
        return violation < rhs.violation;
    }
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

    /// Add an explicitly excluded finding with its auditable justification.
    void addExcludedViolation(const Violation& v, const std::string& justification);

    /// Register a rule ID so it appears in the summary even with 0 violations.
    void registerRule(const std::string& ruleId);

    /// Set the list of scanned files (shown at the top of the report).
    void setScannedFiles(const std::vector<std::string>& files);

    /// Record a file that failed, crashed, or timed out during analysis.
    void recordFileFailure(const std::string& file, const std::string& reason);

    /// Set an optional tag for the report (e.g. timestamp, commit hash).
    void setReportTag(const std::string& tag);

    // ── Precedence & Output ────────────────────────────────────

    /// Resolve false-positive exclusions and reporting conflicts when multiple
    /// violations flag the same source line, enforcing MISRA rule group
    /// precedence and the Rule 0-1-x over Rule 6-6-5 exception.
    void applyPrecedence();

    /// Evaluate whether `v` matches a false-positive exclusion rule (Qt signal
    /// emission under 0-1-6 / 0-1-3, Q_OBJECT member variable verified as used
    /// in .cpp files under 0-1-3, or '!' on non-bool Qt expressions under 5-3-1).
    bool isFalsePositiveViolation(const Violation& v,
                                  std::string* outJustification = nullptr) const;

    /// Returns the precedence rank (1 = highest, 9 = lowest) for a rule ID.
    static int getRulePrecedenceRank(const std::string& ruleId);

    /// Returns true if `lhs` takes reporting precedence over `rhs` on the same line.
    static bool hasHigherPrecedence(const Violation& lhs, const Violation& rhs);

    /// Atomically write a crash-proof snapshot report of all violations
    /// collected so far (with precedence applied on a snapshot copy) to `outputPath`.
    void writeSnapshotReport(const std::string& outputPath) const;

    /// Apply precedence, sort violations, print to console, and write to `outputPath`.
    /// Returns the total number of confirmed violations.
    size_t finalize(const std::string& outputPath = "misra_output.txt");

    // ── Accessors ──────────────────────────────────────────────

    const std::vector<Violation>& getViolations() const { return violations_; }
    const std::vector<ExcludedViolation>& getExcludedViolations() const {
        return excludedViolations_;
    }
    const std::vector<std::string>& getScannedFiles() const { return scannedFiles_; }
    const std::vector<std::pair<std::string, std::string>>& getFailedFiles() const {
        return failedFiles_;
    }
    const ProjectFileFilter& getFilter() const { return filter_; }
    const std::string& getProjectRoot() const { return rawProjectRoot_; }

private:
    /// Partition false positives into `excluded` and apply precedence on `vec`.
    void evaluateExclusionsAndPrecedence(
        std::vector<Violation>& vec,
        std::vector<ExcludedViolation>& excluded) const;

    /// Apply precedence and sort on a vector of violations in place.
    static void resolvePrecedenceOnVector(std::vector<Violation>& vec);

    /// Print the full report for `violations` and `excluded` to the given stream.
    void printReportFor(std::ostream& os,
                        const std::vector<Violation>& violations,
                        const std::vector<ExcludedViolation>& excluded) const;

    /// Write formatted report content atomically to `outputPath`.
    static bool writeFileAtomically(const std::string& outputPath,
                                    const std::string& content);

    mutable std::mutex                               mutex_;
    std::string                                      rawProjectRoot_;
    ProjectFileFilter                                filter_;
    std::vector<Violation>                           violations_;
    std::vector<ExcludedViolation>                   excludedViolations_;
    std::set<std::string>                            registeredRules_;
    std::vector<std::string>                         scannedFiles_;
    std::vector<std::pair<std::string, std::string>> failedFiles_;
    std::string                                      reportTag_;
};

} // namespace misra

#endif // MISRA_VIOLATION_STORE_H
