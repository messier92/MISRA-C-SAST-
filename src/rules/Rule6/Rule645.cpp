#include "Rule645.h"

#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"

#include <vector>

using namespace clang;
using namespace clang::ast_matchers;

namespace misra {

namespace {

const Stmt* unwrapLabelsAndImplicit(const Stmt* s) {
    while (s) {
        if (const auto* ls = dyn_cast<LabelStmt>(s)) {
            s = ls->getSubStmt();
        } else if (const auto* expr = dyn_cast<Expr>(s)) {
            const Expr* next = expr->IgnoreParenImpCasts();
            if (const auto* ewc = dyn_cast<ExprWithCleanups>(next)) {
                next = ewc->getSubExpr();
            }
            if (next == s) break;
            s = next;
        } else {
            break;
        }
    }
    return s;
}

/// Returns true if statement `s` unconditionally terminates the enclosing
/// switch-clause with a `break` or `throw` statement (ignoring `break`s that
/// belong to nested loops or nested switch statements).
bool terminatesWithBreakOrThrow(const Stmt* s) {
    s = unwrapLabelsAndImplicit(s);
    if (!s) return false;

    if (isa<BreakStmt>(s) || isa<CXXThrowExpr>(s)) {
        return true;
    }

    if (const auto* cs = dyn_cast<CompoundStmt>(s)) {
        for (auto it = cs->body_rbegin(); it != cs->body_rend(); ++it) {
            if (!*it || isa<NullStmt>(*it)) continue;
            return terminatesWithBreakOrThrow(*it);
        }
        return false;
    }

    if (const auto* ifStmt = dyn_cast<IfStmt>(s)) {
        if (ifStmt->getThen() && ifStmt->getElse()) {
            return terminatesWithBreakOrThrow(ifStmt->getThen()) &&
                   terminatesWithBreakOrThrow(ifStmt->getElse());
        }
        return false;
    }

    return false;
}

bool switchHasNonCompliantClause(const SwitchStmt* sw) {
    if (!sw || !sw->getBody()) return false;

    const auto* compound = dyn_cast<CompoundStmt>(sw->getBody());
    if (!compound) return false;

    std::vector<std::vector<const Stmt*>> clauses;
    bool inClause = false;

    for (const Stmt* child : compound->body()) {
        const Stmt* cur = child;
        while (const auto* sc = dyn_cast_or_null<SwitchCase>(cur)) {
            if (!inClause || !clauses.back().empty()) {
                clauses.emplace_back();
            }
            inClause = true;
            cur = sc->getSubStmt();
        }
        if (inClause && cur && !isa<NullStmt>(cur)) {
            clauses.back().push_back(cur);
        }
    }

    for (const auto& clauseStmts : clauses) {
        if (clauseStmts.empty()) {
            // Empty switch-clause (e.g. `case 0: case 1:`) is compliant.
            continue;
        }
        const Stmt* lastStmt = clauseStmts.back();
        if (!terminatesWithBreakOrThrow(lastStmt)) {
            return true;
        }
    }

    return false;
}

} // namespace

Rule645::Rule645()
    : Rule("MISRA-6-4-5",
           "An unconditional throw or break statement shall terminate every non-empty switch-clause.") {}

void Rule645::registerMatchers(MatchFinder& finder) {
    finder.addMatcher(switchStmt().bind("switch"), this);
}

void Rule645::run(const MatchFinder::MatchResult& result) {
    const auto* sw = result.Nodes.getNodeAs<SwitchStmt>("switch");
    if (!sw) return;

    setContext(result.Context);
    const SourceManager& sm = *result.SourceManager;

    SourceLocation loc = sw->getBeginLoc();
    SourceLocation spellingLoc = sm.getSpellingLoc(loc);
    if (spellingLoc.isInvalid() || sm.isInSystemHeader(spellingLoc)) return;

    PresumedLoc presumed = sm.getPresumedLoc(spellingLoc);
    if (presumed.isInvalid()) return;
    if (store_ && !store_->getFilter().isProjectFile(presumed.getFilename())) {
        return;
    }

    if (switchHasNonCompliantClause(sw)) {
        report(loc,
               "Non-empty switch-clause does not terminate with an unconditional break or throw statement [MISRA-6-4-5]");
    }
}

} // namespace misra

