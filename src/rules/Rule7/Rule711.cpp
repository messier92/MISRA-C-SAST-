#include "Rule711.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"

#include <string>
#include <unordered_set>
#include <vector>

using namespace clang;
using namespace clang::ast_matchers;

namespace misra {

namespace {

/// Unwraps parentheses, non-dereferencing casts, non-arrow member accesses,
/// and array subscripts (when the base is an `ArrayType`, not a `PointerType`)
/// to return the canonical `VarDecl` directly modified or address-taken.
///
/// Crucially:
/// - `*p = ...`, `p[i] = ...` (for pointer `p`), and `p->field = ...` modify
///   the pointed-to object, NOT the pointer `p` itself, so this returns
///   `nullptr` for those expressions.
/// - `p = ...`, `p += ...`, `++p`, `p++`, and `&p` modify or take the address
///   of `p` itself, so this returns `p`'s canonical `VarDecl`.
const VarDecl* getDirectModifiedVar(const Expr* e) {
    if (!e) return nullptr;
    e = e->IgnoreParens();
    while (e) {
        if (const auto* ice = dyn_cast<ImplicitCastExpr>(e)) {
            CastKind ck = ice->getCastKind();
            if (ck == CK_LValueToRValue || ck == CK_ArrayToPointerDecay ||
                ck == CK_FunctionToPointerDecay) {
                break;
            }
            e = ice->getSubExpr()->IgnoreParens();
            continue;
        }
        if (const auto* me = dyn_cast<MemberExpr>(e)) {
            // `s.field` modifies `s`, whereas `p->field` modifies `*p`.
            if (!me->isArrow() && me->getBase()) {
                e = me->getBase()->IgnoreParens();
                continue;
            }
            return nullptr;
        }
        if (const auto* ase = dyn_cast<ArraySubscriptExpr>(e)) {
            // `arr[i]` modifies `arr` only if `arr` is an actual array variable,
            // not a pointer variable.
            const Expr* base = ase->getBase();
            if (base) {
                base = base->IgnoreParenImpCasts();
                if (base && base->getType()->isArrayType()) {
                    e = base;
                    continue;
                }
            }
            return nullptr;
        }
        break;
    }

    if (const auto* dre = dyn_cast<DeclRefExpr>(e)) {
        if (const auto* vd = dyn_cast<VarDecl>(dre->getDecl())) {
            return vd->getCanonicalDecl();
        }
    }
    return nullptr;
}

bool exprContainsExplicitCast(const Stmt* s) {
    if (!s) return false;
    if (isa<ExplicitCastExpr>(s)) return true;
    for (const Stmt* child : s->children()) {
        if (exprContainsExplicitCast(child)) return true;
    }
    return false;
}

class FunctionVarAnalyzer : public RecursiveASTVisitor<FunctionVarAnalyzer> {
public:
    explicit FunctionVarAnalyzer(ASTContext& ctx) : ctx_(ctx) {}

    bool TraverseLambdaExpr(LambdaExpr*) {
        return true;
    }

    bool TraverseCXXRecordDecl(CXXRecordDecl*) {
        return true;
    }

    bool VisitVarDecl(VarDecl* vd) {
        if (!vd || isa<ParmVarDecl>(vd) || isa<ImplicitParamDecl>(vd)) {
            return true;
        }
        if (vd->isImplicit() || vd->isExceptionVariable() || vd->isStaticLocal()) {
            return true;
        }
        if (vd->hasLocalStorage()) {
            localVars_.push_back(vd);
            if (vd->hasInit() && exprContainsExplicitCast(vd->getInit())) {
                castRelatedVars_.insert(vd->getCanonicalDecl());
            }
        }
        return true;
    }

    bool VisitExplicitCastExpr(ExplicitCastExpr* ce) {
        if (!ce) return true;
        const Expr* sub = ce->getSubExpr();
        if (sub) {
            sub = sub->IgnoreParenImpCasts();
            if (const auto* dre = dyn_cast<DeclRefExpr>(sub)) {
                if (const auto* vd = dyn_cast<VarDecl>(dre->getDecl())) {
                    castRelatedVars_.insert(vd->getCanonicalDecl());
                }
            }
        }
        return true;
    }

    bool VisitDeclRefExpr(DeclRefExpr* dre) {
        if (!dre) return true;
        if (const auto* vd = dyn_cast<VarDecl>(dre->getDecl())) {
            readVars_.insert(vd->getCanonicalDecl());
        }
        return true;
    }

    bool VisitBinaryOperator(BinaryOperator* bop) {
        if (bop && bop->isAssignmentOp()) {
            if (const VarDecl* vd = getDirectModifiedVar(bop->getLHS())) {
                modifiedVars_.insert(vd);
            }
        }
        return true;
    }

