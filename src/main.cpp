/// ============================================================
/// misra_checker — MISRA C++:2008 Static Analysis Tool
/// ============================================================
///
/// Built on Clang LibTooling.  This is the CLI entry point that:
///   1. Parses command-line arguments (source files or directories).
///   2. Creates the shared ViolationStore and GlobalSymbolDatabase.
///   3. Instantiates all enabled Rule subclasses.
///   4. Runs the ClangTool over every translation unit.
///   5. Evaluates project-level rules from the GlobalSymbolDatabase.
///   6. Prints the final report to the console and to misra_output.txt.
///

#include "GlobalSymbolDatabase.h"
#include "MisraChecker.h"
#include "Rule.h"
#include "ViolationStore.h"

// ── Include concrete rules here ────────────────────────────────
#include "rules/Rule0/Rule011.h"
#include "rules/Rule0/Rule013.h"
#include "rules/Rule0/Rule016.h"
#include "rules/Rule0/Rule017.h"
#include "rules/Rule0/Rule018.h"
#include "rules/Rule0/Rule019.h"
#include "rules/Rule2/Rule2101.h"
#include "rules/Rule2/Rule2102.h"
#include "rules/Rule2/Rule2103.h"
#include "rules/Rule2/Rule2104.h"
#include "rules/Rule2/Rule2106.h"
#include "rules/Rule3/Rule311.h"
#include "rules/Rule3/Rule322.h"
#include "rules/Rule3/Rule323.h"
#include "rules/Rule3/Rule391.h"
#include "rules/Rule5/Rule524.h"
// Add more: #include "rules/RuleXYZ.h"

#include "clang/Tooling/ArgumentsAdjusters.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ── CLI options ────────────────────────────────────────────────

static llvm::cl::OptionCategory MisraCategory("MISRA Checker Options");

static llvm::cl::opt<std::string> ProjectRoot(
    "project-root",
    llvm::cl::desc("Root directory of the project (filters out non-project files)"),
    llvm::cl::value_desc("path"),
    llvm::cl::cat(MisraCategory));

static llvm::cl::opt<std::string> OutputFile(
    "output",
    llvm::cl::desc("Output report file path (default: misra_output.txt)"),
    llvm::cl::value_desc("file"),
    llvm::cl::init("misra_output.txt"),
    llvm::cl::cat(MisraCategory));

// ── Helpers ────────────────────────────────────────────────────

/// Build a human-readable timestamp for the report tag.
static std::string makeTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    char buf[64];
    struct tm tmBuf;
#ifdef _WIN32
    localtime_s(&tmBuf, &time);
#else
    localtime_r(&time, &tmBuf);
#endif
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmBuf);
    return buf;
}

/// Recursively collect all .cpp / .c / .cc / .cxx files under `dir`.
static std::vector<std::string> discoverSources(const std::string& dir) {
    std::vector<std::string> files;
    if (!fs::exists(dir) || !fs::is_directory(dir)) return files;
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        if (ext == ".cpp" || ext == ".c" || ext == ".cc" || ext == ".cxx") {
            files.push_back(entry.path().string());
        }
    }
    return files;
}

/// Register all enabled MISRA rules.  Add new rules here.
static void createRules(std::vector<std::unique_ptr<misra::Rule>>& rules) {
    rules.push_back(std::make_unique<misra::Rule011>());
    rules.push_back(std::make_unique<misra::Rule013>());
    rules.push_back(std::make_unique<misra::Rule016>());
    rules.push_back(std::make_unique<misra::Rule017>());
    rules.push_back(std::make_unique<misra::Rule018>());
    rules.push_back(std::make_unique<misra::Rule019>());
    rules.push_back(std::make_unique<misra::Rule2101>());
    rules.push_back(std::make_unique<misra::Rule2102>());
    rules.push_back(std::make_unique<misra::Rule2103>());
    rules.push_back(std::make_unique<misra::Rule2104>());
    rules.push_back(std::make_unique<misra::Rule2106>());
    rules.push_back(std::make_unique<misra::Rule311>());
    rules.push_back(std::make_unique<misra::Rule322>());
    rules.push_back(std::make_unique<misra::Rule323>());
    rules.push_back(std::make_unique<misra::Rule391>());
    rules.push_back(std::make_unique<misra::Rule524>());
    // ── Add more rules here ──
    // rules.push_back(std::make_unique<misra::Rule504>());
    // rules.push_back(std::make_unique<misra::Rule621>());
}

// ── main ───────────────────────────────────────────────────────

int main(int argc, const char** argv) {
    // ── Parse CLI ──────────────────────────────────────────────
    auto optParser = clang::tooling::CommonOptionsParser::create(
        argc, argv, MisraCategory);
    if (!optParser) {
        llvm::errs() << "Error: " << llvm::toString(optParser.takeError()) << "\n";
        return 1;
    }

    std::vector<std::string> sources = optParser->getSourcePathList();
    std::string inferredRoot;

    // If a single argument is a directory, expand it.
    if (sources.size() == 1 && fs::is_directory(sources[0])) {
        std::string dir = sources[0];
        inferredRoot = fs::absolute(dir).lexically_normal().string();
        sources = discoverSources(dir);
        if (sources.empty()) {
            std::cerr << "No C/C++ sources found under " << dir << "\n";
            return 1;
        }
    } else if (!sources.empty()) {
        inferredRoot = fs::absolute(sources[0]).lexically_normal().parent_path().string();
    }

    std::cout << "================================================================\n";
    std::cout << "  misra_checker — MISRA C++:2008 Static Analysis Tool\n";
    std::cout << "  " << sources.size() << " source file(s) to analyze\n";
    std::cout << "================================================================\n\n";

    // ── Create shared state ────────────────────────────────────
    std::string root = !ProjectRoot.empty()
        ? ProjectRoot.getValue()
        : (!inferredRoot.empty() ? inferredRoot : fs::current_path().string());

    misra::ViolationStore       store(root);
    misra::GlobalSymbolDatabase db;

    store.setScannedFiles(sources);
    store.setReportTag(makeTimestamp());

    // ── Create and register rules ──────────────────────────────
    std::vector<std::unique_ptr<misra::Rule>> rules;
    createRules(rules);

    for (const auto& rule : rules) {
        store.registerRule(rule->getRuleId());
    }

    std::cout << "Enabled rules: " << rules.size() << "\n";
    for (const auto& rule : rules) {
        std::cout << "  [" << rule->getRuleId() << "] "
                  << rule->getDescription() << "\n";
    }
    std::cout << "\n";

    // ── Run the Clang tool ─────────────────────────────────────
    clang::tooling::ClangTool tool(optParser->getCompilations(), sources);
    tool.appendArgumentsAdjuster(
        clang::tooling::getInsertArgumentAdjuster(
            {"-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH",
             "-fno-access-control",
             "-ffreestanding",
             "-w"},
            clang::tooling::ArgumentInsertPosition::BEGIN));

    misra::MisraActionFactory factory(rules, &store, &db);

    std::cout << "Running analysis...\n";
    int toolResult = tool.run(&factory);

    if (toolResult != 0) {
        std::cerr << "Warning: Clang tool returned non-zero ("
                  << toolResult << "). Some files may have had parse errors.\n\n";
    }

    // ── Project-level evaluation ───────────────────────────────
    std::cout << "\nEvaluating project-level rules...\n";
    db.evaluateAll(store);

    // ── Finalize and output ────────────────────────────────────
    std::cout << "\n";
    size_t total = store.finalize(OutputFile.getValue());

    return (total > 0) ? 1 : 0;
}
