#include "Rule951.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"

using namespace clang;
using namespace clang::ast_matchers;

namespace misra {

Rule951::Rule951()
    : Rule("MISRA-9-5-1",
           "Unions shall not be used.") {}

void Rule951::registerMatchers(MatchFinder& finder) {
    finder.addMatcher(recordDecl(isUnion()).bind("unionDecl"), this);
}

void Rule951::run(const MatchFinder::MatchResult& result) {
    const auto* rd = result.Nodes.getNodeAs<RecordDecl>("unionDecl");
    if (!rd || !result.Context || !result.SourceManager) return;

    // Skip compiler-synthesized injected-class-name declarations.
    if (rd->isImplicit()) return;

    // Skip implicit template instantiations so union templates are reported once.
    if (const auto* cxxRd = llvm::dyn_cast<CXXRecordDecl>(rd)) {
        if (cxxRd->getTemplateSpecializationKind() == TSK_ImplicitInstantiation) {
            return;
        }
    }

    // Avoid duplicate reports when a union has both a forward declaration and a
    // definition: report the definition if one exists in the TU, otherwise
    // report only the canonical (first) forward declaration.
    if (rd->getDefinition() != nullptr) {
        if (!rd->isThisDeclarationADefinition()) return;
    } else {
        if (rd != rd->getCanonicalDecl()) return;
    }

    setContext(result.Context);
    const SourceManager& sm = *result.SourceManager;

    SourceLocation loc = rd->getLocation();
    if (loc.isInvalid()) {
        loc = rd->getBeginLoc();
    }
    SourceLocation spellLoc = sm.getSpellingLoc(loc);
    if (spellLoc.isInvalid() || sm.isInSystemHeader(spellLoc)) return;

    PresumedLoc presumed = sm.getPresumedLoc(spellLoc);
    if (presumed.isInvalid()) return;
    if (store_ && !store_->getFilter().isProjectFile(presumed.getFilename())) {
        return;
    }

    std::string name = rd->getNameAsString();
    std::string msg = name.empty()
        ? "Union declaration shall not be used [MISRA-9-5-1]"
        : "Union '" + name + "' shall not be used [MISRA-9-5-1]";

    report(loc, msg);
}

} // namespace misra
