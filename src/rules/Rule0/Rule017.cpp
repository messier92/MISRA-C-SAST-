#include "Rule017.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/Stmt.h"

#include "llvm/ADT/StringRef.h"

using namespace clang;
using namespace clang::ast_matchers;

namespace misra {

namespace {

bool isExcludedStdFunc(const FunctionDecl* callee) {
    if (!callee || !callee->getIdentifier()) return false;
    llvm::StringRef name = callee->getName();
    return name == "memcpy"  ||
           name == "memset"  ||
           name == "memmove" ||
           name == "strcpy"  ||
           name == "strncpy" ||
           name == "strcat"  ||
           name == "strncat";
}

bool isTransparentWrapper(const Stmt* s) {
    return isa<ExprWithCleanups>(s)        ||
           isa<MaterializeTemporaryExpr>(s) ||
           isa<CXXBindTemporaryExpr>(s)     ||
           isa<ImplicitCastExpr>(s)         ||
           isa<ParenExpr>(s);
}

bool isDiscardedInStmt(const Stmt* parent, const Stmt* child) {
    if (!parent || !child) return false;
    if (isa<CompoundStmt>(parent)) return true;
    if (const auto* ifStmt = dyn_cast<IfStmt>(parent)) {
        return child == ifStmt->getThen() || child == ifStmt->getElse();
    }
    if (const auto* whileStmt = dyn_cast<WhileStmt>(parent)) {
        return child == whileStmt->getBody();
    }
    if (const auto* doStmt = dyn_cast<DoStmt>(parent)) {
        return child == doStmt->getBody();
    }
    if (const auto* forStmt = dyn_cast<ForStmt>(parent)) {
        return child == forStmt->getInit() ||
               child == forStmt->getInc()  ||
               child == forStmt->getBody();
    }
    if (const auto* rangeStmt = dyn_cast<CXXForRangeStmt>(parent)) {
        return child == rangeStmt->getBody();
    }
    if (const auto* switchStmt = dyn_cast<SwitchStmt>(parent)) {
        return child == switchStmt->getBody();
    }
    if (const auto* sc = dyn_cast<SwitchCase>(parent)) {
        return child == sc->getSubStmt();
    }
    if (const auto* ls = dyn_cast<LabelStmt>(parent)) {
        return child == ls->getSubStmt();
    }
    return false;
}

bool isInImplicitOrInstantiatedFunction(const Stmt* s, ASTContext& ctx) {
    DynTypedNodeList parents = ctx.getParents(*s);
    while (!parents.empty()) {
        const DynTypedNode& node = parents[0];
        if (const auto* fd = node.get<FunctionDecl>()) {
            return fd->isImplicit() || fd->isTemplateInstantiation();
        }
        parents = ctx.getParents(node);
    }
    return false;
}

} // namespace

Rule017::Rule017()
    : Rule("MISRA-0-1-7",
           "The value returned by a function having a non-void return type "
           "that is not an overloaded operator shall always be used.") {}

void Rule017::registerMatchers(MatchFinder& finder) {
    finder.addMatcher(
        callExpr(unless(cxxOperatorCallExpr()),
                 unless(userDefinedLiteral())).bind("call"),
        this);
}

void Rule017::run(const MatchFinder::MatchResult& result) {
    const auto* call = result.Nodes.getNodeAs<CallExpr>("call");
    if (!call) return;

    ASTContext& ctx = *result.Context;
    const SourceManager& sm = *result.SourceManager;

    SourceLocation loc = call->getBeginLoc();
    if (loc.isInvalid() || sm.isInSystemHeader(sm.getSpellingLoc(loc))) return;

    if (call->isTypeDependent() || call->isValueDependent()) return;

    QualType retType = call->getCallReturnType(ctx);
    if (retType.isNull() || retType->isVoidType() || retType->isDependentType()) {
        return;
    }

    if (const FunctionDecl* callee = call->getDirectCallee()) {
        if (isExcludedStdFunc(callee)) return;
    }

    if (isInImplicitOrInstantiatedFunction(call, ctx)) return;

    const Stmt* cur = call;
    while (cur) {
        DynTypedNodeList parents = ctx.getParents(*cur);
        if (parents.empty()) break;

        const Stmt* parentStmt = parents[0].get<Stmt>();
        if (!parentStmt) break;

        if (const auto* explicitCast = dyn_cast<ExplicitCastExpr>(parentStmt)) {
            if (explicitCast->getType()->isVoidType()) {
                return; // Explicitly cast to void: compliant.
            }
        }

        if (isTransparentWrapper(parentStmt)) {
            cur = parentStmt;
            continue;
        }

        if (const auto* bo = dyn_cast<BinaryOperator>(parentStmt)) {
            if (bo->getOpcode() == BO_Comma && cur == bo->getLHS()) {
                report(loc, "Return value of non-void function call is ignored.");
                return;
            }
        }

        if (isDiscardedInStmt(parentStmt, cur)) {
            report(loc, "Return value of non-void function call is ignored.");
            return;
        }

        break;
    }
}

} // namespace misra

