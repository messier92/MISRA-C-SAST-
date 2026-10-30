#include "Rule524.h"

#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"

using namespace clang;
using namespace clang::ast_matchers;

namespace misra {

Rule524::Rule524()
    : Rule("MISRA-5-2-4",
           "C-style casts (other than void casts) and functional notation casts (other than explicit constructor calls) shall not be used.") {}

void Rule524::registerMatchers(MatchFinder& finder) {
    finder.addMatcher(cStyleCastExpr().bind("cstyle"), this);
    finder.addMatcher(cxxFunctionalCastExpr().bind("funccast"), this);
}

void Rule524::run(const MatchFinder::MatchResult& result) {
    setContext(result.Context);
    const SourceManager& sm = *result.SourceManager;

    if (const auto* cast = result.Nodes.getNodeAs<CStyleCastExpr>("cstyle")) {
        if (cast->getType()->isVoidType()) return;

        SourceLocation loc = cast->getBeginLoc();
        if (loc.isInvalid() || sm.isInSystemHeader(sm.getSpellingLoc(loc))) {
            return;
        }

        report(loc,
               "C-style cast used; prefer static_cast, const_cast, or reinterpret_cast [MISRA-5-2-4]");
        return;
    }

    if (const auto* fcast = result.Nodes.getNodeAs<CXXFunctionalCastExpr>("funccast")) {
        if (fcast->getType()->isVoidType()) return;

        SourceLocation loc = fcast->getBeginLoc();
        if (loc.isInvalid() || sm.isInSystemHeader(sm.getSpellingLoc(loc))) {
            return;
        }

        // Brace-enclosed initialization (e.g. `Base{10}` or `std::uint32_t{0}`)
        // has an invalid LParenLoc or wraps an InitListExpr and is compliant.
        if (fcast->getLParenLoc().isInvalid()) return;
        const Expr* sub = fcast->getSubExpr() ? fcast->getSubExpr()->IgnoreImplicit() : nullptr;
        if (sub && (isa<InitListExpr>(sub) || isa<CXXStdInitializerListExpr>(sub))) {
            return;
        }

        // Explicit constructor calls on class/struct/union types are allowed.
        if (fcast->getType()->isRecordType() ||
            fcast->getCastKind() == CK_ConstructorConversion ||
            (sub && isa<CXXConstructExpr>(sub))) {
            return;
        }

        report(loc,
               "Functional notation cast used; prefer static_cast, const_cast, or reinterpret_cast [MISRA-5-2-4]");
    }
}

} // namespace misra
