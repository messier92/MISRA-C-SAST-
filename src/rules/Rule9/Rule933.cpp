#include "Rule933.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"

using namespace clang;
using namespace clang::ast_matchers;

namespace misra {

namespace {

/// Determine whether `expr` designates a non-static data member (FieldDecl /
/// IndirectFieldDecl) directly stored in `*this` (excluding pointer dereferences
/// and reference members when checking whether `*this` itself is modified).
const FieldDecl* getModifiedThisField(const Expr* expr) {
    if (!expr) return nullptr;
    expr = expr->IgnoreParenImpCasts();

    if (const auto* me = llvm::dyn_cast<MemberExpr>(expr)) {
        const FieldDecl* fd = llvm::dyn_cast<FieldDecl>(me->getMemberDecl());
        if (!fd) {
            if (const auto* ifd = llvm::dyn_cast<IndirectFieldDecl>(me->getMemberDecl())) {
                fd = ifd->getAnonField();
            }
        }
        if (!fd) return nullptr;

        const Expr* base = me->getBase() ? me->getBase()->IgnoreParenImpCasts() : nullptr;
        if (!base) return nullptr;

        if (llvm::isa<CXXThisExpr>(base)) {
            return fd;
        }
        // Chained value subobject access `this->sub.field`: only modifies `*this`
        // if `this->sub` is a value member (neither pointer nor reference).
        if (!me->isArrow()) {
            const FieldDecl* rootField = getModifiedThisField(base);
            if (rootField &&
                !rootField->getType()->isPointerType() &&
                !rootField->getType()->isReferenceType()) {
                return rootField;
            }
        }
        return nullptr;
    }

    if (const auto* ase = llvm::dyn_cast<ArraySubscriptExpr>(expr)) {
        // `m_arr[i] = ...` modifies `*this` only when `m_arr` is an array member,
        // NOT when `m_ptr[i] = ...` writes through a pointer member.
        const Expr* base = ase->getBase() ? ase->getBase()->IgnoreParenImpCasts() : nullptr;
        const FieldDecl* fd = getModifiedThisField(base);
        if (fd && fd->getType()->isArrayType()) {
            return fd;
        }
        return nullptr;
    }

    return nullptr;
}

/// Check whether `expr` accesses any non-static data member of `this` (either
/// directly or through chained member access on `this`).
bool isThisFieldAccess(const MemberExpr* me) {
    if (!me) return false;
    const ValueDecl* memberDecl = me->getMemberDecl();
    if (!llvm::isa<FieldDecl>(memberDecl) &&
        !llvm::isa<IndirectFieldDecl>(memberDecl)) {
        return false;
    }

    const Expr* base = me->getBase();
    while (base) {
        base = base->IgnoreParenCasts();
        if (llvm::isa<CXXThisExpr>(base)) {
            return true;
        }
        if (const auto* baseMe = llvm::dyn_cast<MemberExpr>(base)) {
            base = baseMe->getBase();
            continue;
        }
        if (const auto* ase = llvm::dyn_cast<ArraySubscriptExpr>(base)) {
            base = ase->getBase();
            continue;
        }
        if (const auto* uo = llvm::dyn_cast<UnaryOperator>(base)) {
            if (uo->getOpcode() == UO_Deref || uo->getOpcode() == UO_AddrOf) {
                base = uo->getSubExpr();
                continue;
            }
        }
        break;
    }
    return false;
}

/// AST visitor that checks:
///   1. Whether a member function accesses any non-static data member of `this`
///      (or calls a non-static member function on `this`).
///   2. Whether the member function modifies any non-static data member of `this`.
class MethodAccessVisitor : public RecursiveASTVisitor<MethodAccessVisitor> {
public:
    bool accessesNonStaticData = false;
    bool modifiesNonStaticData = false;

    bool TraverseCXXRecordDecl(CXXRecordDecl*) { return true; }

    bool VisitMemberExpr(const MemberExpr* me) {
        if (isThisFieldAccess(me)) {
            accessesNonStaticData = true;
        }
        return true;
    }

    bool VisitBinaryOperator(const BinaryOperator* bo) {
        if (bo->isAssignmentOp()) {
            if (const FieldDecl* fd = getModifiedThisField(bo->getLHS())) {
                accessesNonStaticData = true;
                if (!fd->getType()->isReferenceType()) {
                    modifiesNonStaticData = true;
                }
            }
        }
        return true;
    }

    bool VisitUnaryOperator(const UnaryOperator* uo) {
        if (uo->isIncrementDecrementOp()) {
            if (const FieldDecl* fd = getModifiedThisField(uo->getSubExpr())) {
                accessesNonStaticData = true;
                if (!fd->getType()->isReferenceType()) {
                    modifiesNonStaticData = true;
                }
            }
        }
        return true;
    }

    bool VisitCXXOperatorCallExpr(const CXXOperatorCallExpr* oce) {
        OverloadedOperatorKind op = oce->getOperator();
        if ((oce->isAssignmentOp() || op == OO_PlusPlus || op == OO_MinusMinus) &&
            oce->getNumArgs() >= 1) {
            if (const FieldDecl* fd = getModifiedThisField(oce->getArg(0))) {
                accessesNonStaticData = true;
                if (!fd->getType()->isReferenceType()) {
                    modifiesNonStaticData = true;
                }
            }
        }
        return true;
    }

