#include "MisraChecker.h"
#include "rules/Rule16/PreprocessorTracker.h"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Frontend/CompilerInstance.h"

#include <exception>
#include <iostream>
#include <mutex>

namespace misra {

namespace {
std::mutex gConsoleMutex;
} // namespace

// ── MisraConsumer ──────────────────────────────────────────────

MisraConsumer::MisraConsumer(std::vector<std::unique_ptr<Rule>>& rules,
                             ViolationStore* store,
                             GlobalSymbolDatabase* db)
    : rules_(rules), store_(store), db_(db) {}

void MisraConsumer::HandleTranslationUnit(clang::ASTContext& ctx) {
    // Restrict AST traversal scope to declarations whose SourceLocation belongs
    // to the configured project root (including project .cpp, .c, .h, .hpp files),
    // skipping AST nodes from Qt installation directories, MSVC toolchain,
    // Windows SDK directories, compiler builtin headers, and third-party dependencies.
    const auto& sm = ctx.getSourceManager();
    std::vector<clang::Decl*> projectDecls;
    if (auto* tu = ctx.getTranslationUnitDecl()) {
        for (clang::Decl* d : tu->decls()) {
            if (!d) continue;
            clang::SourceLocation loc = d->getLocation();
            if (loc.isInvalid()) loc = d->getBeginLoc();
            if (Rule::isProjectLocation(loc, sm, store_)) {
                projectDecls.push_back(d);
            }
        }
    }
    ctx.setTraversalScope(projectDecls);

    for (auto& rule : rules_) {
        try {
            clang::ast_matchers::MatchFinder finder;
            rule->setStore(store_);
            rule->setDatabase(db_);
            rule->setContext(&ctx);
            rule->registerMatchers(finder);
            finder.matchAST(ctx);
        } catch (const std::exception& ex) {
            std::lock_guard<std::mutex> lock(gConsoleMutex);
            std::cerr << "  [Rule Error] Exception in " << rule->getRuleId()
                      << ": " << ex.what() << "\n";
        } catch (...) {
            std::lock_guard<std::mutex> lock(gConsoleMutex);
            std::cerr << "  [Rule Error] Unknown exception in "
                      << rule->getRuleId() << "\n";
        }
    }
}

// ── MisraChecker (FrontendAction) ──────────────────────────────

MisraChecker::MisraChecker(std::vector<std::unique_ptr<Rule>>& rules,
                           ViolationStore* store,
                           GlobalSymbolDatabase* db)
    : rules_(rules), store_(store), db_(db) {}

std::unique_ptr<clang::ASTConsumer>
MisraChecker::CreateASTConsumer(clang::CompilerInstance& ci,
                                llvm::StringRef file) {
    {
        std::lock_guard<std::mutex> lock(gConsoleMutex);
        std::cout << "  Analyzing: " << file.str() << std::endl;
    }
    ci.getPreprocessor().addPPCallbacks(
        std::make_unique<MisraPPCallbacks>(ci.getSourceManager(), store_));
    return std::make_unique<MisraConsumer>(rules_, store_, db_);
}

// ── MisraActionFactory ─────────────────────────────────────────

MisraActionFactory::MisraActionFactory(
    std::vector<std::unique_ptr<Rule>>& rules,
    ViolationStore* store,
    GlobalSymbolDatabase* db)
    : rules_(rules), store_(store), db_(db) {}

std::unique_ptr<clang::FrontendAction> MisraActionFactory::create() {
    return std::make_unique<MisraChecker>(rules_, store_, db_);
}

} // namespace misra
