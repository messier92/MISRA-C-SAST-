#include "Rule019.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Analysis/CFG.h"

#include <queue>
#include <unordered_set>

using namespace clang;
using namespace clang::ast_matchers;

namespace misra {

namespace {

bool isCandidateLocalVar(const VarDecl* vd) {
    if (!vd) return false;
    if (!vd->hasLocalStorage() || isa<ParmVarDecl>(vd) || isa<ImplicitParamDecl>(vd)) {
        return false;
    }
    if (vd->isImplicit() || vd->isExceptionVariable()) return false;
    if (vd->getType().isVolatileQualified() || vd->getType()->isReferenceType()) {
        return false;
    }
    return true;
}

const VarDecl* getDirectAssignedLocal(const Stmt* s,
                                      const Expr** rhsOut = nullptr,
                                      const DeclRefExpr** lhsDreOut = nullptr) {
    if (!s) return nullptr;
    if (const auto* bo = dyn_cast<BinaryOperator>(s)) {
        if (bo->getOpcode() == BO_Assign) {
            const Expr* lhs = bo->getLHS()->IgnoreParenImpCasts();
            if (const auto* dre = dyn_cast<DeclRefExpr>(lhs)) {
                if (const auto* vd = dyn_cast<VarDecl>(dre->getDecl())) {
                    if (rhsOut) *rhsOut = bo->getRHS();
                    if (lhsDreOut) *lhsDreOut = dre;
                    return vd->getCanonicalDecl();
                }
            }
        }
    }
    return nullptr;
}

/// Returns true if `e` may have observable side effects (such as a function call,
/// volatile access, assignment, increment/decrement, throw, or new/delete).
bool exprMayHaveSideEffects(const Expr* e, ASTContext& ctx) {
    if (!e) return false;
    if (e->HasSideEffects(ctx)) return true;

    class CallOrVolatileFinder : public RecursiveASTVisitor<CallOrVolatileFinder> {
    public:
        bool VisitCallExpr(CallExpr*) {
            found_ = true;
            return false;
        }
        bool VisitCXXConstructExpr(CXXConstructExpr*) {
            found_ = true;
            return false;
        }
        bool VisitCXXNewExpr(CXXNewExpr*) {
            found_ = true;
            return false;
        }
        bool VisitCXXDeleteExpr(CXXDeleteExpr*) {
            found_ = true;
            return false;
        }
        bool VisitCXXThrowExpr(CXXThrowExpr*) {
            found_ = true;
            return false;
        }
        bool VisitBinaryOperator(BinaryOperator* bo) {
            if (bo->isAssignmentOp()) {
                found_ = true;
                return false;
            }
            return true;
        }
        bool VisitUnaryOperator(UnaryOperator* uo) {
            if (uo->isIncrementDecrementOp()) {
                found_ = true;
                return false;
            }
            return true;
        }
        bool found_ = false;
    };

    CallOrVolatileFinder finder;
    finder.TraverseStmt(const_cast<Expr*>(e));
    return finder.found_;
}

class PureWriteAndAliasCollector
    : public RecursiveASTVisitor<PureWriteAndAliasCollector> {
public:
    bool VisitBinaryOperator(BinaryOperator* bo) {
        if (bo->getOpcode() == BO_Assign) {
            const DeclRefExpr* lhsDre = nullptr;
            if (getDirectAssignedLocal(bo, nullptr, &lhsDre)) {
                pureWriteDres_.insert(lhsDre);
            }
        }
        return true;
    }

    bool VisitUnaryOperator(UnaryOperator* uo) {
        if (uo->getOpcode() == UO_AddrOf) {
            const Expr* sub = uo->getSubExpr()->IgnoreParenImpCasts();
            if (const auto* dre = dyn_cast<DeclRefExpr>(sub)) {
                if (const auto* vd = dyn_cast<VarDecl>(dre->getDecl())) {
                    aliasedVars_.insert(vd->getCanonicalDecl());
                }
            }
        }
        return true;
    }

    bool VisitVarDecl(VarDecl* vd) {
        if (vd && vd->getType()->isReferenceType() && vd->hasInit()) {
            const Expr* init = vd->getInit()->IgnoreParenImpCasts();
            if (const auto* dre = dyn_cast<DeclRefExpr>(init)) {
                if (const auto* target = dyn_cast<VarDecl>(dre->getDecl())) {
                    aliasedVars_.insert(target->getCanonicalDecl());
                }
            }
        }
        return true;
    }

    std::unordered_set<const DeclRefExpr*> pureWriteDres_;
    std::unordered_set<const VarDecl*> aliasedVars_;
};

bool exprSubtreeReadsVar(const Stmt* s,
                         const VarDecl* targetVar,
                         const std::unordered_set<const DeclRefExpr*>& pureWriteDres) {
    if (!s) return false;
    if (isa<CompoundStmt>(s) || isa<IfStmt>(s) || isa<WhileStmt>(s) ||
        isa<DoStmt>(s) || isa<ForStmt>(s) || isa<SwitchStmt>(s)) {
        return false;
    }
    if (const auto* dre = dyn_cast<DeclRefExpr>(s)) {
        if (pureWriteDres.count(dre) == 0) {
            if (const auto* vd = dyn_cast<VarDecl>(dre->getDecl())) {
                if (vd->getCanonicalDecl() == targetVar) return true;
            }
        }
    }
    for (const Stmt* child : s->children()) {
        if (exprSubtreeReadsVar(child, targetVar, pureWriteDres)) return true;
    }
    return false;
}

bool stmtReadsVarDirectly(const Stmt* s,
                          const VarDecl* targetVar,
                          const std::unordered_set<const DeclRefExpr*>& pureWriteDres) {
    if (!s) return false;
    const Expr* rhs = nullptr;
    if (getDirectAssignedLocal(s, &rhs) == targetVar) {
        return exprSubtreeReadsVar(rhs, targetVar, pureWriteDres);
    }
    return exprSubtreeReadsVar(s, targetVar, pureWriteDres);
}

bool isStandaloneStmtContainer(const Stmt* parent, const Stmt* child) {
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
        return child == forStmt->getBody();
    }
    if (const auto* sc = dyn_cast<SwitchCase>(parent)) {
        return child == sc->getSubStmt();
    }
    if (const auto* ls = dyn_cast<LabelStmt>(parent)) {
        return child == ls->getSubStmt();
    }
    return false;
}

} // namespace