    bool VisitCXXMemberCallExpr(const CXXMemberCallExpr* mce) {
        const CXXMethodDecl* callee = mce->getMethodDecl();
        if (!callee || callee->isStatic()) return true;

        const Expr* objArg = mce->getImplicitObjectArgument();
        if (!objArg) return true;
        objArg = objArg->IgnoreParenImpCasts();

        if (llvm::isa<CXXThisExpr>(objArg)) {
            // Calling a non-static member function on `this` uses `this`, and if
            // that callee is non-const it prevents this function from being const.
            accessesNonStaticData = true;
            if (!callee->isConst()) {
                modifiesNonStaticData = true;
            }
        } else if (const FieldDecl* fd = getModifiedThisField(objArg)) {
            accessesNonStaticData = true;
            if (!callee->isConst() &&
                !fd->getType()->isPointerType() &&
                !fd->getType()->isReferenceType()) {
                modifiesNonStaticData = true;
            }
        }
        return true;
    }

    bool VisitCallExpr(const CallExpr* ce) {
        if (llvm::isa<CXXOperatorCallExpr>(ce) || llvm::isa<CXXMemberCallExpr>(ce)) {
            return true;
        }
        const FunctionDecl* callee = ce->getDirectCallee();
        unsigned numArgs = ce->getNumArgs();
        for (unsigned i = 0; i < numArgs; ++i) {
            const Expr* arg = ce->getArg(i);
            if (!callee || i >= callee->getNumParams()) continue;
            QualType paramType = callee->getParamDecl(i)->getType();
            if (paramType->isReferenceType() &&
                !paramType->getPointeeType().isConstQualified()) {
                if (const FieldDecl* fd = getModifiedThisField(arg)) {
                    if (!fd->getType()->isReferenceType()) {
                        modifiesNonStaticData = true;
                    }
                }
            } else if (paramType->isPointerType() &&
                       !paramType->getPointeeType().isConstQualified()) {
                const Expr* stripped = arg ? arg->IgnoreParenImpCasts() : nullptr;
                if (const auto* uo = llvm::dyn_cast_or_null<UnaryOperator>(stripped)) {
                    if (uo->getOpcode() == UO_AddrOf) {
                        if (const FieldDecl* fd = getModifiedThisField(uo->getSubExpr())) {
                            if (!fd->getType()->isReferenceType()) {
                                modifiesNonStaticData = true;
                            }
                        }
                    }
                }
            }
        }
        return true;
    }
};

} // namespace

Rule933::Rule933()
    : Rule("MISRA-9-3-3",
           "If a member function can be made static then it shall be made static, otherwise if it can be made const then it shall be made const.") {}

void Rule933::registerMatchers(MatchFinder& finder) {
    finder.addMatcher(cxxMethodDecl(isDefinition()).bind("method"), this);
}

void Rule933::run(const MatchFinder::MatchResult& result) {
    const auto* method = result.Nodes.getNodeAs<CXXMethodDecl>("method");
    if (!method || !result.Context || !result.SourceManager) return;

    if (method->isImplicit() || method->isDefaulted() || method->isDeleted()) return;
    if (!method->hasBody()) return;

    // Exclude special member functions, conversion operators, overloaded operators,
    // and virtual member functions.
    if (llvm::isa<CXXConstructorDecl>(method) ||
        llvm::isa<CXXDestructorDecl>(method) ||
        llvm::isa<CXXConversionDecl>(method) ||
        method->isOverloadedOperator() ||
        method->isVirtual()) {
        return;
    }

    if (method->isDependentContext() ||
        method->getDescribedFunctionTemplate() != nullptr ||
        method->getTemplateSpecializationKind() != TSK_Undeclared) {
        return;
    }

    // Functions already declared static are compliant with both checks.
    if (method->isStatic()) return;

    setContext(result.Context);
    const SourceManager& sm = *result.SourceManager;

    SourceLocation loc = method->getLocation();
    SourceLocation spellLoc = sm.getSpellingLoc(loc);
    if (spellLoc.isInvalid() || sm.isInSystemHeader(spellLoc)) return;

    PresumedLoc presumed = sm.getPresumedLoc(spellLoc);
    if (presumed.isInvalid()) return;
    if (store_ && !store_->getFilter().isProjectFile(presumed.getFilename())) {
        return;
    }

    MethodAccessVisitor visitor;
    visitor.TraverseStmt(method->getBody());

    // Check 1 (highest priority): If the member function does not access any
    // non-static data members of the class, it can be declared static (even if
    // it is already declared const).
    if (!visitor.accessesNonStaticData) {
        report(
            loc,
            "Member function '" + method->getNameAsString() +
            "' can be declared static [MISRA-9-3-3]");
        return;
    }

    // Check 2: If the member function accesses non-static data members of the
    // class without modifying any of them, and is not already declared const,
    // it can be declared const.
    if (!method->isConst() && !visitor.modifiesNonStaticData) {
        report(
            loc,
            "Member function '" + method->getNameAsString() +
            "' can be declared const [MISRA-9-3-3]");
    }
}

} // namespace misra
