#include "Rule2106.h"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"

#include <functional>
#include <map>
#include <string>
#include <unordered_set>
#include <utility>

using namespace clang::ast_matchers;

namespace misra {

namespace {

bool isTypeDecl(const clang::NamedDecl* d) {
    return llvm::isa<clang::TagDecl>(d) || llvm::isa<clang::TypedefNameDecl>(d);
}

bool isObjectOrFunctionDecl(const clang::NamedDecl* d) {
    if (const auto* fn = llvm::dyn_cast<clang::FunctionDecl>(d)) {
        if (llvm::isa<clang::CXXConstructorDecl>(fn) ||
            llvm::isa<clang::CXXDestructorDecl>(fn)  ||
            llvm::isa<clang::CXXConversionDecl>(fn)  ||
            fn->isOverloadedOperator()) {
            return false;
        }
        return true;
    }
    return llvm::isa<clang::VarDecl>(d) || llvm::isa<clang::FieldDecl>(d);
}

bool isRelevantDecl(const clang::NamedDecl* d,
                    clang::ASTContext& ctx,
                    const ViolationStore* store) {
    if (!d || d->isImplicit() || !d->getIdentifier()) return false;
    if (!isTypeDecl(d) && !isObjectOrFunctionDecl(d)) return false;

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

class TypeObjectSameScopeVisitor
    : public clang::RecursiveASTVisitor<TypeObjectSameScopeVisitor> {
public:
    TypeObjectSameScopeVisitor(
        clang::ASTContext& ctx,
        const ViolationStore* store,
        std::function<void(clang::SourceLocation, const std::string&)> reportFn)
        : ctx_(ctx), store_(store), reportFn_(std::move(reportFn)) {}

    bool VisitNamedDecl(clang::NamedDecl* d) {
        if (!isRelevantDecl(d, ctx_, store_)) return true;

        // Deduplicate redeclarations (e.g. a function both declared and defined
        // in the same scope shall only be reported once).
        const clang::Decl* canon = d->getCanonicalDecl();
        if (!seenCanonicals_.insert(canon).second) {
            return true;
        }

        const clang::DeclContext* scope =
            d->getDeclContext() ? d->getDeclContext()->getRedeclContext() : nullptr;
        if (!scope) return true;

        std::string name = d->getName().str();
        if (name.empty()) return true;

        ScopeNameKey key{scope, name};
        ScopeEntry& entry = entries_[key];

        if (isTypeDecl(d)) {
            if (entry.hasObjectOrFunction) {
                reportFn_(d->getLocation(),
                          "Identifier '" + name +
                          "' refers to both a type and an object or function in the same scope [MISRA-2-10-6]");
            }
            entry.hasType = true;
        } else if (isObjectOrFunctionDecl(d)) {
            if (entry.hasType) {
                reportFn_(d->getLocation(),
                          "Identifier '" + name +
                          "' refers to both a type and an object or function in the same scope [MISRA-2-10-6]");
            }
            entry.hasObjectOrFunction = true;
        }

        return true;
    }

private:
    using ScopeNameKey = std::pair<const clang::DeclContext*, std::string>;

    struct ScopeEntry {
        bool hasType = false;
        bool hasObjectOrFunction = false;
    };

    clang::ASTContext& ctx_;
    const ViolationStore* store_;
    std::function<void(clang::SourceLocation, const std::string&)> reportFn_;
    std::map<ScopeNameKey, ScopeEntry> entries_;
    std::unordered_set<const clang::Decl*> seenCanonicals_;
};

} // namespace

Rule2106::Rule2106()
    : Rule("MISRA-2-10-6",
           "If an identifier refers to a type, it shall not also refer to an object or a function in the same scope.") {}

void Rule2106::registerMatchers(MatchFinder& finder) {
    finder.addMatcher(translationUnitDecl().bind("tu"), this);
}

void Rule2106::run(const MatchFinder::MatchResult& result) {
    const auto* tu = result.Nodes.getNodeAs<clang::TranslationUnitDecl>("tu");
    if (!tu) return;

    setContext(result.Context);

    TypeObjectSameScopeVisitor visitor(
        *result.Context,
        store_,
        [this](clang::SourceLocation loc, const std::string& msg) {
            report(loc, msg);
        });
    visitor.TraverseDecl(const_cast<clang::TranslationUnitDecl*>(tu));
}

} // namespace misra

