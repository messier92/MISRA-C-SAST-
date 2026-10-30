#include "Rule508.h"

#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"

#include <functional>
#include <string>

using namespace clang::ast_matchers;

namespace misra {

namespace {

const clang::Expr* unwrapImplicitAndParens(const clang::Expr* e) {
    while (e) {
        const clang::Expr* next = e->IgnoreParenImpCasts();
        if (const auto* ewc = llvm::dyn_cast<clang::ExprWithCleanups>(next)) {
            next = ewc->getSubExpr();
        } else if (const auto* bte = llvm::dyn_cast<clang::CXXBindTemporaryExpr>(next)) {
            next = bte->getSubExpr();
        } else if (const auto* mte = llvm::dyn_cast<clang::MaterializeTemporaryExpr>(next)) {
            next = mte->getSubExpr();
        }
        if (next == e) break;
        e = next;
    }
    return e;
}

bool isNonBoolIntegralType(clang::QualType qt) {
    if (qt.isNull()) return false;
    qt = qt.getNonReferenceType().getUnqualifiedType();
    return qt->isIntegerType() && !qt->isBooleanType() && !qt->isEnumeralType();
}

bool isArithmeticNonBoolType(clang::QualType qt) {
    if (qt.isNull()) return false;
    qt = qt.getNonReferenceType().getUnqualifiedType();
    return isNonBoolIntegralType(qt) || qt->isFloatingType();
}

bool isCValueExpr(const clang::Expr* e) {
    if (!e) return false;
    if (const auto* bop = llvm::dyn_cast<clang::BinaryOperator>(e)) {
        return bop->isAdditiveOp()       ||
               bop->isMultiplicativeOp() ||
               bop->isBitwiseOp()        ||
               bop->isShiftOp();
    }
    if (const auto* uop = llvm::dyn_cast<clang::UnaryOperator>(e)) {
        return uop->isArithmeticOp();
    }
    return llvm::isa<clang::ConditionalOperator>(e);
}

clang::QualType getUnderlyingType(const clang::Expr* expr, clang::ASTContext& ctx) {
    if (!expr) return clang::QualType();
    const clang::Expr* e = unwrapImplicitAndParens(expr);
    if (!e) return clang::QualType();

    if (llvm::isa<clang::ExplicitCastExpr>(e)) {
        return e->getType().getNonReferenceType().getUnqualifiedType();
    }

    if (const auto* uop = llvm::dyn_cast<clang::UnaryOperator>(e)) {
        if (uop->isArithmeticOp() || uop->isIncrementDecrementOp()) {
            return getUnderlyingType(uop->getSubExpr(), ctx);
        }
    }

    if (const auto* bop = llvm::dyn_cast<clang::BinaryOperator>(e)) {
        if (bop->isAssignmentOp() || bop->isShiftOp()) {
            return getUnderlyingType(bop->getLHS(), ctx);
        }
        if (bop->isAdditiveOp() || bop->isMultiplicativeOp() || bop->isBitwiseOp()) {
            clang::QualType lt = getUnderlyingType(bop->getLHS(), ctx);
            clang::QualType rt = getUnderlyingType(bop->getRHS(), ctx);
            if (lt.isNull() || rt.isNull()) {
                return e->getType().getNonReferenceType().getUnqualifiedType();
            }
            if (lt->isFloatingType() && !rt->isFloatingType()) return lt;
            if (rt->isFloatingType() && !lt->isFloatingType()) return rt;
            if (lt->isFloatingType() && rt->isFloatingType()) {
                return (ctx.getTypeSize(lt) >= ctx.getTypeSize(rt)) ? lt : rt;
            }
            if (isNonBoolIntegralType(lt) && isNonBoolIntegralType(rt)) {
                return (ctx.getTypeSize(lt) >= ctx.getTypeSize(rt)) ? lt : rt;
            }
        }
    }

    if (const auto* cond = llvm::dyn_cast<clang::ConditionalOperator>(e)) {
        clang::QualType tt = getUnderlyingType(cond->getTrueExpr(), ctx);
        clang::QualType ft = getUnderlyingType(cond->getFalseExpr(), ctx);
        if (!tt.isNull() && !ft.isNull()) {
            return (ctx.getTypeSize(tt) >= ctx.getTypeSize(ft)) ? tt : ft;
        }
    }

    return e->getType().getNonReferenceType().getUnqualifiedType();
}

} // namespace

Rule508::Rule508()
    : Rule("MISRA-5-0-8",
           "An explicit integral or floating-point conversion shall not increase the size of the underlying type of a cvalue expression.") {}

void Rule508::registerMatchers(MatchFinder& finder) {
    finder.addMatcher(explicitCastExpr().bind("cast"), this);
}

void Rule508::run(const MatchFinder::MatchResult& result) {
    const auto* cast = result.Nodes.getNodeAs<clang::ExplicitCastExpr>("cast");
    if (!cast) return;

    setContext(result.Context);
    clang::ASTContext& ctx = *result.Context;
    const auto& sm = ctx.getSourceManager();

    clang::SourceLocation spellingLoc = sm.getSpellingLoc(cast->getBeginLoc());
    if (spellingLoc.isInvalid() || sm.isInSystemHeader(spellingLoc)) return;

    clang::PresumedLoc presumed = sm.getPresumedLoc(spellingLoc);
    if (presumed.isInvalid()) return;
    if (store_ && !store_->getFilter().isProjectFile(presumed.getFilename())) {
        return;
    }

    clang::QualType dstType =
        cast->getType().getNonReferenceType().getUnqualifiedType();
    if (!isArithmeticNonBoolType(dstType)) return;

    const clang::Expr* sub = unwrapImplicitAndParens(cast->getSubExpr());
    if (!isCValueExpr(sub)) return;

    clang::QualType srcUnderlying = getUnderlyingType(sub, ctx);
    if (!isArithmeticNonBoolType(srcUnderlying)) return;

    if (ctx.getTypeSize(dstType) > ctx.getTypeSize(srcUnderlying)) {
        report(cast->getBeginLoc(),
               "Explicit conversion increases the size of the underlying type of a cvalue expression [MISRA-5-0-8]");
    }
}

} // namespace misra

