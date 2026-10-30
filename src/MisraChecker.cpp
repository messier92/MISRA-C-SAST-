#include "MisraChecker.h"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Frontend/CompilerInstance.h"

#include <iostream>

namespace misra {

// ── MisraConsumer ──────────────────────────────────────────────

MisraConsumer::MisraConsumer(std::vector<std::unique_ptr<Rule>>& rules,
                             ViolationStore* store,
                             GlobalSymbolDatabase* db)
    : rules_(rules), store_(store), db_(db) {}

void MisraConsumer::HandleTranslationUnit(clang::ASTContext& ctx) {
    for (auto& rule : rules_) {
        clang::ast_matchers::MatchFinder finder;
        rule->setStore(store_);
        rule->setDatabase(db_);
        rule->setContext(&ctx);
        rule->registerMatchers(finder);
        finder.matchAST(ctx);
    }
}

// ── MisraChecker (FrontendAction) ──────────────────────────────

MisraChecker::MisraChecker(std::vector<std::unique_ptr<Rule>>& rules,
                           ViolationStore* store,
                           GlobalSymbolDatabase* db)
    : rules_(rules), store_(store), db_(db) {}

std::unique_ptr<clang::ASTConsumer>
MisraChecker::CreateASTConsumer(clang::CompilerInstance& /*ci*/,
                                llvm::StringRef file) {
    std::cout << "  Analyzing: " << file.str() << "\n";
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
