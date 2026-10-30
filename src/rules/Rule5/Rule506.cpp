#include "Rule506.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"

#include <functional>
#include <set>
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

bool isPtrdiffType(clang::QualType qt) {
    while (!qt.isNull()) {
        if (const auto* td = qt->getAs<clang::TypedefType>()) {
            llvm::StringRef name = td->getDecl()->getName();
            if (name == "ptrdiff_t") return true;
            qt = td->desugar();
        } else {
            break;
        }
    }
    return false;
}

bool isSignedIntegralOrPtrdiff(clang::QualType qt) {
    if (qt.isNull()) return false;
    qt = qt.getNonReferenceType().getUnqualifiedType();
    if (isPtrdiffType(qt)) return true;
    return qt.getCanonicalType()->isSignedIntegerType();
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
                uint64_t lSize = ctx.getTypeSize(lt);
                uint64_t rSize = ctx.getTypeSize(rt);
                if (lSize > rSize) return lt;
                if (rSize > lSize) return rt;
                if (!isSignedIntegralOrPtrdiff(lt)) return lt;
                if (!isSignedIntegralOrPtrdiff(rt)) return rt;
                return lt;
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

bool isProjectLoc(clang::SourceLocation loc,
                  clang::ASTContext& ctx,
                  const ViolationStore* store) {
    const auto& sm = ctx.getSourceManager();
    clang::SourceLocation spellingLoc = sm.getSpellingLoc(loc);
    if (spellingLoc.isInvalid() || sm.isInSystemHeader(spellingLoc)) return false;
    clang::PresumedLoc presumed = sm.getPresumedLoc(spellingLoc);
    if (presumed.isInvalid()) return false;
    if (store && !store->getFilter().isProjectFile(presumed.getFilename())) {
        return false;
    }
    return true;
}

class NarrowingConversionVisitor
    : public clang::RecursiveASTVisitor<NarrowingConversionVisitor> {
public:
    NarrowingConversionVisitor(
        clang::ASTContext& ctx,
        const ViolationStore* store,
        std::function<void(clang::SourceLocation, const std::string&)> reportFn)
        : ctx_(ctx), store_(store), reportFn_(std::move(reportFn)) {}

    bool checkImplicitConversion(const clang::Expr* srcExpr,
                                 clang::QualType dstType,
                                 clang::SourceLocation reportLoc) {
        if (!srcExpr || dstType.isNull()) return false;

        const clang::Expr* stripped = unwrapImplicitAndParens(srcExpr);
        if (!stripped ||
            llvm::isa<clang::ExplicitCastExpr>(stripped) ||
            llvm::isa<clang::IntegerLiteral>(stripped)   ||
            llvm::isa<clang::FloatingLiteral>(stripped)  ||
            llvm::isa<clang::CharacterLiteral>(stripped)) {
            return false;
        }

        clang::QualType srcType = getUnderlyingType(srcExpr, ctx_);
        dstType = dstType.getNonReferenceType().getUnqualifiedType();

        if (!isArithmeticNonBoolType(srcType) || !isArithmeticNonBoolType(dstType)) {
            return false;
        }

        // Precedence rule: if an implicit integral conversion also changes
        // signedness, Rule 5-0-4 takes precedence over Rule 5-0-6.
        if (isNonBoolIntegralType(srcType) && isNonBoolIntegralType(dstType) &&
            isSignedIntegralOrPtrdiff(srcType) != isSignedIntegralOrPtrdiff(dstType)) {
            return false;
        }

        uint64_t srcSize = ctx_.getTypeSize(srcType);
        uint64_t dstSize = ctx_.getTypeSize(dstType);

        if (dstSize < srcSize) {
            emitOncePerLine(
                reportLoc,
                "Implicit conversion reduces the size of the underlying type [MISRA-5-0-6]");
            return true;
        }
        return false;
    }

    bool VisitBinaryOperator(clang::BinaryOperator* bop) {
        if (!bop || !bop->isAssignmentOp()) return true;
        if (!isProjectLoc(bop->getBeginLoc(), ctx_, store_)) return true;

        clang::QualType dstType = getUnderlyingType(bop->getLHS(), ctx_);
        checkImplicitConversion(bop->getRHS(), dstType, bop->getBeginLoc());
        return true;
    }

    bool VisitVarDecl(clang::VarDecl* vd) {
        if (!vd || vd->isImplicit() || !vd->hasInit()) return true;
        if (!isProjectLoc(vd->getBeginLoc(), ctx_, store_)) return true;

        checkImplicitConversion(vd->getInit(), vd->getType(), vd->getBeginLoc());
        return true;
    }

    bool VisitCallExpr(clang::CallExpr* call) {
        if (!call || !isProjectLoc(call->getBeginLoc(), ctx_, store_)) return true;
        const clang::FunctionDecl* callee = call->getDirectCallee();
        if (!callee) return true;

        unsigned n = std::min(call->getNumArgs(), callee->getNumParams());
        for (unsigned i = 0; i < n; ++i) {
            const clang::Expr* arg = call->getArg(i);
            clang::QualType paramType = callee->getParamDecl(i)->getType();
            checkImplicitConversion(arg, paramType, arg->getBeginLoc());
        }
        return true;
    }

private:
    void emitOncePerLine(clang::SourceLocation loc, const std::string& msg) {
        const auto& sm = ctx_.getSourceManager();
        clang::PresumedLoc p = sm.getPresumedLoc(sm.getSpellingLoc(loc));
        if (p.isInvalid()) return;
        auto key = std::make_pair(std::string(p.getFilename()), p.getLine());
        if (reportedLines_.insert(key).second) {
            reportFn_(loc, msg);
        }
    }

    clang::ASTContext& ctx_;
    const ViolationStore* store_;
    std::function<void(clang::SourceLocation, const std::string&)> reportFn_;
    std::set<std::pair<std::string, unsigned>> reportedLines_;
};

} // namespace

Rule506::Rule506()
    : Rule("MISRA-5-0-6",
           "An implicit integral or floating-point conversion shall not reduce the size of the underlying type.") {}

void Rule506::registerMatchers(MatchFinder& finder) {
    finder.addMatcher(translationUnitDecl().bind("tu"), this);
}

void Rule506::run(const MatchFinder::MatchResult& result) {
    const auto* tu = result.Nodes.getNodeAs<clang::TranslationUnitDecl>("tu");
    if (!tu) return;

    setContext(result.Context);

    NarrowingConversionVisitor visitor(
        *result.Context,
        store_,
        [this](clang::SourceLocation loc, const std::string& msg) {
            report(loc, msg);
        });
    visitor.TraverseDecl(const_cast<clang::TranslationUnitDecl*>(tu));
}

} // namespace misra

