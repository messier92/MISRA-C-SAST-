#include "Rule018.h"

#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtCXX.h"

#include "llvm/ADT/StringRef.h"

#include <unordered_set>

using namespace clang;
using namespace clang::ast_matchers;

namespace misra {

namespace {

bool isKnownPureFunction(const FunctionDecl* fd) {
    if (!fd) return false;
    if (fd->hasAttr<ConstAttr>() || fd->hasAttr<PureAttr>()) return true;
    if (!fd->getIdentifier()) return false;

    llvm::StringRef name = fd->getName();
    static const std::unordered_set<std::string> kPureNames = {
        "sin", "sinf", "sinl",
        "cos", "cosf", "cosl",
        "tan", "tanf", "tanl",
        "asin", "asinf", "asinl",
        "acos", "acosf", "acosl",
        "atan", "atanf", "atanl",
        "atan2", "atan2f", "atan2l",
        "sinh", "sinhf", "sinhl",
        "cosh", "coshf", "coshl",
        "tanh", "tanhf", "tanhl",
        "asinh", "acosh", "atanh",
        "exp", "expf", "expl", "exp2", "expm1",
        "log", "logf", "logl", "log10", "log2", "log1p",
        "pow", "powf", "powl",
        "sqrt", "sqrtf", "sqrtl",
        "cbrt", "hypot",
        "ceil", "ceilf", "ceill",
        "floor", "floorf", "floorl",
        "trunc", "round", "nearbyint", "rint",
        "fabs", "fabsf", "fabsl", "abs", "labs", "llabs",
        "fmod", "remainder", "fmax", "fmin", "fdim", "fma",
        "isnan", "isinf", "isfinite", "isnormal", "signbit",
        "strlen", "strcmp", "strncmp", "memcmp", "strchr", "strrchr", "strstr"
    };
    return kPureNames.count(name.str()) > 0;
}

bool isEmptyBody(const Stmt* body) {
    const auto* compound = dyn_cast_or_null<CompoundStmt>(body);
    if (!compound) return false;
    for (const Stmt* child : compound->body()) {
        if (!isa<NullStmt>(child)) return false;
    }
    return true;
}

bool hasExecutableStmt(const Stmt* body) {
    const auto* compound = dyn_cast_or_null<CompoundStmt>(body);
    if (!compound) return false;
    for (const Stmt* child : compound->body()) {
        if (!isa<NullStmt>(child) && !isa<DeclStmt>(child)) {
            return true;
        }
    }
    return false;
}

bool isExcludedFunction(const FunctionDecl* fd) {
    if (!fd) return true;
    if (fd->isMain()) return true;
    if (isa<CXXConstructorDecl>(fd) || isa<CXXDestructorDecl>(fd)) return true;

    OverloadedOperatorKind oo = fd->getOverloadedOperator();
    if (oo == OO_New || oo == OO_Array_New ||
        oo == OO_Delete || oo == OO_Array_Delete) {
        return true;
    }

    if (const auto* md = dyn_cast<CXXMethodDecl>(fd)) {
        if (md->getParent() && md->getParent()->isLambda()) {
            return true;
        }
        if (md->isVirtual() && isEmptyBody(md->getBody())) {
            return true;
        }
    }

    if (!hasExecutableStmt(fd->getBody())) {
        return true;
    }
    return false;
}

bool functionHasExternalSideEffects(
    const FunctionDecl* func,
    std::unordered_set<const FunctionDecl*>& visited);

class SideEffectFinder : public RecursiveASTVisitor<SideEffectFinder> {
public:
    SideEffectFinder(const FunctionDecl* func,
                     std::unordered_set<const FunctionDecl*>& visited)
        : func_(func->getCanonicalDecl()), visited_(visited) {}

    bool hasSideEffects() const { return foundSideEffect_; }

    bool VisitAsmStmt(AsmStmt*) {
        foundSideEffect_ = true;
        return false;
    }

