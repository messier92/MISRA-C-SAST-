#include "Rule2104.h"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace clang::ast_matchers;

namespace misra {

namespace {

bool isCandidateDecl(const clang::NamedDecl* d,
                     clang::ASTContext& ctx,
                     const ViolationStore* store) {
    if (!d || d->isImplicit() || !d->getIdentifier()) return false;

    if (const auto* fn = llvm::dyn_cast<clang::FunctionDecl>(d)) {
        if (llvm::isa<clang::CXXConstructorDecl>(fn) ||
            llvm::isa<clang::CXXDestructorDecl>(fn)  ||
            llvm::isa<clang::CXXConversionDecl>(fn)  ||
            fn->isOverloadedOperator()) {
            return false;
        }
    }

    if (!(llvm::isa<clang::VarDecl>(d)          ||
          llvm::isa<clang::FunctionDecl>(d)     ||
          llvm::isa<clang::FieldDecl>(d)        ||
          llvm::isa<clang::TypedefNameDecl>(d)  ||
          llvm::isa<clang::TagDecl>(d)          ||
          llvm::isa<clang::EnumConstantDecl>(d))) {
        return false;
    }

    const auto& sm = ctx.getSourceManager();
    clang::SourceLocation spellingLoc = sm.getSpellingLoc(d->getLocation());
    if (spellingLoc.isInvalid() || sm.isInSystemHeader(spellingLoc)) return false;

    clang::PresumedLoc presumed = sm.getPresumedLoc(spellingLoc);
    if (presumed.isInvalid()) return false;
    if (store && !store->getFilter().isProjectFile(presumed.getFilename())) {
        return false;
    }
    return true;
}

std::string getNamespaceQualification(const clang::Decl* d) {
    std::vector<std::string> parts;
    const clang::DeclContext* dc = d ? d->getDeclContext() : nullptr;
    while (dc) {
        if (const auto* ns = llvm::dyn_cast<clang::NamespaceDecl>(dc)) {
            if (!ns->isAnonymousNamespace() && ns->getIdentifier()) {
                parts.push_back(ns->getName().str());
            }
        }
        dc = dc->getParent();
    }
    std::string qual;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        if (!qual.empty()) qual += "::";
        qual += *it;
    }
    return qual;
}

bool isObjectOrFunction(const clang::NamedDecl* d) {
    return llvm::isa<clang::VarDecl>(d) || llvm::isa<clang::FunctionDecl>(d);
}

class TagUniquenessVisitor
    : public clang::RecursiveASTVisitor<TagUniquenessVisitor> {
public:
    TagUniquenessVisitor(
        clang::ASTContext& ctx,
        const ViolationStore* store,
        std::function<void(clang::SourceLocation, const std::string&)> reportFn)
        : ctx_(ctx), store_(store), reportFn_(std::move(reportFn)) {}

    bool VisitNamedDecl(clang::NamedDecl* d) {
        if (!isCandidateDecl(d, ctx_, store_)) return true;

        std::string name = d->getName().str();
        if (name.empty()) return true;

        std::string key = getNamespaceQualification(d) + "::" + name;
        auto& prevDecls = seenByKey_[key];

        bool isCurrTag = llvm::isa<clang::TagDecl>(d);
        const clang::DeclContext* currScope =
            d->getDeclContext() ? d->getDeclContext()->getRedeclContext() : nullptr;

        for (const clang::NamedDecl* prev : prevDecls) {
            if (prev->getCanonicalDecl() == d->getCanonicalDecl()) {
                continue;
            }
            bool isPrevTag = llvm::isa<clang::TagDecl>(prev);
            if (!isPrevTag && !isCurrTag) {
                continue;
            }

            const clang::DeclContext* prevScope =
                prev->getDeclContext() ? prev->getDeclContext()->getRedeclContext() : nullptr;

            // Same-scope collisions between a type (TagDecl) and an object or
            // function are governed by MISRA-2-10-6 rather than MISRA-2-10-4.
            if (currScope && currScope == prevScope &&
                ((isPrevTag && isObjectOrFunction(d)) ||
                 (isCurrTag && isObjectOrFunction(prev)))) {
                continue;
            }

            reportFn_(d->getLocation(),
                      "Class, union or enum name '" + name +
                      "' shall be a unique identifier [MISRA-2-10-4]");
            break;
        }

        prevDecls.push_back(d);
        return true;
    }

private:
    clang::ASTContext& ctx_;
    const ViolationStore* store_;
    std::function<void(clang::SourceLocation, const std::string&)> reportFn_;
    std::unordered_map<std::string, std::vector<const clang::NamedDecl*>> seenByKey_;
};

} // namespace

Rule2104::Rule2104()
    : Rule("MISRA-2-10-4",
           "A class, union or enum name (including qualification, if any) shall be a unique identifier.") {}

void Rule2104::registerMatchers(MatchFinder& finder) {
    finder.addMatcher(translationUnitDecl().bind("tu"), this);
}

void Rule2104::run(const MatchFinder::MatchResult& result) {
    const auto* tu = result.Nodes.getNodeAs<clang::TranslationUnitDecl>("tu");
    if (!tu) return;

    setContext(result.Context);

    TagUniquenessVisitor visitor(
        *result.Context,
        store_,
        [this](clang::SourceLocation loc, const std::string& msg) {
            report(loc, msg);
        });
    visitor.TraverseDecl(const_cast<clang::TranslationUnitDecl*>(tu));
}

} // namespace misra

