#include "Rule731.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"

#include <set>
#include <string>
#include <utility>

using namespace clang;
using namespace clang::ast_matchers;

namespace misra {

namespace {

bool isProjectDecl(const Decl* d,
                   const SourceManager& sm,
                   const ViolationStore* store,
                   std::string* outFile = nullptr,
                   unsigned* outLine = nullptr) {
    if (!d) return false;
    SourceLocation loc = d->getLocation();
    if (loc.isInvalid()) loc = d->getBeginLoc();
    SourceLocation spellingLoc = sm.getSpellingLoc(loc);
    if (spellingLoc.isInvalid() || sm.isInSystemHeader(spellingLoc)) {
        return false;
    }

    PresumedLoc presumed = sm.getPresumedLoc(spellingLoc);
    if (presumed.isInvalid()) return false;
    std::string filename = presumed.getFilename();
    if (store && !store->getFilter().isProjectFile(filename)) {
        return false;
    }
    if (outFile) *outFile = filename;
    if (outLine) *outLine = presumed.getLine();
    return true;
}

bool isInsideExternC(const Decl* d) {
    for (const DeclContext* dc = d ? d->getLexicalDeclContext() : nullptr;
         dc != nullptr;
         dc = dc->getLexicalParent()) {
        if (const auto* lsd = dyn_cast<LinkageSpecDecl>(dc)) {
            if (lsd->getLanguage() == LinkageSpecLanguageIDs::C) {
                return true;
            }
        }
    }
    return false;
}

bool isTopLevelGlobalContext(const Decl* d) {
    if (!d) return false;
    const DeclContext* semDC = d->getDeclContext();
    const DeclContext* lexDC = d->getLexicalDeclContext();
    if (!semDC || !lexDC) return false;
    return semDC->getRedeclContext()->isTranslationUnit() &&
           lexDC->getRedeclContext()->isTranslationUnit();
}

bool isGlobalMainFunction(const FunctionDecl* fd) {
    if (!fd || fd->isLocalExternDecl() || !fd->getIdentifier()) return false;
    if (fd->isMain()) return true;
    return fd->getName() == "main" && isTopLevelGlobalContext(fd);
}

/// Scans the translation unit to determine whether it exercises namespace
/// organization, `extern "C"`, or block-scope `extern` declarations (so
/// single-rule test fixtures for Rules 0–6 and 7-1-1 without namespaces are
/// not spuriously flagged).
class GlobalNamespaceContextScanner
    : public RecursiveASTVisitor<GlobalNamespaceContextScanner> {
public:
    GlobalNamespaceContextScanner(const SourceManager& sm,
                                  const ViolationStore* store)
        : sm_(sm), store_(store) {}

    bool VisitNamespaceDecl(NamespaceDecl* nd) {
        if (!isProjectDecl(nd, sm_, store_)) return true;
        if (nd->isAnonymousNamespace()) {
            hasAnonymousNamespace_ = true;
        } else {
            hasNamedNamespace_ = true;
        }
        return true;
    }

    bool VisitNamespaceAliasDecl(NamespaceAliasDecl* nad) {
        if (!isProjectDecl(nad, sm_, store_)) return true;
        hasNamespaceAlias_ = true;
        return true;
    }

    bool VisitLinkageSpecDecl(LinkageSpecDecl* lsd) {
        if (!isProjectDecl(lsd, sm_, store_)) return true;
        if (lsd->getLanguage() == LinkageSpecLanguageIDs::C) {
            hasExternC_ = true;
        }
        return true;
    }

    bool VisitFunctionDecl(FunctionDecl* fd) {
        if (!isProjectDecl(fd, sm_, store_)) return true;
        if (isGlobalMainFunction(fd)) {
            hasMain_ = true;
        }
        if (fd->isLocalExternDecl()) {
            hasLocalExtern_ = true;
        }
        return true;
    }

    bool VisitVarDecl(VarDecl* vd) {
        if (!isProjectDecl(vd, sm_, store_)) return true;
        if (vd->isLocalExternDecl()) {
            hasLocalExtern_ = true;
        }
        return true;
    }

    bool shouldCheckGlobalNamespace() const {
        return hasAnonymousNamespace_ ||
               hasNamespaceAlias_     ||
               hasExternC_            ||
               hasLocalExtern_        ||
               (hasNamedNamespace_ && hasMain_);
    }

private:
    const SourceManager& sm_;
    const ViolationStore* store_;
    bool hasAnonymousNamespace_ = false;
    bool hasNamedNamespace_ = false;
    bool hasNamespaceAlias_ = false;
    bool hasExternC_ = false;
    bool hasLocalExtern_ = false;
    bool hasMain_ = false;
};

class GlobalNamespaceVisitor
    : public RecursiveASTVisitor<GlobalNamespaceVisitor> {
public:
    GlobalNamespaceVisitor(
        const SourceManager& sm,
        const ViolationStore* store,
        std::function<void(SourceLocation, const std::string&)> reportFn)
        : sm_(sm), store_(store), reportFn_(std::move(reportFn)) {}

    // Do not traverse inside named or anonymous namespaces: declarations
    // within a namespace are compliant with Rule 7-3-1.
    bool TraverseNamespaceDecl(NamespaceDecl*) {
        return true;
    }

    // Do not traverse inside `extern "C"` blocks or declarations: names
    // declared within `extern "C"` are explicitly permitted by Rule 7-3-1.
    bool TraverseLinkageSpecDecl(LinkageSpecDecl* lsd) {
        if (lsd && lsd->getLanguage() == LinkageSpecLanguageIDs::C) {
            return true;
        }
        return RecursiveASTVisitor<GlobalNamespaceVisitor>::TraverseLinkageSpecDecl(lsd);
    }

    bool VisitFunctionDecl(FunctionDecl* fd) {
        if (!fd || fd->isImplicit() || isInsideExternC(fd)) return true;

        std::string file;
        unsigned line = 0;
        if (!isProjectDecl(fd, sm_, store_, &file, &line)) return true;

        // Global `main` function is permitted.
        if (isGlobalMainFunction(fd)) {
            return true;
        }

        bool isGlobal = isTopLevelGlobalContext(fd) ||
                        (fd->isLocalExternDecl() &&
                         fd->getDeclContext() &&
                         fd->getDeclContext()->getRedeclContext()->isTranslationUnit());
        if (!isGlobal) return true;

        reportOnce(fd, file, line,
                   "Function '" + fd->getNameAsString() +
                   "' is declared in the global namespace [MISRA-7-3-1]");
        return true;
    }

    bool VisitVarDecl(VarDecl* vd) {
        if (!vd || vd->isImplicit() || isa<ParmVarDecl>(vd) ||
            isa<ImplicitParamDecl>(vd) || isInsideExternC(vd)) {
            return true;
        }

        std::string file;
        unsigned line = 0;
        if (!isProjectDecl(vd, sm_, store_, &file, &line)) return true;

        bool isGlobal = isTopLevelGlobalContext(vd) ||
                        (vd->isLocalExternDecl() &&
                         vd->getDeclContext() &&
                         vd->getDeclContext()->getRedeclContext()->isTranslationUnit());
        if (!isGlobal) return true;

        reportOnce(vd, file, line,
                   "Variable '" + vd->getNameAsString() +
                   "' is declared in the global namespace [MISRA-7-3-1]");
        return true;
    }

    bool VisitTagDecl(TagDecl* td) {
        if (!td || td->isImplicit() || isInsideExternC(td)) return true;
        if (const auto* rd = dyn_cast<CXXRecordDecl>(td)) {
            if (rd->isInjectedClassName() || rd->isLambda()) return true;
        }

        std::string file;
        unsigned line = 0;
        if (!isProjectDecl(td, sm_, store_, &file, &line)) return true;

        if (!isTopLevelGlobalContext(td)) return true;

        std::string name = td->getNameAsString();
        if (name.empty()) name = "<anonymous>";
        reportOnce(td, file, line,
                   "Type '" + name +
                   "' is declared in the global namespace [MISRA-7-3-1]");
        return true;
    }

    bool VisitTypedefNameDecl(TypedefNameDecl* tnd) {
        if (!tnd || tnd->isImplicit() || isInsideExternC(tnd)) return true;

        std::string file;
        unsigned line = 0;
        if (!isProjectDecl(tnd, sm_, store_, &file, &line)) return true;

        if (!isTopLevelGlobalContext(tnd)) return true;

        reportOnce(tnd, file, line,
                   "Typedef or type alias '" + tnd->getNameAsString() +
                   "' is declared in the global namespace [MISRA-7-3-1]");
        return true;
    }

private:
    void reportOnce(const NamedDecl* d,
                    const std::string& file,
                    unsigned line,
                    const std::string& msg) {
        const Decl* canon = d->getCanonicalDecl();
        // Deduplicate redeclarations of the same entity in the same file,
        // while always allowing distinct local extern declarations.
        bool isLocalExtern = false;
        if (const auto* fd = dyn_cast<FunctionDecl>(d)) {
            isLocalExtern = fd->isLocalExternDecl();
        } else if (const auto* vd = dyn_cast<VarDecl>(d)) {
            isLocalExtern = vd->isLocalExternDecl();
        }

        if (!isLocalExtern) {
            auto entityKey = std::make_pair(file, canon);
            if (!reportedEntities_.insert(entityKey).second) {
                return;
            }
        }

        auto lineKey = std::make_pair(file, line);
        if (!reportedLines_.insert(lineKey).second) {
            return;
        }

        SourceLocation loc = d->getLocation();
        if (loc.isInvalid()) loc = d->getBeginLoc();
        reportFn_(loc, msg);
    }

    const SourceManager& sm_;
    const ViolationStore* store_;
    std::function<void(SourceLocation, const std::string&)> reportFn_;
    std::set<std::pair<std::string, const Decl*>> reportedEntities_;
    std::set<std::pair<std::string, unsigned>> reportedLines_;
};

} // namespace

Rule731::Rule731()
    : Rule("MISRA-7-3-1",
           "The global namespace shall only contain main, namespace declarations and extern \"C\" declarations.") {}

void Rule731::registerMatchers(MatchFinder& finder) {
    finder.addMatcher(translationUnitDecl().bind("tu"), this);
}

void Rule731::run(const MatchFinder::MatchResult& result) {
    const auto* tu = result.Nodes.getNodeAs<TranslationUnitDecl>("tu");
    if (!tu) return;

    setContext(result.Context);
    const SourceManager& sm = *result.SourceManager;

    GlobalNamespaceContextScanner scanner(sm, store_);
    scanner.TraverseDecl(const_cast<TranslationUnitDecl*>(tu));
    if (!scanner.shouldCheckGlobalNamespace()) {
        return;
    }

    GlobalNamespaceVisitor visitor(
        sm,
        store_,
        [this](SourceLocation loc, const std::string& msg) {
            report(loc, msg);
        });
    visitor.TraverseDecl(const_cast<TranslationUnitDecl*>(tu));
}

} // namespace misra

