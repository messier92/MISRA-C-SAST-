#include "Rule011.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtCXX.h"

using namespace clang;
using namespace clang::ast_matchers;

namespace misra {

namespace {

constexpr unsigned kMaxStmtRecursionDepth = 128;

/// Returns true if `s` contains any label (`LabelStmt`, `CaseStmt`, `DefaultStmt`)
/// that could serve as a jump target into `s`.
bool containsLabel(const Stmt* s, unsigned depth = 0) {
    if (!s || depth >= kMaxStmtRecursionDepth) return false;
    if (isa<LabelStmt>(s) || isa<SwitchCase>(s)) return true;
    for (const Stmt* child : s->children()) {
        if (containsLabel(child, depth + 1)) return true;
    }
    return false;
}

/// Returns true if `s` contains a `BreakStmt` that breaks out of the current loop
/// (i.e. not nested inside an inner loop or switch).
bool containsLoopBreak(const Stmt* s, unsigned depth = 0) {
    if (!s || depth >= kMaxStmtRecursionDepth) return false;
    if (isa<BreakStmt>(s)) return true;
    if (isa<WhileStmt>(s) || isa<ForStmt>(s) || isa<CXXForRangeStmt>(s) ||
        isa<DoStmt>(s) || isa<SwitchStmt>(s)) {
        return false;
    }
    for (const Stmt* child : s->children()) {
        if (containsLoopBreak(child, depth + 1)) return true;
    }
    return false;
}

/// Unwraps label/case wrappers to get the underlying statement.
const Stmt* unwrapLabels(const Stmt* s) {
    unsigned hops = 0;
    while (s && ++hops <= kMaxStmtRecursionDepth) {
        if (const auto* sc = dyn_cast<SwitchCase>(s)) {
            s = sc->getSubStmt();
        } else if (const auto* ls = dyn_cast<LabelStmt>(s)) {
            s = ls->getSubStmt();
        } else {
            break;
        }
    }
    return s;
}

/// Returns true if `s` is a declaration statement where every declared variable
/// has no initializer (e.g. `int x;` at the top of a `switch` body).
bool isUninitializedDeclStmt(const Stmt* s) {
    const auto* ds = dyn_cast_or_null<DeclStmt>(s);
    if (!ds) return false;
    for (const Decl* d : ds->decls()) {
        if (const auto* vd = dyn_cast<VarDecl>(d)) {
            if (vd->hasInit()) return false;
        }
    }
    return true;
}

/// Returns true if `s` unconditionally transfers control out of the current
/// sequential block (via return, break, continue, throw, goto, an if-else
/// where both branches terminate, or an infinite loop without break).
bool terminatesBlock(const Stmt* s, ASTContext& ctx, unsigned depth = 0) {
    if (!s || depth >= kMaxStmtRecursionDepth) return false;
    s = unwrapLabels(s);
    if (!s) return false;

    if (const auto* expr = dyn_cast<Expr>(s)) {
        s = expr->IgnoreImplicit();
    }

    if (isa<ReturnStmt>(s)  ||
        isa<BreakStmt>(s)   ||
        isa<ContinueStmt>(s) ||
        isa<CXXThrowExpr>(s) ||
        isa<GotoStmt>(s)) {
        return true;
    }

    if (const auto* compound = dyn_cast<CompoundStmt>(s)) {
        bool terminated = false;
        for (const Stmt* child : compound->body()) {
            if (isa<SwitchCase>(child) || isa<LabelStmt>(child)) {
                terminated = false;
            }
            if (terminatesBlock(child, ctx, depth + 1)) {
                terminated = true;
            }
        }
        return terminated;
    }

    if (const auto* ifStmt = dyn_cast<IfStmt>(s)) {
        const Expr* cond = ifStmt->getCond();
        bool condVal = false;
        if (cond && !cond->isValueDependent() &&
            cond->EvaluateAsBooleanCondition(condVal, ctx)) {
            if (condVal) {
                return terminatesBlock(ifStmt->getThen(), ctx, depth + 1);
            } else {
                return ifStmt->getElse() && terminatesBlock(ifStmt->getElse(), ctx, depth + 1);
            }
        }
        if (ifStmt->getThen() && ifStmt->getElse()) {
            return terminatesBlock(ifStmt->getThen(), ctx, depth + 1) &&
                   terminatesBlock(ifStmt->getElse(), ctx, depth + 1);
        }
        return false;
    }

    if (const auto* whileStmt = dyn_cast<WhileStmt>(s)) {
        const Expr* cond = whileStmt->getCond();
        bool condVal = false;
        if (cond && !cond->isValueDependent() &&
            cond->EvaluateAsBooleanCondition(condVal, ctx) && condVal) {
            if (!containsLoopBreak(whileStmt->getBody())) {
                return true;
            }
        }
        return false;
    }

    if (const auto* forStmt = dyn_cast<ForStmt>(s)) {
        const Expr* cond = forStmt->getCond();
        bool alwaysTrue = (cond == nullptr);
        bool condVal = false;
        if (cond && !cond->isValueDependent() &&
            cond->EvaluateAsBooleanCondition(condVal, ctx) && condVal) {
            alwaysTrue = true;
        }
        if (alwaysTrue && !containsLoopBreak(forStmt->getBody())) {
            return true;
        }
        return false;
    }

    if (const auto* doStmt = dyn_cast<DoStmt>(s)) {
        const Expr* cond = doStmt->getCond();
        bool condVal = false;
        if (cond && !cond->isValueDependent() &&
            cond->EvaluateAsBooleanCondition(condVal, ctx) && condVal) {
            if (!containsLoopBreak(doStmt->getBody())) {
                return true;
            }
        }
        return false;
    }

    return false;
}

/// Returns the first executable statement location for reporting an unreachable branch/block.
SourceLocation getFirstExecutableLoc(const Stmt* s) {
    if (!s) return SourceLocation();
    if (const auto* compound = dyn_cast<CompoundStmt>(s)) {
        for (const Stmt* child : compound->body()) {
            if (!isa<NullStmt>(child)) {
                return child->getBeginLoc();
            }
        }
        return SourceLocation();
    }
    if (isa<NullStmt>(s)) return SourceLocation();
    return s->getBeginLoc();
}

} // namespace

Rule011::Rule011()
    : Rule("MISRA-0-1-1",
           "A project shall not contain unreachable code.") {}

void Rule011::registerMatchers(MatchFinder& finder) {
    finder.addMatcher(
        functionDecl(isDefinition(), unless(isImplicit())).bind("func"),
        this);
}

void Rule011::run(const MatchFinder::MatchResult& result) {
    const auto* func = result.Nodes.getNodeAs<FunctionDecl>("func");
    if (!func || !func->hasBody()) return;
    if (func->isTemplateInstantiation()) return;

    const SourceManager& sm = *result.SourceManager;
    ASTContext& ctx = *result.Context;

    if (func->getBeginLoc().isInvalid() || sm.isInSystemHeader(func->getBeginLoc())) {
        return;
    }

    std::function<void(const Stmt*, bool, unsigned)> checkStmt =
        [&](const Stmt* s, bool isSwitchBody, unsigned depth) {
        if (!s || depth >= kMaxStmtRecursionDepth) return;

        if (const auto* compound = dyn_cast<CompoundStmt>(s)) {
            bool seenTerminator = isSwitchBody;

            for (const Stmt* child : compound->body()) {
                if (!child) continue;

                if (isa<SwitchCase>(child) || isa<LabelStmt>(child)) {
                    seenTerminator = false;
                    const Stmt* sub = unwrapLabels(child);
                    if (sub) {
                        checkStmt(sub, false, depth + 1);
                        if (terminatesBlock(sub, ctx)) {
                            seenTerminator = true;
                        }
                    }
                    continue;
                }

                SourceLocation loc = child->getBeginLoc();
                if (loc.isInvalid() || sm.isInSystemHeader(loc)) continue;

                if (seenTerminator) {
                    if (isSwitchBody && isUninitializedDeclStmt(child)) {
                        continue;
                    }
                    if (!isa<NullStmt>(child) && !containsLabel(child)) {
                        report(loc, "Unreachable code detected.");
                        continue;
                    }
                }

                checkStmt(child, false, depth + 1);

                if (terminatesBlock(child, ctx)) {
                    seenTerminator = true;
                }
            }
            return;
        }

        if (const auto* switchStmt = dyn_cast<SwitchStmt>(s)) {
            checkStmt(switchStmt->getBody(), true, depth + 1);
            return;
        }

        if (const auto* ifStmt = dyn_cast<IfStmt>(s)) {
            const Expr* cond = ifStmt->getCond();
            bool condVal = false;
            if (cond && !cond->isValueDependent() &&
                !cond->getBeginLoc().isMacroID() &&
                cond->EvaluateAsBooleanCondition(condVal, ctx)) {
                if (!condVal) {
                    if (ifStmt->getThen() && !containsLabel(ifStmt->getThen())) {
                        SourceLocation loc = getFirstExecutableLoc(ifStmt->getThen());
                        if (loc.isValid() && !sm.isInSystemHeader(loc)) {
                            report(loc, "Unreachable code detected.");
                        }
                    }
                    if (ifStmt->getElse()) {
                        checkStmt(ifStmt->getElse(), false, depth + 1);
                    }
                    return;
                } else {
                    if (ifStmt->getThen()) {
                        checkStmt(ifStmt->getThen(), false, depth + 1);
                    }
                    if (ifStmt->getElse() && !containsLabel(ifStmt->getElse())) {
                        SourceLocation loc = getFirstExecutableLoc(ifStmt->getElse());
                        if (loc.isValid() && !sm.isInSystemHeader(loc)) {
                            report(loc, "Unreachable code detected.");
                        }
                    }
                    return;
                }
            }
            if (ifStmt->getThen()) checkStmt(ifStmt->getThen(), false, depth + 1);
            if (ifStmt->getElse()) checkStmt(ifStmt->getElse(), false, depth + 1);
            return;
        }

        if (const auto* whileStmt = dyn_cast<WhileStmt>(s)) {
            const Expr* cond = whileStmt->getCond();
            bool condVal = false;
            if (cond && !cond->isValueDependent() &&
                !cond->getBeginLoc().isMacroID() &&
                cond->EvaluateAsBooleanCondition(condVal, ctx) && !condVal) {
                if (whileStmt->getBody() && !containsLabel(whileStmt->getBody())) {
                    SourceLocation loc = getFirstExecutableLoc(whileStmt->getBody());
                    if (loc.isValid() && !sm.isInSystemHeader(loc)) {
                        report(loc, "Unreachable code detected.");
                    }
                    return;
                }
            }
            if (whileStmt->getBody()) checkStmt(whileStmt->getBody(), false, depth + 1);
            return;
        }

        if (const auto* forStmt = dyn_cast<ForStmt>(s)) {
            const Expr* cond = forStmt->getCond();
            bool condVal = false;
            if (cond && !cond->isValueDependent() &&
                !cond->getBeginLoc().isMacroID() &&
                cond->EvaluateAsBooleanCondition(condVal, ctx) && !condVal) {
                if (forStmt->getBody() && !containsLabel(forStmt->getBody())) {
                    SourceLocation loc = getFirstExecutableLoc(forStmt->getBody());
                    if (loc.isValid() && !sm.isInSystemHeader(loc)) {
                        report(loc, "Unreachable code detected.");
                    }
                    return;
                }
            }
            if (forStmt->getBody()) checkStmt(forStmt->getBody(), false, depth + 1);
            return;
        }

        for (const Stmt* child : s->children()) {
            if (child) checkStmt(child, false, depth + 1);
        }
    };

    checkStmt(func->getBody(), false, 0);
}

} // namespace misra
