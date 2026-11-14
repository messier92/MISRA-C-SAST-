#include "Rule016.h"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Analysis/CFG.h"
#include "clang/Lex/Lexer.h"

#include <cctype>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace clang;
using namespace clang::ast_matchers;

namespace misra {

namespace {

bool isIdentChar016(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

bool isEmitSignalVarDecl(const VarDecl* vd, const SourceManager& sm) {
    if (!vd || vd->getName().empty()) return false;
    SourceLocation spellingLoc = sm.getSpellingLoc(vd->getLocation());
    if (spellingLoc.isInvalid()) return false;

    FileID fid = sm.getFileID(spellingLoc);
    unsigned lineNo = sm.getSpellingLineNumber(spellingLoc);
    SourceLocation lineStart = sm.translateLineCol(fid, lineNo, 1);
    if (lineStart.isInvalid()) return false;

    bool invalid = false;
    const char* buf = sm.getCharacterData(lineStart, &invalid);
    if (invalid || !buf) return false;

    const char* p = buf;
    while (*p && *p != '\n' && *p != '\r') ++p;
    std::string lineText(buf, p);

    size_t n = lineText.size();
    for (size_t i = 0; i < n;) {
        if (isIdentChar016(lineText[i]) && !std::isdigit(static_cast<unsigned char>(lineText[i]))) {
            size_t start = i;
            while (i < n && isIdentChar016(lineText[i])) ++i;
            llvm::StringRef kw(lineText.data() + start, i - start);
            if (kw == "emit" || kw == "Q_EMIT") {
                size_t j = i;
                while (j < n && std::isspace(static_cast<unsigned char>(lineText[j]))) ++j;
                if (j == i) continue;
                while (j < n && isIdentChar016(lineText[j])) {
                    size_t idStart = j;
                    while (j < n && isIdentChar016(lineText[j])) ++j;
                    llvm::StringRef ident(lineText.data() + idStart, j - idStart);
                    size_t k = j;
                    while (k < n && std::isspace(static_cast<unsigned char>(lineText[k]))) ++k;
                    if (k < n && lineText[k] == '(') {
                        return ident == vd->getName();
                    }
                    break;
                }
            }
        } else {
            ++i;
        }
    }
    return false;
}

bool isLocalVarReadInFunctionText(const VarDecl* vd,
                                  const SourceManager& sm,
                                  const LangOptions& langOpts) {
    if (!vd || !vd->hasLocalStorage() || vd->getName().empty()) return false;
    if (isEmitSignalVarDecl(vd, sm)) return false;

    const auto* fd = dyn_cast_or_null<FunctionDecl>(vd->getDeclContext());
    if (!fd || !fd->hasBody()) return false;

    Stmt* body = fd->getBody();
    SourceLocation declLoc = sm.getSpellingLoc(vd->getLocation());
    SourceLocation bodyEnd = sm.getSpellingLoc(body->getEndLoc());
    if (declLoc.isInvalid() || bodyEnd.isInvalid()) return false;

    FileID fid = sm.getFileID(declLoc);
    if (fid != sm.getFileID(bodyEnd)) return false;

    unsigned declLine = sm.getSpellingLineNumber(declLoc);
    SourceLocation scanStart = sm.translateLineCol(fid, declLine + 1, 1);
    if (scanStart.isInvalid() || !sm.isBeforeInTranslationUnit(scanStart, bodyEnd)) {
        return false;
    }

    bool invalid = false;
    llvm::StringRef buffer = sm.getBufferData(fid, &invalid);
    if (invalid) return false;

    const char* startPtr = sm.getCharacterData(scanStart, &invalid);
    if (invalid || !startPtr) return false;
    const char* endPtr = sm.getCharacterData(bodyEnd, &invalid);
    if (invalid || !endPtr || startPtr >= endPtr) return false;

    Lexer lexer(scanStart, langOpts, buffer.begin(), startPtr, endPtr);
    lexer.SetCommentRetentionState(false);

    llvm::StringRef targetName = vd->getName();
    Token tok;
    while (!lexer.LexFromRawLexer(tok)) {
        if (tok.is(tok::raw_identifier) && tok.getRawIdentifier() == targetName) {
            Token nextTok;
            if (!lexer.LexFromRawLexer(nextTok)) {
                if (nextTok.is(tok::equal)) {
                    continue;
                }
            }
            return true;
        }
    }
    return false;
}

bool isTargetLocalVar(const VarDecl* vd) {
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

bool isTargetFileStaticVar(const VarDecl* vd) {
    if (!vd || !vd->isFileVarDecl()) return false;
    if (vd->isImplicit() || vd->isConstexpr()) return false;
    if (vd->getType().isVolatileQualified() || vd->getType()->isReferenceType()) {
        return false;
    }
    if (vd->getStorageClass() == SC_Static) return true;
    if (vd->getType().isConstQualified() && !vd->hasExternalStorage()) return true;
    return false;
}

bool hasExplicitValueInit(const VarDecl* vd) {
    if (!vd || !vd->hasInit()) return false;
    const Expr* init = vd->getInit();
    if (!init) return false;
    init = init->IgnoreImplicit();

    if (isa<ExplicitCastExpr>(init) || isa<CXXTemporaryObjectExpr>(init)) {
        return false;
    }

    if (const auto* construct = dyn_cast<CXXConstructExpr>(init)) {
        if (construct->getNumArgs() == 0 &&
            vd->getInitStyle() == VarDecl::CallInit &&
            !construct->isListInitialization()) {
            return false;
        }
    }
    return true;
}

/// If `s` is a simple assignment `var = rhs` (or overloaded `operator=`) to a
/// target local variable, returns that canonical `VarDecl*` and sets `lhsDre`
/// to the LHS `DeclRefExpr*`.
const VarDecl* getDirectAssignedVar(const Stmt* s, const DeclRefExpr** lhsDre = nullptr) {
    if (!s) return nullptr;
    if (const auto* bo = dyn_cast<BinaryOperator>(s)) {
        if (bo->getOpcode() == BO_Assign) {
            const Expr* lhs = bo->getLHS()->IgnoreParenImpCasts();
            if (const auto* dre = dyn_cast<DeclRefExpr>(lhs)) {
                if (const auto* vd = dyn_cast<VarDecl>(dre->getDecl())) {
                    if (lhsDre) *lhsDre = dre;
                    return vd->getCanonicalDecl();
                }
            }
        }
    } else if (const auto* oce = dyn_cast<CXXOperatorCallExpr>(s)) {
        if (oce->getOperator() == OO_Equal && oce->getNumArgs() >= 2) {
            const Expr* lhs = oce->getArg(0)->IgnoreParenImpCasts();
            if (const auto* dre = dyn_cast<DeclRefExpr>(lhs)) {
                if (const auto* vd = dyn_cast<VarDecl>(dre->getDecl())) {
                    if (lhsDre) *lhsDre = dre;
                    return vd->getCanonicalDecl();
                }
            }
        }
    }
    return nullptr;
}

/// Collects variable reads and writes across an AST subtree.
class VarAccessCollector : public SafeASTVisitor<VarAccessCollector> {
public:
    bool VisitBinaryOperator(BinaryOperator* bo) {
        if (bo->getOpcode() == BO_Assign) {
            const DeclRefExpr* lhsDre = nullptr;
            if (const VarDecl* vd = getDirectAssignedVar(bo, &lhsDre)) {
                pureWriteDres_.insert(lhsDre);
                writtenVars_.insert(vd);
            }
        }
        return true;
    }

    bool VisitCXXOperatorCallExpr(CXXOperatorCallExpr* oce) {
        if (oce->getOperator() == OO_Equal) {
            const DeclRefExpr* lhsDre = nullptr;
            if (const VarDecl* vd = getDirectAssignedVar(oce, &lhsDre)) {
                pureWriteDres_.insert(lhsDre);
                writtenVars_.insert(vd);
            }
        }
        return true;
    }

    bool VisitDeclRefExpr(DeclRefExpr* dre) {
        if (const auto* vd = dyn_cast<VarDecl>(dre->getDecl())) {
            const VarDecl* canon = vd->getCanonicalDecl();
            if (pureWriteDres_.count(dre) == 0) {
                readVars_.insert(canon);
            }
        }
        return true;
    }

    std::unordered_set<const DeclRefExpr*> pureWriteDres_;
    std::unordered_set<const VarDecl*> readVars_;
    std::unordered_set<const VarDecl*> writtenVars_;
};

/// Checks whether `s` (or its expression/return/decl subtree, without entering
/// nested control-flow statements) reads `targetVar`.
bool exprSubtreeReadsVar(const Stmt* s,
                         const VarDecl* targetVar,
                         const std::unordered_set<const DeclRefExpr*>& pureWriteDres,
                         unsigned depth = 0) {
    if (!s || depth >= 128) return false;
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
        if (exprSubtreeReadsVar(child, targetVar, pureWriteDres, depth + 1)) return true;
    }
    return false;
}

bool stmtReadsVarDirectly(const Stmt* s,
                          const VarDecl* targetVar,
                          const std::unordered_set<const DeclRefExpr*>& pureWriteDres) {
    if (!s) return false;
    // If `s` is a direct assignment `targetVar = rhs`, only inspect `rhs` for reads.
    if (getDirectAssignedVar(s) == targetVar) {
        if (const auto* bo = dyn_cast<BinaryOperator>(s)) {
            return exprSubtreeReadsVar(bo->getRHS(), targetVar, pureWriteDres);
        }
        if (const auto* oce = dyn_cast<CXXOperatorCallExpr>(s)) {
            if (oce->getNumArgs() >= 2) {
                return exprSubtreeReadsVar(oce->getArg(1), targetVar, pureWriteDres);
            }
        }
        return false;
    }
    return exprSubtreeReadsVar(s, targetVar, pureWriteDres);
}

} // namespace

Rule016::Rule016()
    : Rule("MISRA-0-1-6",
           "A project shall not contain instances of non-volatile variables "
           "being given values that are never subsequently used.") {}

void Rule016::registerMatchers(MatchFinder& finder) {
    finder.addMatcher(
        translationUnitDecl().bind("tu"),
        this);
}

void Rule016::run(const MatchFinder::MatchResult& result) {
    const auto* tu = result.Nodes.getNodeAs<TranslationUnitDecl>("tu");
    if (!tu) return;

    ASTContext& ctx = *result.Context;
    const SourceManager& sm = *result.SourceManager;

    ParentBuilder parentBuilder;
    parentBuilder.build(ctx);

    // 1. Collect file-scope static variables and all functions in the TU.
    VarAccessCollector tuAccess;
    tuAccess.TraverseAST(ctx);

    class TUDeclFinder : public SafeASTVisitor<TUDeclFinder> {
    public:
        TUDeclFinder(const SourceManager& sm, const ViolationStore* store)
            : sm_(sm), store_(store) {}

        bool VisitVarDecl(VarDecl* vd) {
            if (!vd || !Rule::isProjectLocation(vd->getBeginLoc(), sm_, store_)) {
                return true;
            }
            if (isTargetFileStaticVar(vd)) {
                fileStaticVars_.push_back(vd);
            }
            return true;
        }

        bool VisitFunctionDecl(FunctionDecl* fd) {
            if (!fd || !fd->hasBody() || fd->isImplicit() ||
                fd->isTemplateInstantiation()) {
                return true;
            }
            if (!Rule::isProjectLocation(fd->getBeginLoc(), sm_, store_)) {
                return true;
            }
            functions_.push_back(fd);
            return true;
        }

        const SourceManager& sm_;
        const ViolationStore* store_ = nullptr;
        std::vector<const VarDecl*> fileStaticVars_;
        std::vector<const FunctionDecl*> functions_;
    };

    TUDeclFinder finder(sm, store_);
    finder.TraverseAST(ctx);

    // Check file-scope static variables with unused initializations.
    for (const VarDecl* vd : finder.fileStaticVars_) {
        const VarDecl* canon = vd->getCanonicalDecl();
        if (hasExplicitValueInit(vd) &&
            tuAccess.readVars_.count(canon) == 0 &&
            tuAccess.writtenVars_.count(canon) == 0) {
            report(vd->getBeginLoc(),
                   "Variable '" + vd->getNameAsString() +
                   "' is initialized with a value that is never subsequently used.");
        }
    }

    // 2. Analyze each function for unused local initializations and overwritten assignments.
    for (const FunctionDecl* func : finder.functions_) {
        Stmt* body = func->getBody();
        if (!body) continue;

        VarAccessCollector funcAccess;
        funcAccess.TraverseStmt(body);

        // Collect local variable declarations in this function.
        class LocalVarFinder : public SafeASTVisitor<LocalVarFinder> {
        public:
            explicit LocalVarFinder(const SourceManager& sm) : sm_(sm) {}
            bool VisitVarDecl(VarDecl* vd) {
                if (!vd || vd->getBeginLoc().isInvalid() ||
                    sm_.isInSystemHeader(vd->getBeginLoc())) {
                    return true;
                }
                if (isTargetLocalVar(vd)) {
                    localVars_.push_back(vd);
                }
                return true;
            }
            const SourceManager& sm_;
            std::vector<const VarDecl*> localVars_;
        };

        LocalVarFinder localFinder(sm);
        localFinder.TraverseStmt(body);

        for (const VarDecl* vd : localFinder.localVars_) {
            const VarDecl* canon = vd->getCanonicalDecl();
            if (funcAccess.readVars_.count(canon) == 0 &&
                isLocalVarReadInFunctionText(vd, sm, ctx.getLangOpts())) {
                funcAccess.readVars_.insert(canon);
            }
            // Exception in specification: "The only exception is the case where
            // a value is assigned at variable initialization and later overwritten."
            if ((hasExplicitValueInit(vd) || isEmitSignalVarDecl(vd, sm)) &&
                funcAccess.readVars_.count(canon) == 0 &&
                funcAccess.writtenVars_.count(canon) == 0) {
                report(vd->getBeginLoc(),
                       "Variable '" + vd->getNameAsString() +
                       "' is initialized with a value that is never subsequently used.");
            }
        }

        // Build CFG to detect assignments overwritten before any subsequent read.
        CFG::BuildOptions buildOpts;
        std::unique_ptr<CFG> cfg = CFG::buildCFG(func, body, &ctx, buildOpts);
        if (!cfg) continue;

        for (const CFGBlock* block : *cfg) {
            if (!block) continue;
            for (size_t idx = 0; idx < block->size(); ++idx) {
                CFGElement elem = (*block)[idx];
                auto cfgStmt = elem.getAs<CFGStmt>();
                if (!cfgStmt) continue;

                const Stmt* s = cfgStmt->getStmt();
                const VarDecl* targetVar = getDirectAssignedVar(s);
                if (!targetVar || !isTargetLocalVar(targetVar)) continue;

                if (s->getBeginLoc().isInvalid() ||
                    sm.isInSystemHeader(s->getBeginLoc())) {
                    continue;
                }
                if (!isReachable(s, ctx, parentBuilder)) continue;

                // Perform BFS in the CFG starting after (block, idx) to see if
                // targetVar can reach any read before being overwritten.
                bool canReachRead = false;
                bool canReachOverwrite = false;

                auto scanRange = [&](const CFGBlock* b, size_t startIdx) -> bool {
                    for (size_t i = startIdx; i < b->size(); ++i) {
                        auto cs = (*b)[i].getAs<CFGStmt>();
                        if (!cs) continue;
                        const Stmt* curStmt = cs->getStmt();
                        if (stmtReadsVarDirectly(curStmt, targetVar,
                                                 funcAccess.pureWriteDres_)) {
                            canReachRead = true;
                            return false; // Stop scanning this path (found read).
                        }
                        if (curStmt != s && getDirectAssignedVar(curStmt) == targetVar) {
                            canReachOverwrite = true;
                            return false; // Killed by overwrite on this path.
                        }
                    }
                    return true; // Reached end of block with value still live.
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

                if (!canReachRead && canReachOverwrite &&
                    funcAccess.readVars_.count(targetVar) > 0) {
                    report(s->getBeginLoc(),
                           "Value assigned to '" + targetVar->getNameAsString() +
                           "' is overwritten before being used.");
                }
            }
        }
    }
}

} // namespace misra