    bool VisitCXXThrowExpr(CXXThrowExpr*) {
        foundSideEffect_ = true;
        return false;
    }

    bool VisitCXXNewExpr(CXXNewExpr*) {
        foundSideEffect_ = true;
        return false;
    }

    bool VisitCXXDeleteExpr(CXXDeleteExpr*) {
        foundSideEffect_ = true;
        return false;
    }

    bool VisitCXXThisExpr(CXXThisExpr*) {
        foundSideEffect_ = true;
        return false;
    }

    bool VisitDeclRefExpr(DeclRefExpr* dre) {
        if (!dre) return true;
        if (dre->getType().isVolatileQualified()) {
            foundSideEffect_ = true;
            return false;
        }
        if (const auto* vd = dyn_cast<VarDecl>(dre->getDecl())) {
            if (vd->getType().isVolatileQualified()) {
                foundSideEffect_ = true;
                return false;
            }
        }
        return true;
    }

    bool VisitMemberExpr(MemberExpr* me) {
        if (!me) return true;
        if (me->getType().isVolatileQualified()) {
            foundSideEffect_ = true;
            return false;
        }
        // Accessing or setting a class/struct member on a non-local object,
        // reference/pointer parameter, or `this` has an external side effect.
        const Expr* base = me->getBase();
        if (base) {
            base = base->IgnoreParenImpCasts();
            if (isa<CXXThisExpr>(base) || me->isArrow()) {
                foundSideEffect_ = true;
                return false;
            }
            if (const auto* dre = dyn_cast<DeclRefExpr>(base)) {
                if (const auto* pvd = dyn_cast<ParmVarDecl>(dre->getDecl())) {
                    if (pvd->getType()->isReferenceType() ||
                        pvd->getType()->isPointerType()) {
                        foundSideEffect_ = true;
                        return false;
                    }
                } else if (const auto* vd = dyn_cast<VarDecl>(dre->getDecl())) {
                    if (!vd->hasLocalStorage()) {
                        foundSideEffect_ = true;
                        return false;
                    }
                }
            }
        }
        return true;
    }

    bool VisitUnaryOperator(UnaryOperator* uo) {
        if (!uo) return true;
        if (uo->getOpcode() == UO_Deref) {
            // Dereferencing a pointer argument, nonlocal pointer, or absolute address.
            const Expr* sub = uo->getSubExpr()->IgnoreParenImpCasts();
            if (const auto* dre = dyn_cast<DeclRefExpr>(sub)) {
                if (const auto* pvd = dyn_cast<ParmVarDecl>(dre->getDecl())) {
                    if (pvd->getType()->isPointerType()) {
                        foundSideEffect_ = true;
                        return false;
                    }
                } else if (const auto* vd = dyn_cast<VarDecl>(dre->getDecl())) {
                    if (!vd->hasLocalStorage()) {
                        foundSideEffect_ = true;
                        return false;
                    }
                    // Local pointer initialized/assigned an absolute address or external ptr.
                    if (vd->hasInit()) {
                        foundSideEffect_ = true;
                        return false;
                    }
                }
            } else {
                foundSideEffect_ = true;
                return false;
            }
        } else if (uo->isIncrementDecrementOp()) {
            if (isNonLocalOrRefParam(uo->getSubExpr())) {
                foundSideEffect_ = true;
                return false;
            }
        }
        return true;
    }

    bool VisitArraySubscriptExpr(ArraySubscriptExpr* ase) {
        if (!ase) return true;
        const Expr* base = ase->getBase()->IgnoreParenImpCasts();
        if (const auto* dre = dyn_cast<DeclRefExpr>(base)) {
            if (const auto* pvd = dyn_cast<ParmVarDecl>(dre->getDecl())) {
                if (pvd->getType()->isPointerType() ||
                    pvd->getType()->isReferenceType()) {
                    foundSideEffect_ = true;
                    return false;
                }
            } else if (const auto* vd = dyn_cast<VarDecl>(dre->getDecl())) {
                if (!vd->hasLocalStorage()) {
                    foundSideEffect_ = true;
                    return false;
                }
            }
        }
        return true;
    }