Rule019::Rule019()
    : Rule("MISRA-0-1-9",
           "There shall be no dead code.") {}

void Rule019::registerMatchers(MatchFinder& finder) {
    finder.addMatcher(
        functionDecl(isDefinition(), unless(isImplicit())).bind("func"),
        this);
}

void Rule019::run(const MatchFinder::MatchResult& result) {
    const auto* func = result.Nodes.getNodeAs<FunctionDecl>("func");
    if (!func || !func->hasBody()) return;
    if (func->isTemplateInstantiation()) return;

    ASTContext& ctx = *result.Context;
    const SourceManager& sm = *result.SourceManager;

    if (func->getBeginLoc().isInvalid() || sm.isInSystemHeader(func->getBeginLoc())) {
        return;
    }
    if (func->getReturnType()->isVoidType() && func->getNumParams() == 0) {
        return;
    }

    ParentBuilder parentBuilder;
    parentBuilder.build(ctx);

    Stmt* body = func->getBody();
    PureWriteAndAliasCollector collector;
    collector.TraverseStmt(body);

    // 1. Check for dead assignment statements via CFG reachability.
    CFG::BuildOptions buildOpts;
    if (std::unique_ptr<CFG> cfg = CFG::buildCFG(func, body, &ctx, buildOpts)) {
        for (const CFGBlock* block : *cfg) {
            if (!block) continue;
            for (size_t idx = 0; idx < block->size(); ++idx) {
                auto cfgStmt = (*block)[idx].getAs<CFGStmt>();
                if (!cfgStmt) continue;

                const Stmt* s = cfgStmt->getStmt();
                const Expr* rhs = nullptr;
                const VarDecl* targetVar = getDirectAssignedLocal(s, &rhs);
                if (!targetVar || !isCandidateLocalVar(targetVar)) continue;
                if (collector.aliasedVars_.count(targetVar) > 0) continue;

                if (s->getBeginLoc().isInvalid() ||
                    sm.isInSystemHeader(s->getBeginLoc())) {
                    continue;
                }
                if (!isReachable(s, ctx, parentBuilder)) continue;

                // Ensure the assignment is a standalone statement (not a sub-expression).
                const Stmt* parent = parentBuilder.getParent(s);
                while (parent && (isa<ExprWithCleanups>(parent) || isa<ParenExpr>(parent))) {
                    parent = parentBuilder.getParent(parent);
                }
                if (!isStandaloneStmtContainer(parent, s)) continue;

                // If the RHS itself has side effects (e.g. calls an impure function),
                // the statement is not dead code.
                if (exprMayHaveSideEffects(rhs, ctx)) continue;

                bool canReachRead = false;
                bool canReachOverwrite = false;

                auto scanRange = [&](const CFGBlock* b, size_t startIdx) -> bool {
                    for (size_t i = startIdx; i < b->size(); ++i) {
                        auto cs = (*b)[i].getAs<CFGStmt>();
                        if (!cs) continue;
                        const Stmt* curStmt = cs->getStmt();
                        if (stmtReadsVarDirectly(curStmt, targetVar,
                                                 collector.pureWriteDres_)) {
                            canReachRead = true;
                            return false;
                        }
                        if (curStmt != s && getDirectAssignedLocal(curStmt) == targetVar) {
                            canReachOverwrite = true;
                            return false;
                        }
                    }
                    return true;
                };

                std::queue<const CFGBlock*> q;
                std::unordered_set<unsigned> visitedBlocks;

                if (scanRange(block, idx + 1)) {
                    for (auto it = block->succ_begin(); it != block->succ_end(); ++it) {
                        if (const CFGBlock* succ = *it) {
                            if (visitedBlocks.insert(succ->getBlockID()).second) {
                                q.push(succ);
                            }
                        }
                    }
                }

                while (!q.empty() && !canReachRead) {
                    const CFGBlock* curBlock = q.front();
                    q.pop();

                    if (scanRange(curBlock, 0)) {
                        for (auto it = curBlock->succ_begin(); it != curBlock->succ_end(); ++it) {
                            if (const CFGBlock* succ = *it) {
                                if (visitedBlocks.insert(succ->getBlockID()).second) {
                                    q.push(succ);
                                }
                            }
                        }
                    }
                }

                // Report dead assignments that are never subsequently read and
                // not already covered as overwritten assignments by Rule 0-1-6.
                if (!canReachRead && !canReachOverwrite) {
                    report(s->getBeginLoc(),
                           "Dead code: value assigned to '" +
                           targetVar->getNameAsString() +
                           "' is never subsequently used.");
                }
            }
        }
    }

    // 2. Check for pure/redundant expression statements that have no side effects.
    std::function<void(const Stmt*)> checkPureExprStmts = [&](const Stmt* s) {
        if (!s) return;
        if (const auto* cs = dyn_cast<CompoundStmt>(s)) {
            for (const Stmt* child : cs->body()) {
                if (const auto* expr = dyn_cast_or_null<Expr>(child)) {
                    const Expr* stripped = expr->IgnoreImplicit();
                    if (stripped->getBeginLoc().isValid() &&
                        !sm.isInSystemHeader(stripped->getBeginLoc()) &&
                        !stripped->getType()->isVoidType() &&
                        isReachable(child, ctx, parentBuilder) &&
                        !exprMayHaveSideEffects(stripped, ctx)) {
                        report(stripped->getBeginLoc(),
                               "Dead operation: expression statement has no side effects.");
                    }
                }
                checkPureExprStmts(child);
            }
            return;
        }
        for (const Stmt* child : s->children()) {
            if (child) checkPureExprStmts(child);
        }
    };

    checkPureExprStmts(body);
}

} // namespace misra
