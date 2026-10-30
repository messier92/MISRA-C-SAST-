#include "Rule931.h"

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

/// Check whether an expression has `CXXThisExpr` at the root of its member/object
/// access chain.
bool hasThisBase(const Expr* expr) {
    if (!expr) return false;
    expr = expr->IgnoreParenCasts();

    if (llvm::isa<CXXThisExpr>(expr)) {
        return true;
    }
    if (const auto* me = llvm::dyn_cast<MemberExpr>(expr)) {
        if (llvm::isa<FieldDecl>(me->getMemberDecl()) ||
            llvm::isa<IndirectFieldDecl>(me->getMemberDecl())) {
            return hasThisBase(me->getBase());
        }
        return false;
    }
    if (const auto* ase = llvm::dyn_cast<ArraySubscriptExpr>(expr)) {
        return hasThisBase(ase->getBase());
    }
    if (const auto* uo = llvm::dyn_cast<UnaryOperator>(expr)) {
        if (uo->getOpcode() == UO_Deref || uo->getOpcode() == UO_AddrOf) {
            return hasThisBase(uo->getSubExpr());
        }
    }
    return false;
}

/// Determine whether a returned expression refers to a non-static data member
/// belonging to `this` (excluding static data members, local variables,
/// parameters, and external objects).
bool refersToNonStaticClassData(const Expr* expr) {
    if (!expr) return false;
    expr = expr->IgnoreParenCasts();

    if (const auto* ewc = llvm::dyn_cast<ExprWithCleanups>(expr)) {
        return refersToNonStaticClassData(ewc->getSubExpr());
    }
    if (const auto* bte = llvm::dyn_cast<CXXBindTemporaryExpr>(expr)) {
        return refersToNonStaticClassData(bte->getSubExpr());
    }
    if (const auto* mte = llvm::dyn_cast<MaterializeTemporaryExpr>(expr)) {
        return refersToNonStaticClassData(mte->getSubExpr());
    }

    if (const auto* me = llvm::dyn_cast<MemberExpr>(expr)) {
        // Static data members are represented as VarDecl in Clang's AST;
        // non-static data members are FieldDecl / IndirectFieldDecl.
        if (llvm::isa<FieldDecl>(me->getMemberDecl()) ||
            llvm::isa<IndirectFieldDecl>(me->getMemberDecl())) {
            return hasThisBase(me->getBase());
        }
        return false;
    }

    if (const auto* uo = llvm::dyn_cast<UnaryOperator>(expr)) {
        if (uo->getOpcode() == UO_AddrOf || uo->getOpcode() == UO_Deref) {
            return refersToNonStaticClassData(uo->getSubExpr());
        }
    }

    if (const auto* ase = llvm::dyn_cast<ArraySubscriptExpr>(expr)) {
        return refersToNonStaticClassData(ase->getBase());
    }

    if (const auto* bo = llvm::dyn_cast<BinaryOperator>(expr)) {
        if (bo->getOpcode() == BO_Add || bo->getOpcode() == BO_Sub) {
            return refersToNonStaticClassData(bo->getLHS()) ||
                   refersToNonStaticClassData(bo->getRHS());
        }
        if (bo->getOpcode() == BO_Comma) {
            return refersToNonStaticClassData(bo->getRHS());
        }
    }

    if (const auto* co = llvm::dyn_cast<ConditionalOperator>(expr)) {
        return refersToNonStaticClassData(co->getTrueExpr()) ||
               refersToNonStaticClassData(co->getFalseExpr());
    }

    return false;
}

/// Collect return statements directly belonging to a member function body,
/// stopping at nested lambdas or local classes.
class ReturnStmtCollector : public RecursiveASTVisitor<ReturnStmtCollector> {
public:
    std::vector<const ReturnStmt*> returns;

    bool TraverseLambdaExpr(LambdaExpr*) { return true; }
    bool TraverseCXXRecordDecl(CXXRecordDecl*) { return true; }

    bool VisitReturnStmt(const ReturnStmt* ret) {
        returns.push_back(ret);
        return true;
    }
};

} // namespace

Rule931::Rule931()
    : Rule("MISRA-9-3-1",
           "Const member functions shall not return non-const pointers or references to class-data.") {}

void Rule931::registerMatchers(MatchFinder& finder) {
    finder.addMatcher(cxxMethodDecl(isDefinition()).bind("method"), this);
}

void Rule931::run(const MatchFinder::MatchResult& result) {
    const auto* method = result.Nodes.getNodeAs<CXXMethodDecl>("method");
    if (!method || !result.Context || !result.SourceManager) return;

    if (method->isImplicit() || !method->hasBody()) return;
    if (method->isDependentContext() ||
        method->getDescribedFunctionTemplate() != nullptr) {
        return;
    }

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

    // Rule 9-3-1 applies only to const-qualified member functions.
    if (!method->isConst()) return;

    QualType retType = method->getReturnType();
    if (retType.isNull()) return;

    QualType canonRet = retType.getCanonicalType();
    bool isNonConstPtrOrRef = false;
    if (canonRet->isPointerType() || canonRet->isReferenceType()) {
        QualType pointee = canonRet->getPointeeType();
        if (!pointee.isConstQualified()) {
            isNonConstPtrOrRef = true;
        }
    }

    if (!isNonConstPtrOrRef) return;

    ReturnStmtCollector collector;
    collector.TraverseStmt(method->getBody());

    for (const ReturnStmt* ret : collector.returns) {
        const Expr* retVal = ret->getRetValue();
        if (refersToNonStaticClassData(retVal)) {
            report(
                loc,
                "Const member function '" + method->getNameAsString() +
                "' returns a non-const pointer or reference to class data [MISRA-9-3-1]");
            return;
        }
    }
}

} // namespace misra