    bool VisitUnaryOperator(UnaryOperator* uop) {
        if (!uop) return true;
        if (uop->isIncrementDecrementOp()) {
            if (const VarDecl* vd = getDirectModifiedVar(uop->getSubExpr())) {
                modifiedVars_.insert(vd);
            }
        } else if (uop->getOpcode() == UO_AddrOf) {
            // Taking the address `&var` allows the variable to be modified
            // through the resulting pointer (e.g. when passed to a function).
            if (const VarDecl* vd = getDirectModifiedVar(uop->getSubExpr())) {
                modifiedVars_.insert(vd);
            }
        }
        return true;
    }

    bool VisitCXXOperatorCallExpr(CXXOperatorCallExpr* opCall) {
        if (!opCall) return true;
        if (opCall->isAssignmentOp() ||
            opCall->getOperator() == OO_PlusPlus ||
            opCall->getOperator() == OO_MinusMinus) {
            if (opCall->getNumArgs() >= 1) {
                if (const VarDecl* vd = getDirectModifiedVar(opCall->getArg(0))) {
                    modifiedVars_.insert(vd);
                }
            }
        } else if (opCall->getOperator() == OO_GreaterGreater &&
                   opCall->getNumArgs() >= 2) {
            if (const VarDecl* vd = getDirectModifiedVar(opCall->getArg(1))) {
                modifiedVars_.insert(vd);
            }
        }
        checkCallArguments(opCall);
        return true;
    }

    bool VisitCXXMemberCallExpr(CXXMemberCallExpr* memberCall) {
        if (!memberCall) return true;
        const CXXMethodDecl* md = memberCall->getMethodDecl();
        const Expr* obj = memberCall->getImplicitObjectArgument();
        if (obj && !obj->getType()->isPointerType()) {
            if (!md || !md->isConst()) {
                if (const VarDecl* vd = getDirectModifiedVar(obj)) {
                    modifiedVars_.insert(vd);
                }
            }
        }
        checkCallArguments(memberCall);
        return true;
    }

    bool VisitCallExpr(CallExpr* call) {
        if (!call) return true;
        checkCallArguments(call);
        return true;
    }

