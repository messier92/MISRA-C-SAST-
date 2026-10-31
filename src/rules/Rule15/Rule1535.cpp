#include "Rule1535.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/StmtCXX.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"

using namespace clang;
using namespace clang::ast_matchers;

namespace misra {

Rule1535::Rule1535()
    : Rule("MISRA-15-3-5",
           "A class type exception shall always be caught by reference.") {}

void Rule1535::registerMatchers(MatchFinder& finder) {
    finder.addMatcher(cxxCatchStmt().bind("catchStmt"), this);
}

void Rule1535::run(const MatchFinder::MatchResult& result) {
    const auto* catchStmt = result.Nodes.getNodeAs<CXXCatchStmt>("catchStmt");
    if (!catchStmt || !result.Context || !result.SourceManager) return;

    // Ignore catch-all `catch (...)` handlers.
    QualType caughtType = catchStmt->getCaughtType();
    if (caughtType.isNull()) return;

    setContext(result.Context);
    const SourceManager& sm = *result.SourceManager;

    SourceLocation loc = catchStmt->getCatchLoc();
    SourceLocation spellLoc = sm.getSpellingLoc(loc);
    if (spellLoc.isInvalid() || sm.isInSystemHeader(spellLoc)) return;

    PresumedLoc presumed = sm.getPresumedLoc(spellLoc);
    if (presumed.isInvalid()) return;
    if (store_ && !store_->getFilter().isProjectFile(presumed.getFilename())) {
        return;
    }

    // Resolve typedefs/type aliases via canonical type.
    QualType canonType = caughtType.getCanonicalType();
    if (canonType->isDependentType()) return;

    // Compliant if caught by reference (lvalue or rvalue reference, const or non-const).
    if (canonType->isReferenceType()) return;

    // Flag if the underlying non-reference type is a class/struct/union type.
    if (canonType->isRecordType()) {
        std::string typeName = caughtType.getAsString();
        report(loc,
               "Class type exception '" + typeName +
               "' shall be caught by reference [MISRA-15-3-5]");
    }
}

} // namespace misra

