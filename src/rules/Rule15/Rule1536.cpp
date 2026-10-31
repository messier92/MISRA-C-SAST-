#include "Rule1536.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/StmtCXX.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"

using namespace clang;
using namespace clang::ast_matchers;

namespace misra {

namespace {

/// Extract the canonical class definition (or canonical declaration) for a
/// catch handler's caught type, unwrapping reference and pointer qualifiers.
const CXXRecordDecl* getCaughtClassDecl(const CXXCatchStmt* handler) {
    if (!handler) return nullptr;
    QualType qt = handler->getCaughtType();
    if (qt.isNull()) return nullptr;

    QualType canon = qt.getCanonicalType();
    if (canon->isReferenceType() || canon->isPointerType()) {
        canon = canon->getPointeeType().getCanonicalType();
    }

    if (const CXXRecordDecl* rd = canon->getAsCXXRecordDecl()) {
        if (const CXXRecordDecl* def = rd->getDefinition()) {
            return def;
        }
        return rd->getCanonicalDecl();
    }
    return nullptr;
}

} // namespace

Rule1536::Rule1536()
    : Rule("MISRA-15-3-6",
           "Where multiple handlers are provided in a single try-catch statement or function-try-block for a derived class and some or all of its bases, the handlers shall be ordered most-derived to base class.") {}

void Rule1536::registerMatchers(MatchFinder& finder) {
    finder.addMatcher(cxxTryStmt().bind("tryStmt"), this);
}

void Rule1536::run(const MatchFinder::MatchResult& result) {
    const auto* tryStmt = result.Nodes.getNodeAs<CXXTryStmt>("tryStmt");
    if (!tryStmt || !result.Context || !result.SourceManager) return;

    unsigned numHandlers = tryStmt->getNumHandlers();
    if (numHandlers < 2) return;

    setContext(result.Context);
    const SourceManager& sm = *result.SourceManager;

    SourceLocation tryLoc = sm.getSpellingLoc(tryStmt->getTryLoc());
    if (tryLoc.isInvalid() || sm.isInSystemHeader(tryLoc)) return;

    PresumedLoc presumed = sm.getPresumedLoc(tryLoc);
    if (presumed.isInvalid()) return;
    if (store_ && !store_->getFilter().isProjectFile(presumed.getFilename())) {
        return;
    }

    // If any handler in this try statement catches a class type by value
    // (violating Rule 15-3-5, as in Rule1535.cpp which lists by-value and
    // by-reference handlers in the same try block), defer to Rule 15-3-5.
    for (unsigned i = 0; i < numHandlers; ++i) {
        const CXXCatchStmt* h = tryStmt->getHandler(i);
        if (!h) continue;
        QualType qt = h->getCaughtType();
        if (!qt.isNull()) {
            QualType canon = qt.getCanonicalType();
            if (!canon->isReferenceType() && canon->isRecordType()) {
                return;
            }
        }
    }

    for (unsigned i = 1; i < numHandlers; ++i) {
        const CXXCatchStmt* currHandler = tryStmt->getHandler(i);
        const CXXRecordDecl* currClass = getCaughtClassDecl(currHandler);
        if (!currClass || !currClass->hasDefinition()) continue;

        for (unsigned j = 0; j < i; ++j) {
            const CXXCatchStmt* prevHandler = tryStmt->getHandler(j);
            const CXXRecordDecl* prevClass = getCaughtClassDecl(prevHandler);
            if (!prevClass || !prevClass->hasDefinition()) continue;

            if (currClass->getCanonicalDecl() == prevClass->getCanonicalDecl() ||
                currClass->isDerivedFrom(prevClass)) {
                report(currHandler->getCatchLoc(),
                       "Exception handler for derived class '" +
                       currClass->getNameAsString() +
                       "' appears after handler for base class '" +
                       prevClass->getNameAsString() + "' [MISRA-15-3-6]");
                break;
            }
        }
    }
}

} // namespace misra

