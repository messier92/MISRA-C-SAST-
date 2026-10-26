#ifndef MISRA_CHECKER_H
#define MISRA_CHECKER_H

#include "GlobalSymbolDatabase.h"
#include "Rule.h"
#include "ViolationStore.h"

#include "clang/AST/ASTConsumer.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

#include <memory>
#include <vector>

namespace misra {

/// ASTConsumer that wires all Rule instances into a MatchFinder
/// and runs them over the translation unit's AST.
class MisraConsumer : public clang::ASTConsumer {
public:
    MisraConsumer(std::vector<std::unique_ptr<Rule>>& rules,
                  ViolationStore* store,
                  GlobalSymbolDatabase* db);

    void HandleTranslationUnit(clang::ASTContext& ctx) override;

private:
    std::vector<std::unique_ptr<Rule>>& rules_;
    ViolationStore*                     store_;
    GlobalSymbolDatabase*               db_;
};

/// ASTFrontendAction that creates a MisraConsumer for each TU.
/// The rules, store, and database are shared across all TUs.
class MisraChecker : public clang::ASTFrontendAction {
public:
    MisraChecker(std::vector<std::unique_ptr<Rule>>& rules,
                 ViolationStore* store,
                 GlobalSymbolDatabase* db);

    std::unique_ptr<clang::ASTConsumer>
    CreateASTConsumer(clang::CompilerInstance& ci,
                      llvm::StringRef file) override;

private:
    std::vector<std::unique_ptr<Rule>>& rules_;
    ViolationStore*                     store_;
    GlobalSymbolDatabase*               db_;
};

/// Factory for creating MisraChecker actions.
/// Used with ClangTool::run().
class MisraActionFactory
    : public clang::tooling::FrontendActionFactory {
public:
    MisraActionFactory(std::vector<std::unique_ptr<Rule>>& rules,
                       ViolationStore* store,
                       GlobalSymbolDatabase* db);

    std::unique_ptr<clang::FrontendAction> create() override;

private:
    std::vector<std::unique_ptr<Rule>>& rules_;
    ViolationStore*                     store_;
    GlobalSymbolDatabase*               db_;
};

} // namespace misra

#endif // MISRA_CHECKER_H