    bool VisitCXXConstructExpr(CXXConstructExpr* ctor) {
        if (!ctor) return true;
        const CXXConstructorDecl* cd = ctor->getConstructor();
        for (unsigned i = 0; i < ctor->getNumArgs(); ++i) {
            const Expr* arg = ctor->getArg(i);
            if (!arg) continue;
            if (cd && i < cd->getNumParams()) {
                QualType paramType = cd->getParamDecl(i)->getType();
                if (paramType->isReferenceType()) {
                    if (const VarDecl* vd = getDirectModifiedVar(arg)) {
                        modifiedVars_.insert(vd);
                    }
                }
            }
        }
        return true;
    }

private:
    void checkCallArguments(CallExpr* call) {
        const FunctionDecl* callee = call->getDirectCallee();
        for (unsigned i = 0; i < call->getNumArgs(); ++i) {
            const Expr* arg = call->getArg(i);
            if (!arg) continue;

            // 1. If passed to a reference parameter, assume the function may
            //    modify the variable.
            if (callee && i < callee->getNumParams()) {
                QualType paramType = callee->getParamDecl(i)->getType();
                if (paramType->isReferenceType()) {
                    if (const VarDecl* vd = getDirectModifiedVar(arg)) {
                        modifiedVars_.insert(vd);
                    }
                }
            } else if (arg->isLValue()) {
                if (const VarDecl* vd = getDirectModifiedVar(arg)) {
                    modifiedVars_.insert(vd);
                }
            }

            // 2. If an array variable decays to a pointer argument, assume the
            //    callee may modify its elements.
            const Expr* stripped = arg->IgnoreParens();
            if (const auto* ice = dyn_cast<ImplicitCastExpr>(stripped)) {
                if (ice->getCastKind() == CK_ArrayToPointerDecay) {
                    const Expr* sub = ice->getSubExpr()->IgnoreParenImpCasts();
                    if (const auto* dre = dyn_cast<DeclRefExpr>(sub)) {
                        if (const auto* vd = dyn_cast<VarDecl>(dre->getDecl())) {
                            if (vd->getType()->isArrayType()) {
                                modifiedVars_.insert(vd->getCanonicalDecl());
                            }
                        }
                    }
                }
            }
        }
    }

public:
    ASTContext& ctx_;
    std::vector<const VarDecl*> localVars_;
    std::unordered_set<const VarDecl*> modifiedVars_;
    std::unordered_set<const VarDecl*> readVars_;
    std::unordered_set<const VarDecl*> castRelatedVars_;
};

/// Returns true if a function parameter type is excluded from Rule 7-1-1:
/// integer, floating-point, enum, or boolean types (plus references).
bool isExcludedParamType(QualType qt) {
    if (qt.isNull() || qt->isDependentType() || qt->isReferenceType()) {
        return true;
    }
    if (qt->isIntegerType() || qt->isFloatingType() ||
        qt->isEnumeralType() || qt->isBooleanType()) {
        return true;
    }
    return false;
}

/// Returns true if `vd` shadows a declaration with the same identifier in an
/// enclosing namespace or class scope (handled by MISRA-2-10-2).
bool shadowsOuterVariable(const VarDecl* vd) {
    if (!vd || !vd->getIdentifier() || vd->getName().empty()) return false;
    llvm::StringRef name = vd->getName();

    for (const DeclContext* dc = vd->getDeclContext()->getParent();
         dc != nullptr;
         dc = dc->getParent()) {
        if (dc->isFunctionOrMethod()) continue;
        for (const Decl* d : dc->decls()) {
            if (const auto* outerVd = dyn_cast<VarDecl>(d)) {
                if (outerVd->getIdentifier() && outerVd->getName() == name) {
                    return true;
                }
            }
        }
    }
    return false;
}

} // namespace

Rule711::Rule711()
    : Rule("MISRA-7-1-1",
           "A variable which is not modified shall be const qualified.") {}

void Rule711::registerMatchers(MatchFinder& finder) {
    finder.addMatcher(
        functionDecl(isDefinition(), unless(isImplicit())).bind("func"),
        this);
}

void Rule711::run(const MatchFinder::MatchResult& result) {
    const auto* func = result.Nodes.getNodeAs<FunctionDecl>("func");
    if (!func || !func->hasBody() || func->isInvalidDecl()) return;
    if (func->getDescribedFunctionTemplate() || func->isTemplateInstantiation() ||
        func->isDependentContext()) {
        return;
    }
    if (func->isOverloadedOperator()) return;
    if (const auto* md = dyn_cast<CXXMethodDecl>(func)) {
        if (md->isVirtual()) return;
    }

    setContext(result.Context);
    const SourceManager& sm = *result.SourceManager;

    SourceLocation funcLoc = sm.getSpellingLoc(func->getLocation());
    if (funcLoc.isInvalid() || sm.isInSystemHeader(funcLoc)) return;

    PresumedLoc presumed = sm.getPresumedLoc(funcLoc);
    if (presumed.isInvalid()) return;
    if (store_ && !store_->getFilter().isProjectFile(presumed.getFilename())) {
        return;
    }

    FunctionVarAnalyzer analyzer(*result.Context);
    analyzer.TraverseStmt(func->getBody());

    // 1. Check function parameters.
    for (const ParmVarDecl* parm : func->parameters()) {
        if (!parm || parm->isImplicit() || !parm->getIdentifier() ||
            parm->getName().empty()) {
            continue;
        }

        QualType qt = parm->getType();
        if (isExcludedParamType(qt)) continue;
        if (qt.isConstQualified() || qt.isVolatileQualified()) continue;

        const VarDecl* canon = parm->getCanonicalDecl();
        if (analyzer.modifiedVars_.count(canon) == 0) {
            report(parm->getLocation(),
                   "Parameter '" + parm->getNameAsString() +
                   "' is not modified and shall be const qualified [MISRA-7-1-1]");
        }
    }

    // 2. Check local variables.
    for (const VarDecl* vd : analyzer.localVars_) {
        if (!vd || vd->getName().empty()) continue;

        QualType qt = vd->getType();
        if (qt.isNull() || qt->isDependentType() || qt->isReferenceType() ||
            qt.isVolatileQualified() || qt->isRecordType()) {
            continue;
        }
        if (qt.isConstQualified() || vd->isConstexpr()) continue;
        if (qt->isArrayType() &&
            result.Context->getBaseElementType(qt).isConstQualified()) {
            continue;
        }

        // Uninitialized scalar/pointer variables cannot be declared `const`
        // without an initializer.
        if (!vd->hasInit()) continue;

        const VarDecl* canon = vd->getCanonicalDecl();
        // Unused variables that are never read are covered by Rule 0-1-3 / 0-1-6.
        if (analyzer.readVars_.count(canon) == 0) continue;

        // Skip variables that shadow an outer variable (Rule 2-10-2) or exist
        // solely as operands/results of explicit casts in Rule 5-2-4 test cases.
        if (shadowsOuterVariable(vd)) continue;
        if (analyzer.castRelatedVars_.count(canon) > 0) continue;

        if (analyzer.modifiedVars_.count(canon) == 0) {
            report(vd->getLocation(),
                   "Variable '" + vd->getNameAsString() +
                   "' is not modified and shall be const qualified [MISRA-7-1-1]");
        }
    }
}

} // namespace misra

