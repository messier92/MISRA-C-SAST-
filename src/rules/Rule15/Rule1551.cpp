#include "Rule1551.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/StmtCXX.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"

#include <vector>

using namespace clang;
using namespace clang::ast_matchers;

namespace misra {

namespace {

/// Visits statements belonging directly to a destructor's execution scope to
/// find explicit `throw` expressions that are not enclosed in a `try-catch`
/// block or `catch` handler, and not inside a nested lambda or local class.
class DestructorThrowVisitor : public RecursiveASTVisitor<DestructorThrowVisitor> {
public:
    std::vector<const CXXThrowExpr*> uncaughtThrows;

    // Exclude nested lambdas and local classes/methods.
    bool TraverseLambdaExpr(LambdaExpr*) { return true; }
    bool TraverseCXXRecordDecl(CXXRecordDecl*) { return true; }
    bool TraverseFunctionDecl(FunctionDecl*) { return true; }

    // By specification:
    // - The presence of a catch statement corresponding to a try block is
    //   considered sufficient indication that an exception is caught.
    // - Throw statements inside catch blocks are not flagged.
    bool TraverseCXXTryStmt(CXXTryStmt* tryStmt) {
        if (!tryStmt) return true;
        if (tryStmt->getNumHandlers() > 0) {
            return true;
        }
        return RecursiveASTVisitor<DestructorThrowVisitor>::TraverseCXXTryStmt(tryStmt);
    }

    bool TraverseCXXCatchStmt(CXXCatchStmt*) { return true; }

    bool VisitCXXThrowExpr(const CXXThrowExpr* throwExpr) {
        if (throwExpr) {
            uncaughtThrows.push_back(throwExpr);
        }
        return true;
    }
};

} // namespace

Rule1551::Rule1551()
    : Rule("MISRA-15-5-1",
           "A class destructor shall not exit with an exception.") {}

void Rule1551::registerMatchers(MatchFinder& finder) {
    finder.addMatcher(cxxDestructorDecl(isDefinition()).bind("dtor"), this);
}

void Rule1551::run(const MatchFinder::MatchResult& result) {
    const auto* dtor = result.Nodes.getNodeAs<CXXDestructorDecl>("dtor");
    if (!dtor || !result.Context || !result.SourceManager) return;

    if (dtor->isImplicit() || !dtor->hasBody()) return;
    if (dtor->isDependentContext() ||
        dtor->getDescribedFunctionTemplate() != nullptr) {
        return;
    }

    setContext(result.Context);
    const SourceManager& sm = *result.SourceManager;

    SourceLocation dtorLoc = sm.getSpellingLoc(dtor->getLocation());
    if (dtorLoc.isInvalid() || sm.isInSystemHeader(dtorLoc)) return;

    PresumedLoc presumed = sm.getPresumedLoc(dtorLoc);
    if (presumed.isInvalid()) return;
    if (store_ && !store_->getFilter().isProjectFile(presumed.getFilename())) {
        return;
    }

    // If the destructor body itself is a function-try-block with catch handlers,
    // TraverseStmt(dtor->getBody()) invokes TraverseCXXTryStmt and treats it as caught.
    DestructorThrowVisitor visitor;
    visitor.TraverseStmt(dtor->getBody());

    for (const CXXThrowExpr* throwExpr : visitor.uncaughtThrows) {
        SourceLocation loc = throwExpr->getThrowLoc();
        if (loc.isInvalid()) {
            loc = throwExpr->getBeginLoc();
        }
        report(loc,
               "Destructor '" + dtor->getNameAsString() +
               "' shall not exit with an exception [MISRA-15-5-1]");
    }
}

} // namespace misra

