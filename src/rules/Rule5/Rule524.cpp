#include "Rule524.h"

#include "clang/AST/Expr.h"

using namespace clang;
using namespace clang::ast_matchers;

namespace misra {

Rule524::Rule524()
    : Rule("MISRA-5-2-4",
           "C-style casts (other than void casts) shall not be used.") {}

void Rule524::registerMatchers(MatchFinder& finder) {
    // Match any C-style cast expression.
    finder.addMatcher(
        cStyleCastExpr().bind("cast"),
        this);
}

void Rule524::run(const MatchFinder::MatchResult& result) {
    const auto* cast = result.Nodes.getNodeAs<CStyleCastExpr>("cast");
    if (!cast) return;

    // Allow casts to void — those are intentional "discard return value".
    if (cast->getType()->isVoidType()) return;

    // Skip compiler-generated or system-header locations.
    SourceLocation loc = cast->getBeginLoc();
    if (loc.isInvalid()) return;

    const SourceManager& sm = *result.SourceManager;
    if (sm.isInSystemHeader(loc)) return;

    report(loc, "C-style cast used; prefer static_cast, "
                "const_cast, or reinterpret_cast.");
}

} // namespace misra
