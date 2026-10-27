#include "Rule323.h"

#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>

using namespace clang::ast_matchers;

namespace misra {

namespace {

std::string normalizePath(const std::string& path) {
    if (path.empty()) return "";
    std::error_code ec;
    auto abs = std::filesystem::weakly_canonical(path, ec);
    if (!ec) return abs.string();
    return std::filesystem::path(path).lexically_normal().string();
}

class ExternObjectFileVisitor
    : public clang::RecursiveASTVisitor<ExternObjectFileVisitor> {
public:
    ExternObjectFileVisitor(
        clang::ASTContext& ctx,
        const ViolationStore* store,
        std::function<void(clang::SourceLocation, const std::string&)> reportFn)
        : ctx_(ctx), store_(store), reportFn_(std::move(reportFn)) {}

    bool VisitVarDecl(clang::VarDecl* vd) {
        if (!vd || vd->isImplicit() || !vd->isFileVarDecl()) return true;
        if (vd->getStorageClass() != clang::SC_Extern ||
            vd->isThisDeclarationADefinition() != clang::VarDecl::DeclarationOnly) {
            return true;
        }

        const auto& sm = ctx_.getSourceManager();
        clang::SourceLocation spellingLoc = sm.getSpellingLoc(vd->getLocation());
        if (spellingLoc.isInvalid() || sm.isInSystemHeader(spellingLoc)) {
            return true;
        }

        clang::PresumedLoc presumed = sm.getPresumedLoc(spellingLoc);
        if (presumed.isInvalid()) return true;

        std::string filename = presumed.getFilename();
        if (store_ && !store_->getFilter().isProjectFile(filename)) {
            return true;
        }

        std::string normFile = normalizePath(filename);
        std::string qname = vd->getQualifiedNameAsString();
        if (qname.empty()) return true;

        auto it = firstDeclFileByObject_.find(qname);
        if (it == firstDeclFileByObject_.end()) {
            firstDeclFileByObject_.emplace(qname, normFile);
        } else if (it->second != normFile) {
            reportFn_(vd->getLocation(),
                      "External object '" + qname +
                      "' is declared in multiple files [MISRA-3-2-3]");
        }

        return true;
    }

private:
    clang::ASTContext& ctx_;
    const ViolationStore* store_;
    std::function<void(clang::SourceLocation, const std::string&)> reportFn_;
    std::unordered_map<std::string, std::string> firstDeclFileByObject_;
};

} // namespace

Rule323::Rule323()
    : Rule("MISRA-3-2-3",
           "A type, object or function that is used in multiple translation units shall be declared in one and only one file.") {}

void Rule323::registerMatchers(MatchFinder& finder) {
    finder.addMatcher(translationUnitDecl().bind("tu"), this);
}

void Rule323::run(const MatchFinder::MatchResult& result) {
    const auto* tu = result.Nodes.getNodeAs<clang::TranslationUnitDecl>("tu");
    if (!tu) return;

    setContext(result.Context);

    ExternObjectFileVisitor visitor(
        *result.Context,
        store_,
        [this](clang::SourceLocation loc, const std::string& msg) {
            report(loc, msg);
        });
    visitor.TraverseDecl(const_cast<clang::TranslationUnitDecl*>(tu));
}

} // namespace misra

