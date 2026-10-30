#include "Rule531.h"

#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"

using namespace clang;
using namespace clang::ast_matchers;

namespace misra {

namespace {

bool isEffectivelyBooleanExpr(const Expr* e) {
    if (!e) return true;
    while (e) {
        const Expr* next = e->IgnoreParenImpCasts();
        if (const auto* ewc = dyn_cast<ExprWithCleanups>(next)) {
            next = ewc->getSubExpr();
        } else if (const auto* bte = dyn_cast<CXXBindTemporaryExpr>(next)) {
            next = bte->getSubExpr();
        } else if (const auto* mte = dyn_cast<MaterializeTemporaryExpr>(next)) {
            next = mte->getSubExpr();
        }
        if (next == e) break;
        e = next;
    }
    if (!e || e->getType().isNull()) return true;
    return e->getType()->isBooleanType();
}

} // namespace

Rule531::Rule531()
    : Rule("MISRA-5-3-1",
           "Each operand of the ! operator, the logical && or the logical || operators shall have type bool.") {}

void Rule531::registerMatchers(MatchFinder& finder) {
    finder.addMatcher(
        unaryOperator(hasOperatorName("!")).bind("lnot"),
        this);
    finder.addMatcher(
        binaryOperator(anyOf(hasOperatorName("&&"), hasOperatorName("||"))).bind("logical"),
        this);
}

void Rule531::run(const MatchFinder::MatchResult& result) {
    setContext(result.Context);
    const SourceManager& sm = *result.SourceManager;

    if (const auto* uop = result.Nodes.getNodeAs<UnaryOperator>("lnot")) {
        SourceLocation loc = uop->getOperatorLoc();
        if (loc.isInvalid() || sm.isInSystemHeader(sm.getSpellingLoc(loc))) {
            return;
        }
        if (!isEffectivelyBooleanExpr(uop->getSubExpr())) {
            report(loc,
                   "Operand of logical '!' operator does not have type bool [MISRA-5-3-1]");
        }
        return;
    }

    if (const auto* bop = result.Nodes.getNodeAs<BinaryOperator>("logical")) {
        SourceLocation loc = bop->getOperatorLoc();
        if (loc.isInvalid() || sm.isInSystemHeader(sm.getSpellingLoc(loc))) {
            return;
        }
        bool lhsBool = isEffectivelyBooleanExpr(bop->getLHS());
        bool rhsBool = isEffectivelyBooleanExpr(bop->getRHS());
        if (!lhsBool || !rhsBool) {
            report(loc,
                   "Operand of logical '" + bop->getOpcodeStr().str() +
                   "' operator does not have type bool [MISRA-5-3-1]");
        }
    }
}

} // namespace misra

