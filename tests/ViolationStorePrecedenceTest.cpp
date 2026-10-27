#include "ViolationStore.h"

#include <cstdlib>
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

} // namespace

int main() {
    testRuleGroupPrecedenceOrder();
    testMultipleViolationsSameLine();
    testRule01xExceptionOverRule665();
    testSingleViolationLinesUnaffected();
    testReportFormatPreserved();

    std::cout << "All ViolationStore precedence tests passed successfully.\n";
    return 0;
}
