#include "Rule2102.h"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtCXX.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"

#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

using namespace clang::ast_matchers;

namespace misra {

namespace {

bool isScopedNamedDecl(const clang::NamedDecl* d,
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

    // Skip parameter declarations on bodiless function prototypes so they do not
    // pollute outer scopes or trigger false positives on forward declarations.
    if (const auto* parm = llvm::dyn_cast<clang::ParmVarDecl>(d)) {
        const auto* fn = llvm::dyn_cast_or_null<clang::FunctionDecl>(
            parm->getDeclContext());
        if (!fn || !fn->doesThisDeclarationHaveABody()) {
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

class ScopeShadowVisitor : public clang::RecursiveASTVisitor<ScopeShadowVisitor> {
public:
    using Base = clang::RecursiveASTVisitor<ScopeShadowVisitor>;

    ScopeShadowVisitor(clang::ASTContext& ctx,
                       const ViolationStore* store,
                       std::function<void(clang::SourceLocation, const std::string&)> reportFn)
        : ctx_(ctx), store_(store), reportFn_(std::move(reportFn)) {}

    bool TraverseTranslationUnitDecl(clang::TranslationUnitDecl* d) {
        ScopeGuard guard(scopes_);
        return Base::TraverseTranslationUnitDecl(d);
    }

    bool TraverseNamespaceDecl(clang::NamespaceDecl* d) {
        ScopeGuard guard(scopes_);
        return Base::TraverseNamespaceDecl(d);
    }

    bool TraverseCXXRecordDecl(clang::CXXRecordDecl* d) {
        CheckNamedDecl(d);
        ScopeGuard guard(scopes_);
        return Base::TraverseCXXRecordDecl(d);
    }

    bool TraverseRecordDecl(clang::RecordDecl* d) {
        CheckNamedDecl(d);
        ScopeGuard guard(scopes_);
        return Base::TraverseRecordDecl(d);
    }

    bool TraverseEnumDecl(clang::EnumDecl* d) {
        CheckNamedDecl(d);
        ScopeGuard guard(scopes_);
        return Base::TraverseEnumDecl(d);
    }

    bool TraverseFunctionDecl(clang::FunctionDecl* d) {
        CheckNamedDecl(d);
        ScopeGuard guard(scopes_);
        return Base::TraverseFunctionDecl(d);
    }

    bool TraverseCXXMethodDecl(clang::CXXMethodDecl* d) {
        CheckNamedDecl(d);
        ScopeGuard guard(scopes_);
        return Base::TraverseCXXMethodDecl(d);
    }

    bool TraverseCXXConstructorDecl(clang::CXXConstructorDecl* d) {
        ScopeGuard guard(scopes_);
        return Base::TraverseCXXConstructorDecl(d);
    }

    bool TraverseCXXDestructorDecl(clang::CXXDestructorDecl* d) {
        ScopeGuard guard(scopes_);
        return Base::TraverseCXXDestructorDecl(d);
    }

    bool TraverseCXXConversionDecl(clang::CXXConversionDecl* d) {
        ScopeGuard guard(scopes_);
        return Base::TraverseCXXConversionDecl(d);
    }

    bool TraverseCompoundStmt(clang::CompoundStmt* s) {
        ScopeGuard guard(scopes_);
        return Base::TraverseCompoundStmt(s);
    }

    bool TraverseForStmt(clang::ForStmt* s) {
        ScopeGuard guard(scopes_);
        return Base::TraverseForStmt(s);
    }

    bool TraverseCXXForRangeStmt(clang::CXXForRangeStmt* s) {
        ScopeGuard guard(scopes_);
        return Base::TraverseCXXForRangeStmt(s);
    }

    bool TraverseWhileStmt(clang::WhileStmt* s) {
        ScopeGuard guard(scopes_);
        return Base::TraverseWhileStmt(s);
    }

    bool TraverseIfStmt(clang::IfStmt* s) {
        ScopeGuard guard(scopes_);
        return Base::TraverseIfStmt(s);
    }

    bool TraverseSwitchStmt(clang::SwitchStmt* s) {
        ScopeGuard guard(scopes_);
        return Base::TraverseSwitchStmt(s);
    }

    bool VisitNamedDecl(clang::NamedDecl* d) {
        CheckNamedDecl(d);
        return true;
    }

private:
    struct ScopeGuard {
        explicit ScopeGuard(std::vector<std::unordered_set<std::string>>& s)
            : scopes(s) {
            scopes.emplace_back();
        }
        ~ScopeGuard() {
            scopes.pop_back();
        }
        std::vector<std::unordered_set<std::string>>& scopes;
    };

    void CheckNamedDecl(clang::NamedDecl* d) {
        if (!d || checkedDecls_.count(d)) return;
        checkedDecls_.insert(d);

        if (!isScopedNamedDecl(d, ctx_, store_)) return;
        if (scopes_.empty()) return;

        // Ignore out-of-line redeclarations (e.g. `void Class::method() {}`).
        if (d->getDeclContext() != d->getLexicalDeclContext() &&
            d != d->getCanonicalDecl()) {
            return;
        }

        std::string name = d->getName().str();
        if (name.empty()) return;

        // Check strictly outer scopes (all scopes except `scopes_.back()`).
        for (size_t i = 0; i + 1 < scopes_.size(); ++i) {
            if (scopes_[i].count(name)) {
                reportFn_(d->getLocation(),
                          "Identifier '" + name +
                          "' declared in an inner scope hides an identifier in an outer scope [MISRA-2-10-2]");
                break;
            }
        }

        scopes_.back().insert(name);
    }

    clang::ASTContext& ctx_;
    const ViolationStore* store_;
    std::function<void(clang::SourceLocation, const std::string&)> reportFn_;
    std::vector<std::unordered_set<std::string>> scopes_;
    std::unordered_set<const clang::NamedDecl*> checkedDecls_;
};

} // namespace

Rule2102::Rule2102()
    : Rule("MISRA-2-10-2",
           "Identifiers declared in an inner scope shall not hide an identifier declared in an outer scope.") {}

void Rule2102::registerMatchers(MatchFinder& finder) {
    finder.addMatcher(translationUnitDecl().bind("tu"), this);
}

void Rule2102::run(const MatchFinder::MatchResult& result) {
    const auto* tu = result.Nodes.getNodeAs<clang::TranslationUnitDecl>("tu");
    if (!tu) return;

    setContext(result.Context);

    ScopeShadowVisitor visitor(
        *result.Context,
        store_,
        [this](clang::SourceLocation loc, const std::string& msg) {
            report(loc, msg);
        });
    visitor.TraverseDecl(const_cast<clang::TranslationUnitDecl*>(tu));
}

} // namespace misra

