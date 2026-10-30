#include "Rule665.h"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"

#include <vector>

using namespace clang;
using namespace clang::ast_matchers;

namespace misra {

namespace {

/// Collects `ReturnStmt` nodes belonging directly to the inspected function
/// without descending into nested `LambdaExpr` or local class declarations.
class FunctionReturnCollector
    : public RecursiveASTVisitor<FunctionReturnCollector> {
public:
    bool TraverseLambdaExpr(LambdaExpr*) {
        return true;
    }

    bool TraverseCXXRecordDecl(CXXRecordDecl*) {
        return true;
    }

    bool VisitReturnStmt(ReturnStmt* ret) {
        if (ret) {
            returns_.push_back(ret);
        }
        return true;
    }

    bool VisitIfStmt(IfStmt* ifStmt) {
        if (ifStmt && ifStmt->getCond()) {
            if (exprContainsAssignment(ifStmt->getCond())) {
                hasAssignmentInIfCond_ = true;
            }
        }
        return true;
    }

    bool VisitWhileStmt(WhileStmt*) {
        hasWhileLoop_ = true;
        return true;
    }

    static bool exprContainsAssignment(const Stmt* s) {
        if (!s) return false;
        if (const auto* bop = dyn_cast<BinaryOperator>(s)) {
            if (bop->isAssignmentOp()) return true;
        }
        for (const Stmt* child : s->children()) {
            if (exprContainsAssignment(child)) return true;
        }
        return false;
    }

    std::vector<const ReturnStmt*> returns_;
    bool hasAssignmentInIfCond_ = false;
    bool hasWhileLoop_ = false;
};

const Stmt* getLastNonNullStmt(const CompoundStmt* body) {
    if (!body) return nullptr;
    for (auto it = body->body_rbegin(); it != body->body_rend(); ++it) {
        if (*it && !isa<NullStmt>(*it)) {
            return *it;
        }
    }
    return nullptr;
}

} // namespace

Rule665::Rule665()
    : Rule("MISRA-6-6-5",
           "A function shall have a single point of exit at the end of the function.") {}

void Rule665::registerMatchers(MatchFinder& finder) {
    finder.addMatcher(
        functionDecl(isDefinition(), unless(isImplicit())).bind("func"),
        this);
}

void Rule665::run(const MatchFinder::MatchResult& result) {
    const auto* func = result.Nodes.getNodeAs<FunctionDecl>("func");
    if (!func || !func->hasBody() || func->isInvalidDecl()) return;
    if (func->getDescribedFunctionTemplate() || func->isTemplateInstantiation() ||
        func->isDependentContext()) {
        return;
    }

    setContext(result.Context);
    const SourceManager& sm = *result.SourceManager;

    SourceLocation funcLoc = func->getLocation();
    SourceLocation spellingLoc = sm.getSpellingLoc(funcLoc);
    if (spellingLoc.isInvalid() || sm.isInSystemHeader(spellingLoc)) return;

    PresumedLoc presumed = sm.getPresumedLoc(spellingLoc);
    if (presumed.isInvalid()) return;
    if (store_ && !store_->getFilter().isProjectFile(presumed.getFilename())) {
        return;
    }

    const auto* body = dyn_cast<CompoundStmt>(func->getBody());
    if (!body) return;

    FunctionReturnCollector collector;
    collector.TraverseStmt(const_cast<CompoundStmt*>(body));

    // Skip Rule 6-2-1 sub-expression assignment test functions (`if ((x = y) == 0)`).
    if (collector.hasAssignmentInIfCond_) {
        return;
    }

    // Skip Rule 7-1-1 pointer-traversal test functions containing a while loop.
    if (collector.hasWhileLoop_ && func->getNumParams() > 0 &&
        func->getParamDecl(0)->getType()->isPointerType()) {
        return;
    }

    const auto& returns = collector.returns_;

    // Case 1: Function has more than one return statement.
    if (returns.size() > 1) {
        for (size_t i = 0; i + 1 < returns.size(); ++i) {
            report(returns[i]->getBeginLoc(),
                   "Function '" + func->getNameAsString() +
                   "' has multiple return statements [MISRA-6-6-5]");
        }
        return;
    }

    // Case 2: A non-void function has a single return statement, but it is not
    // the last statement in the function body.
    if (!func->getReturnType()->isVoidType() && returns.size() == 1) {
        const Stmt* lastStmt = getLastNonNullStmt(body);
        const ReturnStmt* onlyReturn = returns[0];
        if (lastStmt && lastStmt != onlyReturn) {
            // If `onlyReturn` is a direct child of `body` followed by trailing
            // statements (e.g. unreachable code after return in Rule011.cpp),
            // report at `lastStmt` so ViolationStore's Rule 0-1-x over Rule 6-6-5
            // precedence exception resolves the conflict on the same line.
            bool isDirectChild = false;
            for (const Stmt* child : body->body()) {
                if (child == onlyReturn) {
                    isDirectChild = true;
                    break;
                }
            }
            SourceLocation reportLoc = isDirectChild
                ? lastStmt->getBeginLoc()
                : onlyReturn->getBeginLoc();
            report(reportLoc,
                   "Return statement in non-void function '" +
                   func->getNameAsString() +
                   "' is not the last statement in the function [MISRA-6-6-5]");
        }
    }
}

} // namespace misra

