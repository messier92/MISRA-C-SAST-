/// ============================================================
/// misra_checker — MISRA C++:2008 Static Analysis Tool
/// ============================================================
///
/// Built on Clang LibTooling.  This is the CLI entry point that:
///   1. Parses command-line arguments (source files or directories).
///   2. Creates the shared ViolationStore and GlobalSymbolDatabase.
///   3. Instantiates all enabled Rule subclasses per isolated worker.
///   4. Processes translation units in parallel with per-file crash,
///      parse-error, and timeout isolation.
///   5. Incrementally writes a crash-proof report after each file and on
///      signals / abnormal termination.
///   6. Evaluates project-level rules from the GlobalSymbolDatabase.
///   7. Prints the final report to the console and to misra_output.txt.
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
#include "rules/Rule5/Rule504.h"
#include "rules/Rule5/Rule506.h"
#include "rules/Rule5/Rule508.h"
#include "rules/Rule5/Rule524.h"
#include "rules/Rule5/Rule531.h"
#include "rules/Rule6/Rule621.h"
#include "rules/Rule6/Rule645.h"
#include "rules/Rule6/Rule651.h"
#include "rules/Rule6/Rule665.h"
#include "rules/Rule7/Rule711.h"
#include "rules/Rule7/Rule731.h"
#include "rules/Rule9/Rule931.h"
#include "rules/Rule9/Rule933.h"
#include "rules/Rule9/Rule951.h"
#include "rules/Rule15/Rule1535.h"
#include "rules/Rule15/Rule1536.h"
#include "rules/Rule15/Rule1537.h"
#include "rules/Rule15/Rule1551.h"
#include "rules/Rule16/Rule1601.h"
#include "rules/Rule16/Rule1606.h"
#include "rules/Rule16/Rule1621.h"
// Add more: #include "rules/RuleXYZ.h"

#include "clang/Tooling/ArgumentsAdjusters.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <malloc.h>
#endif

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

static llvm::cl::opt<unsigned> JobsOpt(
    "jobs",
    llvm::cl::desc("Number of parallel worker threads (0 = automatic)"),
    llvm::cl::value_desc("N"),
    llvm::cl::init(0),
    llvm::cl::cat(MisraCategory));

static llvm::cl::opt<unsigned> TimeoutOpt(
    "timeout",
    llvm::cl::desc("Per-file analysis timeout in seconds (default: 60)"),
    llvm::cl::value_desc("seconds"),
    llvm::cl::init(60),
    llvm::cl::cat(MisraCategory));

// ── Crash-Proof Emergency Report State ─────────────────────────

namespace {

misra::ViolationStore* gEmergencyStore      = nullptr;
std::string            gEmergencyOutputPath;
std::mutex             gLogMutex;
std::mutex             gCompilationsMutex;

void flushEmergencyReport() {
    if (gEmergencyStore && !gEmergencyOutputPath.empty()) {
        gEmergencyStore->writeSnapshotReport(gEmergencyOutputPath);
    }
}

void signalHandler(int sig) {
    flushEmergencyReport();
    std::_Exit(128 + sig);
}

#ifdef _WIN32
BOOL WINAPI consoleCtrlHandler(DWORD ctrlType) {
    switch (ctrlType) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            flushEmergencyReport();
            return FALSE;
        default:
            return FALSE;
    }
}

LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* ep) {
    if (ep && ep->ExceptionRecord &&
        ep->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW) {
        _resetstkoflw();
    }
    flushEmergencyReport();
    return EXCEPTION_EXECUTE_HANDLER;
}

/// Pure-C frame wrapper around ClangTool::run to catch hardware / SEH exceptions
/// (e.g., access violations or stack overflows) per translation unit without
/// terminating the scan.
int runToolWithSEH(clang::tooling::ClangTool* tool,
                   clang::tooling::FrontendActionFactory* factory,
                   unsigned long* outExceptionCode) {
    __try {
        return tool->run(factory);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        unsigned long code = GetExceptionCode();
        if (code == EXCEPTION_STACK_OVERFLOW) {
            _resetstkoflw();
        }
        if (outExceptionCode) {
            *outExceptionCode = code;
        }
        return -999;
    }
}
#endif

} // namespace

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