    bool VisitBinaryOperator(BinaryOperator* bo) {
        if (!bo) return true;
        if (bo->isAssignmentOp()) {
            if (isNonLocalOrRefParam(bo->getLHS())) {
                foundSideEffect_ = true;
                return false;
            }
        }
        return true;
    }

    bool VisitCallExpr(CallExpr* call) {
        if (!call) return true;
        const FunctionDecl* callee = call->getDirectCallee();
        if (!callee) {
            // Indirect call via function pointer: conservatively impure.
            foundSideEffect_ = true;
            return false;
        }

        const FunctionDecl* canonCallee = callee->getCanonicalDecl();
        // Calling itself does not count as calling an impure function.
        if (canonCallee == func_ || visited_.count(canonCallee) > 0) {
            return true;
        }

        if (isKnownPureFunction(callee)) {
            return true;
        }

        if (callee->hasBody()) {
            if (functionHasExternalSideEffects(callee, visited_)) {
                foundSideEffect_ = true;
                return false;
            }
            return true;
        }

        // Unknown external function with no body and not in known-pure list:
        // conservatively treat as impure.
        foundSideEffect_ = true;
        return false;
    }

private:
    bool isNonLocalOrRefParam(const Expr* e) const {
        if (!e) return false;
        e = e->IgnoreParenImpCasts();
        if (const auto* dre = dyn_cast<DeclRefExpr>(e)) {
            if (const auto* pvd = dyn_cast<ParmVarDecl>(dre->getDecl())) {
                return pvd->getType()->isReferenceType() ||
                       pvd->getType()->isPointerType();
            }
            if (const auto* vd = dyn_cast<VarDecl>(dre->getDecl())) {
                return !vd->hasLocalStorage();
            }
        }
        if (isa<UnaryOperator>(e) || isa<ArraySubscriptExpr>(e) || isa<MemberExpr>(e)) {
            return true;
        }
        return false;
    }

    const FunctionDecl* func_;
    std::unordered_set<const FunctionDecl*>& visited_;
    bool foundSideEffect_ = false;
};

bool functionHasExternalSideEffects(
    const FunctionDecl* func,
    std::unordered_set<const FunctionDecl*>& visited) {
    if (!func || !func->hasBody()) return true;
    const FunctionDecl* canon = func->getCanonicalDecl();
    visited.insert(canon);

    SideEffectFinder finder(func, visited);
    finder.TraverseStmt(func->getBody());
    return finder.hasSideEffects();
}

} // namespace

Rule018::Rule018()
    : Rule("MISRA-0-1-8",
           "All functions with void return type shall have external side effect(s).") {}

void Rule018::registerMatchers(MatchFinder& finder) {
    finder.addMatcher(
        functionDecl(isDefinition(), unless(isImplicit())).bind("func"),
        this);
}

void Rule018::run(const MatchFinder::MatchResult& result) {
    const auto* func = result.Nodes.getNodeAs<FunctionDecl>("func");
    if (!func || !func->hasBody()) return;
    if (func->isTemplateInstantiation()) return;

    const SourceManager& sm = *result.SourceManager;
    SourceLocation loc = func->getBeginLoc();
    if (loc.isInvalid() || sm.isInSystemHeader(loc)) return;

    if (!func->getReturnType()->isVoidType()) return;
    if (func->getNumParams() == 0 && !isa<CXXMethodDecl>(func)) return;
    if (isExcludedFunction(func)) return;

    std::unordered_set<const FunctionDecl*> visited;
    if (!functionHasExternalSideEffects(func, visited)) {
        report(loc,
               "Function '" + func->getNameAsString() +
               "' has void return type but no external side effects.");
    }
}

} // namespace misra

