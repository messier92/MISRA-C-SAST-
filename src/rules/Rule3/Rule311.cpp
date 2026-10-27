#include "Rule311.h"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"

#include <filesystem>
#include <functional>
#include <string>

using namespace clang::ast_matchers;

namespace misra {

namespace {

bool isHeaderFileLocation(clang::SourceLocation loc,
                          clang::ASTContext& ctx,
                          const ViolationStore* store) {
    const auto& sm = ctx.getSourceManager();
    clang::SourceLocation spellingLoc = sm.getSpellingLoc(loc);
    if (spellingLoc.isInvalid() || sm.isInSystemHeader(spellingLoc)) {
        return false;
    }

    clang::PresumedLoc presumed = sm.getPresumedLoc(spellingLoc);
    if (presumed.isInvalid()) return false;

    std::string filename = presumed.getFilename();
    if (store && !store->getFilter().isProjectFile(filename)) {
        return false;
    }

    if (!sm.isInMainFile(spellingLoc)) {
        return true;
    }

    auto ext = std::filesystem::path(filename).extension().string();
    return ext == ".h" || ext == ".hh" || ext == ".hpp" || ext == ".hxx";
}

class HeaderODRVisitor : public clang::RecursiveASTVisitor<HeaderODRVisitor> {
public:
    HeaderODRVisitor(
        clang::ASTContext& ctx,
        const ViolationStore* store,
        std::function<void(clang::SourceLocation, const std::string&)> reportFn)
        : ctx_(ctx), store_(store), reportFn_(std::move(reportFn)) {}

    bool VisitFunctionDecl(clang::FunctionDecl* fn) {
        if (!fn || fn->isImplicit()) return true;
        if (!fn->getDeclContext() || !fn->getDeclContext()->isFileContext()) {
            return true;
        }
        if (!isHeaderFileLocation(fn->getLocation(), ctx_, store_)) {
            return true;
        }

        // Templates and template specializations are allowed in headers.
        if (fn->getDescribedFunctionTemplate() != nullptr ||
            fn->isFunctionTemplateSpecialization()) {
            return true;
        }

        // Inline and constexpr functions are allowed in headers.
        if (fn->isInlineSpecified() || fn->isConstexpr()) {
            return true;
        }

        // Non-inline function definition or non-inline static function declaration
        // in a header file violates MISRA-3-1-1.
        if (fn->isThisDeclarationADefinition() ||
            fn->getStorageClass() == clang::SC_Static) {
            reportFn_(fn->getLocation(),
                      "Function '" + fn->getNameAsString() +
                      "' defined or declared static in a header file violates the One Definition Rule [MISRA-3-1-1]");
        }

        return true;
    }

    bool VisitVarDecl(clang::VarDecl* vd) {
        if (!vd || vd->isImplicit() || !vd->isFileVarDecl()) return true;
        if (!vd->getDeclContext() || !vd->getDeclContext()->isFileContext()) {
            return true;
        }
        if (!isHeaderFileLocation(vd->getLocation(), ctx_, store_)) {
            return true;
        }

        // Skip variable templates and specializations.
        if (vd->getDescribedVarTemplate() != nullptr ||
            vd->getTemplateSpecializationKind() != clang::TSK_Undeclared) {
            return true;
        }

        // Non-defining `extern` declarations are compliant in headers.
        if (vd->hasExternalStorage() &&
            vd->isThisDeclarationADefinition() == clang::VarDecl::DeclarationOnly) {
            return true;
        }

        // `constexpr`, `inline`, and `const` static constants are compliant in headers.
        if (vd->isConstexpr() || vd->isInline() ||
            vd->getType().isConstQualified()) {
            return true;
        }

        if (vd->isThisDeclarationADefinition() != clang::VarDecl::DeclarationOnly) {
            reportFn_(vd->getLocation(),
                      "Variable '" + vd->getNameAsString() +
                      "' defined in a header file violates the One Definition Rule [MISRA-3-1-1]");
        }

        return true;
    }

private:
    clang::ASTContext& ctx_;
    const ViolationStore* store_;
    std::function<void(clang::SourceLocation, const std::string&)> reportFn_;
};

} // namespace

Rule311::Rule311()
    : Rule("MISRA-3-1-1",
           "It shall be possible to include any header file in multiple translation units without violating the One Definition Rule.") {}

void Rule311::registerMatchers(MatchFinder& finder) {
    finder.addMatcher(translationUnitDecl().bind("tu"), this);
}

void Rule311::run(const MatchFinder::MatchResult& result) {
    const auto* tu = result.Nodes.getNodeAs<clang::TranslationUnitDecl>("tu");
    if (!tu) return;

    setContext(result.Context);

    HeaderODRVisitor visitor(
        *result.Context,
        store_,
        [this](clang::SourceLocation loc, const std::string& msg) {
            report(loc, msg);
        });
    visitor.TraverseDecl(const_cast<clang::TranslationUnitDecl*>(tu));
}

} // namespace misra