/// Recursively collect all .cpp / .c / .cc / .cxx translation units under `dir`,
/// skipping external/third-party/toolchain directories.
static std::vector<std::string> discoverSources(const std::string& dir) {
    std::vector<std::string> files;
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return files;
    for (auto it = fs::recursive_directory_iterator(
             dir, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (!it->is_regular_file(ec)) continue;
        std::string pathStr = it->path().string();
        if (misra::ProjectFileFilter::isExcludedExternalOrThirdPartyPath(pathStr)) {
            continue;
        }
        auto ext = it->path().extension().string();
        if (ext == ".cpp" || ext == ".c" || ext == ".cc" || ext == ".cxx") {
            files.push_back(std::move(pathStr));
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
    rules.push_back(std::make_unique<misra::Rule504>());
    rules.push_back(std::make_unique<misra::Rule506>());
    rules.push_back(std::make_unique<misra::Rule508>());
    rules.push_back(std::make_unique<misra::Rule524>());
    rules.push_back(std::make_unique<misra::Rule531>());
    rules.push_back(std::make_unique<misra::Rule621>());
    rules.push_back(std::make_unique<misra::Rule645>());
    rules.push_back(std::make_unique<misra::Rule651>());
    rules.push_back(std::make_unique<misra::Rule665>());
    rules.push_back(std::make_unique<misra::Rule711>());
    rules.push_back(std::make_unique<misra::Rule731>());
    rules.push_back(std::make_unique<misra::Rule931>());
    rules.push_back(std::make_unique<misra::Rule933>());
    rules.push_back(std::make_unique<misra::Rule951>());
    rules.push_back(std::make_unique<misra::Rule1535>());
    rules.push_back(std::make_unique<misra::Rule1536>());
    rules.push_back(std::make_unique<misra::Rule1537>());
    rules.push_back(std::make_unique<misra::Rule1551>());
    rules.push_back(std::make_unique<misra::Rule1601>());
    rules.push_back(std::make_unique<misra::Rule1606>());
    rules.push_back(std::make_unique<misra::Rule1621>());
}

struct FileAnalysisResult {
    int         toolReturnCode = 0;
    bool        crashed        = false;
    std::string errorReason;
};

/// Analyze a single translation unit in isolation with its own ClangTool and
/// Rule instances, catching both C++ exceptions and OS/hardware exceptions.
static FileAnalysisResult analyzeSingleFileIsolated(
    clang::tooling::CompilationDatabase& compilations,
    const std::string& file,
    misra::ViolationStore* store,
    misra::GlobalSymbolDatabase* db) {
    FileAnalysisResult res;
    try {
        std::vector<std::unique_ptr<misra::Rule>> localRules;
        createRules(localRules);

        std::unique_ptr<clang::tooling::ClangTool> tool;
        {
            std::lock_guard<std::mutex> lock(gCompilationsMutex);
            tool = std::make_unique<clang::tooling::ClangTool>(
                compilations, std::vector<std::string>{file});
        }

        tool->appendArgumentsAdjuster(
            clang::tooling::getInsertArgumentAdjuster(
                {"-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH",
                 "-fno-access-control",
                 "-ffreestanding",
                 "-w"},
                clang::tooling::ArgumentInsertPosition::BEGIN));

        misra::MisraActionFactory factory(localRules, store, db);

#ifdef _WIN32
        unsigned long sehCode = 0;
        int rc = runToolWithSEH(tool.get(), &factory, &sehCode);
        if (rc == -999) {
            res.crashed = true;
            std::ostringstream oss;
            oss << "Hardware exception 0x" << std::hex << std::uppercase << sehCode;
            if (sehCode == EXCEPTION_STACK_OVERFLOW) {
                oss << " (STACK_OVERFLOW)";
            } else if (sehCode == EXCEPTION_ACCESS_VIOLATION) {
                oss << " (ACCESS_VIOLATION)";
            }
            res.errorReason = oss.str();
            return res;
        }
        res.toolReturnCode = rc;
#else
        res.toolReturnCode = tool->run(&factory);
#endif
    } catch (const std::exception& ex) {
        res.crashed = true;
        res.errorReason = std::string("C++ exception: ") + ex.what();
    } catch (...) {
        res.crashed = true;
        res.errorReason = "Unknown C++ exception";
    }
    return res;
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
        inferredRoot = misra::ProjectFileFilter::canonicalisePath(dir);
        sources = discoverSources(dir);
        if (sources.empty()) {
            std::cerr << "No C/C++ sources found under " << dir << "\n";
            return 1;
        }
    } else if (!sources.empty()) {
        fs::path common = fs::path(
            misra::ProjectFileFilter::canonicalisePath(sources[0])).parent_path();
        for (size_t i = 1; i < sources.size(); ++i) {
            fs::path p = fs::path(
                misra::ProjectFileFilter::canonicalisePath(sources[i])).parent_path();
            fs::path nextCommon;
            auto itA = common.begin();
            auto itB = p.begin();
            while (itA != common.end() && itB != p.end() &&
                   misra::ProjectFileFilter::normalisePath(itA->string()) ==
                       misra::ProjectFileFilter::normalisePath(itB->string())) {
                nextCommon /= *itA;
                ++itA;
                ++itB;
            }
            if (!nextCommon.empty()) {
                common = nextCommon;
            }
        }
        inferredRoot = common.string();
    }

    // Canonicalize and deduplicate input translation unit paths.
    {
        std::vector<std::string> uniqueSources;
        std::set<std::string> seenNorm;
        for (const auto& s : sources) {
            std::string canon = misra::ProjectFileFilter::canonicalisePath(s);
            std::string norm  = misra::ProjectFileFilter::normalisePath(canon);
            if (!canon.empty() && seenNorm.insert(norm).second) {
                uniqueSources.push_back(std::move(canon));
            }
        }
        sources = std::move(uniqueSources);
    }

    std::cout << "================================================================\n";
    std::cout << "  misra_checker — MISRA C++:2008 Static Analysis Tool\n";
    std::cout << "  " << sources.size() << " source file(s) to analyze\n";
    std::cout << "================================================================\n\n";

    // ── Create shared state ────────────────────────────────────
    std::string root = !ProjectRoot.empty()
        ? ProjectRoot.getValue()
        : (!inferredRoot.empty() ? inferredRoot : fs::current_path().string());

    std::string outputPath = OutputFile.getValue();

    misra::ViolationStore       store(root);
    misra::GlobalSymbolDatabase db;

    store.setScannedFiles(sources);
    store.setReportTag(makeTimestamp());

    // ── Create and register rules ──────────────────────────────
    std::vector<std::unique_ptr<misra::Rule>> templateRules;
    createRules(templateRules);

    for (const auto& rule : templateRules) {
        store.registerRule(rule->getRuleId());
    }

    // Register crash-proof emergency handlers and write initial checkpoint report
    // before analysis starts so a valid report always exists on disk.
    gEmergencyStore      = &store;
    gEmergencyOutputPath = outputPath;
    store.writeSnapshotReport(outputPath);

    std::atexit(flushEmergencyReport);
    std::signal(SIGINT,  signalHandler);
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGABRT, signalHandler);
#ifdef _WIN32
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);
    SetUnhandledExceptionFilter(unhandledExceptionFilter);
#endif

    std::cout << "Enabled rules: " << templateRules.size() << "\n";
    for (const auto& rule : templateRules) {
        std::cout << "  [" << rule->getRuleId() << "] "
                  << rule->getDescription() << "\n";
    }
    std::cout << "\n";

    // ── Parallel, Isolated Per-File Execution ──────────────────
    unsigned hwThreads = std::thread::hardware_concurrency();
    if (hwThreads == 0) hwThreads = 4;
    unsigned maxWorkers = JobsOpt.getValue() > 0
        ? JobsOpt.getValue()
        : std::min<unsigned>(hwThreads, 8u);
    maxWorkers = std::max<unsigned>(1u, std::min<unsigned>(maxWorkers, static_cast<unsigned>(sources.size())));
    unsigned timeoutSec = TimeoutOpt.getValue() > 0 ? TimeoutOpt.getValue() : 60u;

    std::cout << "Running analysis (" << maxWorkers << " parallel worker(s), "
              << timeoutSec << "s per-file timeout)...\n";

    std::atomic<size_t> nextIndex{0};
    std::atomic<size_t> parseErrorCount{0};
    std::atomic<size_t> crashOrTimeoutCount{0};
    std::atomic<bool>   anyTimedOutThreadDetached{false};

    auto workerLoop = [&]() {
        while (true) {
            size_t idx = nextIndex.fetch_add(1);
            if (idx >= sources.size()) break;

            const std::string& file = sources[idx];

            // Run the file analysis on a dedicated task with a timeout guard.
            auto promisePtr = std::make_shared<std::promise<FileAnalysisResult>>();
            std::future<FileAnalysisResult> fut = promisePtr->get_future();

            std::thread fileThread([&compilations = optParser->getCompilations(),
                                    file,
                                    &store,
                                    &db,
                                    promisePtr]() {
                FileAnalysisResult r = analyzeSingleFileIsolated(
                    compilations, file, &store, &db);
                try {
                    promisePtr->set_value(std::move(r));
                } catch (...) {
                }
            });

            auto status = fut.wait_for(std::chrono::seconds(timeoutSec));
            if (status == std::future_status::timeout) {
                // Detach the timed-out thread so the scan continues without blocking.
                fileThread.detach();
                anyTimedOutThreadDetached.store(true);
                ++crashOrTimeoutCount;

                std::string reason = "Timed out after " + std::to_string(timeoutSec) + "s";
                store.recordFileFailure(file, reason);
                {
                    std::lock_guard<std::mutex> lock(gLogMutex);
                    std::cerr << "  [TIMEOUT] " << file << ": " << reason
                              << " — continuing scan.\n";
                }
            } else {
                if (fileThread.joinable()) {
                    fileThread.join();
                }
                FileAnalysisResult res = fut.get();
                if (res.crashed) {
                    ++crashOrTimeoutCount;
                    store.recordFileFailure(file, res.errorReason);
                    std::lock_guard<std::mutex> lock(gLogMutex);
                    std::cerr << "  [CRASH/ERROR] " << file << ": " << res.errorReason
                              << " — continuing scan.\n";
                } else if (res.toolReturnCode != 0) {
                    ++parseErrorCount;
                    std::lock_guard<std::mutex> lock(gLogMutex);
                    std::cerr << "  [Warning] Clang returned non-zero ("
                              << res.toolReturnCode << ") for " << file
                              << " (continuing with partial AST).\n";
                }
            }

            // Incrementally update the report on disk after every file completes.
            store.writeSnapshotReport(outputPath);
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(maxWorkers);
    for (unsigned w = 0; w < maxWorkers; ++w) {
        workers.emplace_back(workerLoop);
    }
    for (auto& t : workers) {
        if (t.joinable()) t.join();
    }

    if (parseErrorCount.load() > 0 || crashOrTimeoutCount.load() > 0) {
        std::cerr << "\nScan Diagnostics: " << parseErrorCount.load()
                  << " file(s) had parse warnings/errors, "
                  << crashOrTimeoutCount.load()
                  << " file(s) crashed or timed out.\n";
    }

    // ── Project-level evaluation ───────────────────────────────
    std::cout << "\nEvaluating project-level rules...\n";
    db.evaluateAll(store);

    // ── Finalize and output ────────────────────────────────────
    std::cout << "\n";
    size_t total = store.finalize(outputPath);
    gEmergencyStore = nullptr;

    int exitCode = (total > 0) ? 1 : 0;
    if (anyTimedOutThreadDetached.load()) {
        std::cout.flush();
        std::cerr.flush();
        std::_Exit(exitCode);
    }
    return exitCode;
}
