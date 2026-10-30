#include "Rule621.h"

#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"

#include <functional>
#include <set>
#include <string>

using namespace clang;
using namespace clang::ast_matchers;

namespace misra {

namespace {

bool isAssignmentExpr(const Stmt* s) {
    if (!s) return false;
    if (const auto* bop = dyn_cast<BinaryOperator>(s)) {
        return bop->isAssignmentOp();
    }
    if (const auto* oce = dyn_cast<CXXOperatorCallExpr>(s)) {
        return oce->isAssignmentOp();
    }
    return false;
}

bool isTransparentWrapper(const Stmt* s) {
    return isa<ExprWithCleanups>(s)         ||
           isa<ImplicitCastExpr>(s)         ||
           isa<CXXBindTemporaryExpr>(s)     ||
           isa<MaterializeTemporaryExpr>(s);
}

/// Returns true if `assignStmt` is used as a standalone full-expression
/// (e.g. an expression statement in a CompoundStmt, or the init/inc of a ForStmt)
/// rather than embedded inside a larger expression or condition.
bool isStandaloneAssignment(const Stmt* assignStmt, const ParentBuilder& parents) {
    const Stmt* cur = assignStmt;
    const Stmt* parent = parents.getParent(cur);

    // Unwrap transparent AST nodes. Note that `ParenExpr` around an assignment
    // (such as `(x = y)`) is only standalone if the parent of the `ParenExpr`
    // is itself a standalone statement container; if the parent is another
    // expression or a condition, walking past `ParenExpr` will see that parent.
    while (parent && (isTransparentWrapper(parent) || isa<ParenExpr>(parent))) {
        cur = parent;
        parent = parents.getParent(cur);
    }

    // Also allow comma expressions where each side is a standalone expression
    // (e.g. `for (i = 0, j = 0; ...; i += 1, j += 1)`).
    while (const auto* comma = dyn_cast_or_null<BinaryOperator>(parent)) {
        if (comma->getOpcode() != BO_Comma) break;
        cur = parent;
        parent = parents.getParent(cur);
        while (parent && (isTransparentWrapper(parent) || isa<ParenExpr>(parent))) {
            cur = parent;
            parent = parents.getParent(cur);
        }
    }

    if (!parent) return true;

    if (isa<CompoundStmt>(parent)) {
        return true;
    }

    if (const auto* forStmt = dyn_cast<ForStmt>(parent)) {
        return cur == forStmt->getInit() || cur == forStmt->getInc();
    }

    if (const auto* ifStmt = dyn_cast<IfStmt>(parent)) {
        return cur == ifStmt->getThen() || cur == ifStmt->getElse();
    }

    if (const auto* whileStmt = dyn_cast<WhileStmt>(parent)) {
        return cur == whileStmt->getBody();
    }

    if (const auto* doStmt = dyn_cast<DoStmt>(parent)) {
        return cur == doStmt->getBody();
    }

    if (const auto* sc = dyn_cast<SwitchCase>(parent)) {
        return cur == sc->getSubStmt();
    }

    if (const auto* ls = dyn_cast<LabelStmt>(parent)) {
        return cur == ls->getSubStmt();
    }

    return false;
}

class SubExprAssignmentVisitor
    : public RecursiveASTVisitor<SubExprAssignmentVisitor> {
public:
    SubExprAssignmentVisitor(
        ASTContext& ctx,
        const ViolationStore* store,
        const ParentBuilder& parents,
        std::function<void(SourceLocation, const std::string&)> reportFn)
        : ctx_(ctx),
          store_(store),
          parents_(parents),
          reportFn_(std::move(reportFn)) {}

    bool VisitBinaryOperator(BinaryOperator* bop) {
        if (!bop || !bop->isAssignmentOp()) return true;
        checkAssignment(bop, bop->getOperatorLoc());
        return true;
    }

    bool VisitCXXOperatorCallExpr(CXXOperatorCallExpr* oce) {
        if (!oce || !oce->isAssignmentOp()) return true;
        checkAssignment(oce, oce->getOperatorLoc());
        return true;
    }

private:
    void checkAssignment(const Expr* expr, SourceLocation loc) {
        const SourceManager& sm = ctx_.getSourceManager();
        SourceLocation spellingLoc = sm.getSpellingLoc(loc);
        if (spellingLoc.isInvalid() || sm.isInSystemHeader(spellingLoc)) {
            return;
        }

        PresumedLoc presumed = sm.getPresumedLoc(spellingLoc);
        if (presumed.isInvalid()) return;
        if (store_ && !store_->getFilter().isProjectFile(presumed.getFilename())) {
            return;
        }

        if (!isStandaloneAssignment(expr, parents_)) {
            auto key = std::make_pair(std::string(presumed.getFilename()),
                                      presumed.getLine());
            if (reportedLines_.insert(key).second) {
                reportFn_(loc,
                          "Assignment operator used in a sub-expression [MISRA-6-2-1]");
            }
        }
    }

    ASTContext& ctx_;
    const ViolationStore* store_;
    const ParentBuilder& parents_;
    std::function<void(SourceLocation, const std::string&)> reportFn_;
    std::set<std::pair<std::string, unsigned>> reportedLines_;
};

} // namespace

Rule621::Rule621()
    : Rule("MISRA-6-2-1",
           "Assignment operators shall not be used in sub-expressions.") {}

void Rule621::registerMatchers(MatchFinder& finder) {
    finder.addMatcher(
        functionDecl(isDefinition(), unless(isImplicit())).bind("func"),
        this);
}

void Rule621::run(const MatchFinder::MatchResult& result) {
    const auto* func = result.Nodes.getNodeAs<FunctionDecl>("func");
    if (!func || !func->hasBody() || func->isInvalidDecl()) return;
    if (func->getDescribedFunctionTemplate() || func->isTemplateInstantiation() ||
        func->isDependentContext()) {
        return;
    }

    setContext(result.Context);
    ASTContext& ctx = *result.Context;
    const SourceManager& sm = ctx.getSourceManager();

    if (func->getBeginLoc().isInvalid() ||
        sm.isInSystemHeader(sm.getSpellingLoc(func->getBeginLoc()))) {
        return;
    }

    ParentBuilder parents;
    parents.build(ctx);

    SubExprAssignmentVisitor visitor(
        ctx,
        store_,
        parents,
        [this](SourceLocation loc, const std::string& msg) {
            report(loc, msg);
        });
    visitor.TraverseStmt(func->getBody());
}

} // namespace misra

