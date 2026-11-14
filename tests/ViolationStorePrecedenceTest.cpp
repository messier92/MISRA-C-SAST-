#include "ViolationStore.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#define REQUIRE(cond)                                                          \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "Assertion failed at " << __FILE__ << ":" << __LINE__ \
                      << ": " << #cond << "\n";                                \
            std::exit(1);                                                      \
        }                                                                      \
    } while (0)

using misra::Violation;
using misra::ViolationStore;

namespace {

Violation makeViolation(const std::string& file,
                        uint32_t line,
                        uint32_t col,
                        const std::string& ruleId,
                        const std::string& message = "test violation",
                        const std::string& sourceLine = "int x = 0;") {
    Violation v;
    v.file = file;
    v.line = line;
    v.col = col;
    v.ruleId = ruleId;
    v.message = message;
    v.sourceLine = sourceLine;
    return v;
}

/// 1. Test full precedence chain across all 9 rule groups:
///    15 > 5 > 3 > 16 > 7 > 9 > 2 > 6 > 0
void testRuleGroupPrecedenceOrder() {
    const std::vector<std::string> orderedRules = {
        "MISRA-15-3-5",
        "MISRA-5-2-4",
        "MISRA-3-2-2",
        "MISRA-16-0-1",
        "MISRA-7-1-1",
        "MISRA-9-3-1",
        "MISRA-2-10-1",
        "MISRA-6-2-1",
        "MISRA-0-1-6",
    };

    for (size_t i = 0; i + 1 < orderedRules.size(); ++i) {
        REQUIRE(ViolationStore::getRulePrecedenceRank(orderedRules[i]) <
               ViolationStore::getRulePrecedenceRank(orderedRules[i + 1]));
    }

    // Verify pairwise conflicts between every pair of rule groups (excluding 0-1-x vs 6-6-5).
    for (size_t higher = 0; higher < orderedRules.size(); ++higher) {
        for (size_t lower = higher + 1; lower < orderedRules.size(); ++lower) {
            ViolationStore store("");
            // Add lower-priority violation first (even at an earlier column) and
            // higher-priority violation second to verify precedence overrides order/column.
            store.addViolation(makeViolation("test.cpp", 10, 2, orderedRules[lower]));
            store.addViolation(makeViolation("test.cpp", 10, 15, orderedRules[higher]));

            // Before applyPrecedence(), both violations are stored.
            REQUIRE(store.getViolations().size() == 2);

            store.applyPrecedence();

            REQUIRE(store.getViolations().size() == 1);
            REQUIRE(store.getViolations()[0].ruleId == orderedRules[higher]);
        }
    }
}

/// 2. Test conflicts with 3+ violations on the same source line.
void testMultipleViolationsSameLine() {
    ViolationStore store("");
    store.addViolation(makeViolation("multi.cpp", 25, 1, "MISRA-0-1-9"));
    store.addViolation(makeViolation("multi.cpp", 25, 4, "MISRA-6-2-1"));
    store.addViolation(makeViolation("multi.cpp", 25, 8, "MISRA-2-10-2"));
    store.addViolation(makeViolation("multi.cpp", 25, 12, "MISRA-7-1-1"));
    store.addViolation(makeViolation("multi.cpp", 25, 16, "MISRA-3-1-1"));

    REQUIRE(store.getViolations().size() == 5);
    store.applyPrecedence();

    // Rule 3 has the highest precedence among {0, 6, 2, 7, 3}.
    REQUIRE(store.getViolations().size() == 1);
    REQUIRE(store.getViolations()[0].ruleId == "MISRA-3-1-1");
    REQUIRE(store.getViolations()[0].line == 25);
}

/// 3. Test the specific Rule 0-1-x exception over Rule 6-6-5.
void testRule01xExceptionOverRule665() {
    const std::vector<std::string> rule01xIds = {
        "MISRA-0-1-1",
        "MISRA-0-1-3",
        "MISRA-0-1-6",
        "MISRA-0-1-7",
        "MISRA-0-1-8",
        "MISRA-0-1-9",
    };

    for (const auto& r01x : rule01xIds) {
        ViolationStore store("");
        store.addViolation(makeViolation("except.cpp", 42, 5, "MISRA-6-6-5"));
        store.addViolation(makeViolation("except.cpp", 42, 10, r01x));

        store.applyPrecedence();

        // Rule 0-1-x must take precedence over Rule 6-6-5.
        REQUIRE(store.getViolations().size() == 1);
        REQUIRE(store.getViolations()[0].ruleId == r01x);
    }

    // Other Rule 6 rules (e.g. 6-2-1, 6-4-5, 6-5-1) still take precedence over Rule 0-1-x.
    {
        ViolationStore store("");
        store.addViolation(makeViolation("except.cpp", 50, 2, "MISRA-0-1-1"));
        store.addViolation(makeViolation("except.cpp", 50, 8, "MISRA-6-4-5"));

        store.applyPrecedence();

        REQUIRE(store.getViolations().size() == 1);
        REQUIRE(store.getViolations()[0].ruleId == "MISRA-6-4-5");
    }

    // Three-way conflict: Rule 0-1-1 + Rule 6-6-5 + Rule 5-2-4 -> Rule 5-2-4 wins.
    {
        ViolationStore store("");
        store.addViolation(makeViolation("except.cpp", 60, 2, "MISRA-6-6-5"));
        store.addViolation(makeViolation("except.cpp", 60, 4, "MISRA-0-1-1"));
        store.addViolation(makeViolation("except.cpp", 60, 12, "MISRA-5-2-4"));

        store.applyPrecedence();

        REQUIRE(store.getViolations().size() == 1);
        REQUIRE(store.getViolations()[0].ruleId == "MISRA-5-2-4");
    }
}

/// 4. Test that lines with only a single violation remain unaffected.
void testSingleViolationLinesUnaffected() {
    ViolationStore store("");
    store.addViolation(makeViolation("single.cpp", 10, 2, "MISRA-0-1-1"));
    store.addViolation(makeViolation("single.cpp", 20, 4, "MISRA-6-6-5"));
    store.addViolation(makeViolation("single.cpp", 30, 6, "MISRA-5-2-4"));
    store.addViolation(makeViolation("other.cpp", 10, 2, "MISRA-15-3-5"));

    store.applyPrecedence();

    REQUIRE(store.getViolations().size() == 4);
    REQUIRE(store.getViolations()[0].file == "other.cpp");
    REQUIRE(store.getViolations()[0].line == 10);
    REQUIRE(store.getViolations()[0].ruleId == "MISRA-15-3-5");

    REQUIRE(store.getViolations()[1].file == "single.cpp");
    REQUIRE(store.getViolations()[1].line == 10);
    REQUIRE(store.getViolations()[1].ruleId == "MISRA-0-1-1");

    REQUIRE(store.getViolations()[2].line == 20);
    REQUIRE(store.getViolations()[2].ruleId == "MISRA-6-6-5");

    REQUIRE(store.getViolations()[3].line == 30);
    REQUIRE(store.getViolations()[3].ruleId == "MISRA-5-2-4");
}

/// 5. Regression check on report generation and misra_output.txt preservation.
void testReportFormatPreserved() {
    // Capture existing misra_output.txt content if present to ensure it is untouched.
    std::string originalContent;
    {
        std::ifstream ifs("misra_output.txt", std::ios::binary);
        if (ifs.is_open()) {
            std::ostringstream ss;
            ss << ifs.rdbuf();
            originalContent = ss.str();
        }
    }

    const std::string tempReportPath = "temp_precedence_test_report.txt";
    {
        ViolationStore store("");
        store.registerRule("MISRA-0-1-6");
        store.registerRule("MISRA-5-2-4");
        store.setScannedFiles({"sample.cpp"});
        store.setReportTag("2026-10-27 00:00:00");

        // Same line flagged by both MISRA-0-1-6 and MISRA-5-2-4.
        store.addViolation(makeViolation("sample.cpp", 52, 2, "MISRA-0-1-6",
                                         "Unused init", "Base* obj2 = (Base*)(&obj);"));
        store.addViolation(makeViolation("sample.cpp", 52, 15, "MISRA-5-2-4",
                                         "C-style cast", "Base* obj2 = (Base*)(&obj);"));

        size_t total = store.finalize(tempReportPath);
        REQUIRE(total == 1);

        std::ifstream reportIn(tempReportPath);
        REQUIRE(reportIn.is_open());
        std::ostringstream ss;
        ss << reportIn.rdbuf();
        std::string reportText = ss.str();
        reportIn.close();
        std::remove(tempReportPath.c_str());

        REQUIRE(reportText.find("Violations (1):") != std::string::npos);
        REQUIRE(reportText.find("sample.cpp:52:15  [MISRA-5-2-4]  C-style cast") != std::string::npos);
        REQUIRE(reportText.find("Summary by rule:") != std::string::npos);
        REQUIRE(reportText.find("MISRA-0-1-6         0 violations") != std::string::npos);
        REQUIRE(reportText.find("MISRA-5-2-4         1 violation") != std::string::npos);
        REQUIRE(reportText.find("Total: 1 violation") != std::string::npos);
    }

    // Verify misra_output.txt was not modified.
    if (!originalContent.empty()) {
        std::ifstream ifs("misra_output.txt", std::ios::binary);
        REQUIRE(ifs.is_open());
        std::ostringstream ss;
        ss << ifs.rdbuf();
        REQUIRE(ss.str() == originalContent);
    }
}

/// 6. Test canonicalisation of duplicate #include path spellings and collapsing
///    noisy included headers to one entry per line:file automatically.
void testDuplicatePathSpellingsAndHeaderCanonicalisation() {
    ViolationStore store("C:\\repo");

    // Same physical header arrived via multiple #include path spellings across TUs:
    store.addViolation(makeViolation("C:\\repo\\src\\modA\\..\\..\\include\\generated_hdr.h",
                                     100, 8, "MISRA-0-1-3", "Unused field"));
    store.addViolation(makeViolation("C:/repo/src/modB/sub/../../../include/generated_hdr.h",
                                     100, 8, "MISRA-0-1-3", "Unused field"));
    store.addViolation(makeViolation("c:\\repo\\include\\./generated_hdr.h",
                                     100, 12, "MISRA-0-1-3", "Unused field"));
    // Another per-TU rule (MISRA-2-10-1) firing on the exact same line in the header
    // under yet another path spelling:
    store.addViolation(makeViolation("C:\\repo\\src\\..\\include\\generated_hdr.h",
                                     100, 4, "MISRA-2-10-1", "Typographically ambiguous"));

    // Before applyPrecedence(), all MISRA-0-1-3 duplicates on line 100 collapsed
    // into 1 entry and MISRA-2-10-1 is 1 entry -> 2 entries total.
    REQUIRE(store.getViolations().size() == 2);

    store.applyPrecedence();

    // After applyPrecedence(), collapsed to a single entry for (file, line),
    // with Rule 2-10-1 winning over Rule 0-1-3 and canonical path preserved.
    REQUIRE(store.getViolations().size() == 1);
    REQUIRE(store.getViolations()[0].file == "C:\\repo\\include\\generated_hdr.h");
    REQUIRE(store.getViolations()[0].line == 100);
    REQUIRE(store.getViolations()[0].ruleId == "MISRA-2-10-1");
}

/// 7. Test that project-owned .cpp, .c, .h, .hpp files under project root are
///    kept while Qt, MSVC, Windows SDK, compiler builtin, and third-party
///    dependency paths are filtered out.
void testSourceLocationAndExternalDirectoryFiltering() {
    ViolationStore store("C:\\repo");
    const auto& filter = store.getFilter();

    // Project-owned implementation and header files MUST be included:
    REQUIRE(filter.isProjectFile("C:\\repo\\src\\main.cpp"));
    REQUIRE(filter.isProjectFile("C:\\repo\\src\\driver.c"));
    REQUIRE(filter.isProjectFile("C:\\repo\\include\\config.h"));
    REQUIRE(filter.isProjectFile("C:\\repo\\include\\model.hpp"));

    // Qt installation directories, MSVC toolchain, Windows SDK, compiler builtins,
    // and third-party dependencies (even inside C:\repo) MUST be skipped:
    REQUIRE(!filter.isProjectFile("C:\\Qt\\6.5.0\\msvc2019_64\\include\\QtCore\\qobject.h"));
    REQUIRE(!filter.isProjectFile("C:\\repo\\Qt\\include\\QtWidgets\\QWidget.h"));
    REQUIRE(!filter.isProjectFile("C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Tools\\MSVC\\14.44\\include\\vector"));
    REQUIRE(!filter.isProjectFile("C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.22621.0\\um\\Windows.h"));
    REQUIRE(!filter.isProjectFile("<built-in>"));
    REQUIRE(!filter.isProjectFile("C:\\Program Files\\llvm\\lib\\clang\\18\\include\\stdint.h"));
    REQUIRE(!filter.isProjectFile("C:\\repo\\third_party\\nlohmann\\json.hpp"));
    REQUIRE(!filter.isProjectFile("C:\\repo\\external\\fmt\\format.h"));
    REQUIRE(!filter.isProjectFile("C:\\repo\\vendor\\sdk\\api.h"));
    REQUIRE(!filter.isProjectFile("C:\\repo\\vcpkg_installed\\x64-windows\\include\\zlib.h"));
}

/// 8. Test crash-proof incremental snapshot report generation and failure logging.
void testIncrementalSnapshotAndFailureReporting() {
    ViolationStore store;
    store.setReportTag("2026-11-03 12:00:00");
    store.setScannedFiles({"alpha.cpp", "beta.cpp"});
    store.registerRule("MISRA-5-2-4");
    store.registerRule("MISRA-0-1-6");

    const std::string snapshotPath = "temp_snapshot_test_report.txt";

    // Write initial empty snapshot before any file completes.
    store.writeSnapshotReport(snapshotPath);
    {
        std::ifstream ifs(snapshotPath);
        REQUIRE(ifs.is_open());
        std::ostringstream ss;
        ss << ifs.rdbuf();
        REQUIRE(ss.str().find("No violations found.") != std::string::npos);
    }

    // Add conflicting violations on line 10 and record a timeout failure on beta.cpp.
    store.addViolation(makeViolation("alpha.cpp", 10, 5, "MISRA-0-1-6", "Unused value"));
    store.addViolation(makeViolation("alpha.cpp", 10, 9, "MISRA-5-2-4", "C-style cast"));
    store.recordFileFailure("beta.cpp", "Timed out after 60s");

    // Write incremental snapshot and verify precedence was applied on the snapshot
    // while live violations in the store are still preserved for subsequent TUs.
    store.writeSnapshotReport(snapshotPath);
    REQUIRE(store.getViolations().size() == 2);
    REQUIRE(store.getFailedFiles().size() == 1);

    {
        std::ifstream ifs(snapshotPath);
        REQUIRE(ifs.is_open());
        std::ostringstream ss;
        ss << ifs.rdbuf();
        std::string content = ss.str();
        REQUIRE(content.find("Failed / Timed-out files (1):") != std::string::npos);
        REQUIRE(content.find("Timed out after 60s") != std::string::npos);
        REQUIRE(content.find("Violations (1):") != std::string::npos);
        REQUIRE(content.find("[MISRA-5-2-4]") != std::string::npos);
    }

    std::remove(snapshotPath.c_str());
}

/// 9. Test Qt false-positive exclusions (emit signalName under 0-1-6 / 0-1-3,
///    Q_OBJECT member variables verified in .cpp files under 0-1-3, and '!' on
///    non-bool Qt expressions under 5-3-1), preservation of real findings
///    (7-1-1, 6-6-5, 7-3-1, 2-10-1, 0-1-7), and two-section auditable report.
void testQtFalsePositiveExclusionsAndAuditableReport() {
    namespace fs = std::filesystem;
    fs::path tempDir = fs::temp_directory_path() / "misra_qt_fp_unit_test";
    std::error_code ec;
    fs::remove_all(tempDir, ec);
    fs::create_directories(tempDir, ec);

    fs::path headerPath = tempDir / "Controller.h";
    fs::path cppPath    = tempDir / "Controller.cpp";
    fs::path reportPath = tempDir / "qt_audit_report.txt";

    {
        std::ofstream hOut(headerPath);
        hOut << "#pragma once\n"
             << "class QObject {};\n"
             << "class QString { public: bool isEmpty() const; };\n"
             << "class Controller : public QObject {\n"
             << "    Q_OBJECT\n"
             << "signals:\n"
             << "    void statusChanged(const QString& s);\n"
             << "    void countUpdated(int c);\n"
             << "public slots:\n"
             << "    void onAction(const QString& text);\n"
             << "private:\n"
             << "    int m_usedInSlot;\n"
             << "    int m_trulyUnused;\n"
             << "};\n";
    }
    {
        std::ofstream cppOut(cppPath);
        cppOut << "#include \"Controller.h\"\n"
               << "int globalCounter = 0;\n"
               << "bool saveState(const QString& p);\n"
               << "Controller::Controller() : m_usedInSlot(0), m_trulyUnused(0) {}\n"
               << "void Controller::onAction(const QString& text) {\n"
               << "    m_usedInSlot++;\n"
               << "    if (!text.isEmpty()) {\n"
               << "        emit statusChanged(text);\n"
               << "        return;\n"
               << "    }\n"
               << "    int step = m_usedInSlot;\n"
               << "    emit countUpdated(step);\n"
               << "    saveState(text);\n"
               << "    return;\n"
               << "}\n";
    }

    ViolationStore store(tempDir.string());
    store.setScannedFiles({cppPath.string()});
    store.registerRule("MISRA-0-1-3");
    store.registerRule("MISRA-0-1-6");
    store.registerRule("MISRA-0-1-7");
    store.registerRule("MISRA-2-10-1");
    store.registerRule("MISRA-5-3-1");
    store.registerRule("MISRA-6-6-5");
    store.registerRule("MISRA-7-1-1");
    store.registerRule("MISRA-7-3-1");

    // 1. False positives to be excluded:
    // (a) Qt signal emission under MISRA-0-1-3 and MISRA-0-1-6:
    store.addViolation(makeViolation(
        cppPath.string(), 8, 14, "MISRA-0-1-3",
        "Variable 'statusChanged' is declared or defined but never used.",
        "emit statusChanged(text);"));
    store.addViolation(makeViolation(
        cppPath.string(), 12, 10, "MISRA-0-1-6",
        "Variable 'countUpdated' is initialized with a value that is never subsequently used.",
        "emit countUpdated(step);"));

    // (b) Q_OBJECT member variable used inside slot in Controller.cpp (MISRA-0-1-3):
    store.addViolation(makeViolation(
        headerPath.string(), 12, 9, "MISRA-0-1-3",
        "Member variable 'm_usedInSlot' is declared but never read or written.",
        "int m_usedInSlot;"));

    // (c) '!' on non-bool Qt expression (MISRA-5-3-1) on the SAME line as a real 7-1-1 finding:
    store.addViolation(makeViolation(
        cppPath.string(), 7, 9, "MISRA-5-3-1",
        "Operand of logical '!' operator does not have type bool [MISRA-5-3-1]",
        "if (!text.isEmpty()) {"));
    store.addViolation(makeViolation(
        cppPath.string(), 7, 5, "MISRA-7-1-1",
        "Variable 'localFlag' is not modified and shall be const qualified [MISRA-7-1-1]",
        "if (!text.isEmpty()) {"));

    // 2. Real findings that MUST still be reported:
    // - Genuinely unused Q_OBJECT member variable (only in ctor init list, never read/written in .cpp):
    store.addViolation(makeViolation(
        headerPath.string(), 13, 9, "MISRA-0-1-3",
        "Member variable 'm_trulyUnused' is declared but never read or written.",
        "int m_trulyUnused;"));
    // - MISRA-7-3-1, MISRA-6-6-5, MISRA-2-10-1, MISRA-0-1-7:
    store.addViolation(makeViolation(
        cppPath.string(), 2, 5, "MISRA-7-3-1",
        "Variable 'globalCounter' is declared in the global namespace [MISRA-7-3-1]",
        "int globalCounter = 0;"));
    store.addViolation(makeViolation(
        cppPath.string(), 9, 9, "MISRA-6-6-5",
        "Function 'onAction' has multiple return statements [MISRA-6-6-5]",
        "return;"));
    store.addViolation(makeViolation(
        cppPath.string(), 11, 9, "MISRA-2-10-1",
        "Identifier 'dataO1' is typographically ambiguous with 'data0l' [MISRA-2-10-1]",
        "int dataO1 = 1;"));
    store.addViolation(makeViolation(
        cppPath.string(), 13, 5, "MISRA-0-1-7",
        "Return value of non-void function call is ignored.",
        "saveState(text);"));

    size_t confirmedCount = store.finalize(reportPath.string());
    // 6 confirmed findings: m_trulyUnused (0-1-3), globalCounter (7-3-1),
    // localFlag (7-1-1 on line 7 after 5-3-1 was excluded!), onAction (6-6-5),
    // dataO1 (2-10-1), saveState (0-1-7).
    REQUIRE(confirmedCount == 6);
    REQUIRE(store.getExcludedViolations().size() == 4);

    std::ifstream ifs(reportPath);
    REQUIRE(ifs.is_open());
    std::ostringstream ss;
    ss << ifs.rdbuf();
    std::string reportText = ss.str();
    ifs.close();

    // Verify two-section report structure and auditable justifications with rule numbers:
    REQUIRE(reportText.find("Confirmed Findings — Violations (6):") != std::string::npos);
    REQUIRE(reportText.find("Excluded Findings (4):") != std::string::npos);
    REQUIRE(reportText.find("Exclusion Justification [MISRA-0-1-3]:") != std::string::npos);
    REQUIRE(reportText.find("Exclusion Justification [MISRA-0-1-6]:") != std::string::npos);
    REQUIRE(reportText.find("Exclusion Justification [MISRA-5-3-1]:") != std::string::npos);
    REQUIRE(reportText.find("[MISRA-7-1-1]") != std::string::npos);
    REQUIRE(reportText.find("[MISRA-6-6-5]") != std::string::npos);
    REQUIRE(reportText.find("[MISRA-7-3-1]") != std::string::npos);
    REQUIRE(reportText.find("[MISRA-2-10-1]") != std::string::npos);
    REQUIRE(reportText.find("[MISRA-0-1-7]") != std::string::npos);

    fs::remove_all(tempDir, ec);
}

} // namespace

int main() {
    testRuleGroupPrecedenceOrder();
    testMultipleViolationsSameLine();
    testRule01xExceptionOverRule665();
    testSingleViolationLinesUnaffected();
    testReportFormatPreserved();
    testDuplicatePathSpellingsAndHeaderCanonicalisation();
    testSourceLocationAndExternalDirectoryFiltering();
    testIncrementalSnapshotAndFailureReporting();
    testQtFalsePositiveExclusionsAndAuditableReport();

    std::cout << "All ViolationStore precedence tests passed successfully.\n";
    return 0;
}
