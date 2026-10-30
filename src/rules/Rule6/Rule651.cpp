#include "Rule651.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"

#include <functional>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

using namespace clang;
using namespace clang::ast_matchers;

namespace misra {

namespace {

const VarDecl* extractVarDecl(const Expr* e) {
    if (!e) return nullptr;
    e = e->IgnoreParenImpCasts();
    if (const auto* dre = dyn_cast<DeclRefExpr>(e)) {
        if (const auto* vd = dyn_cast<VarDecl>(dre->getDecl())) {
            return vd->getCanonicalDecl();
        }
    }
    return nullptr;
}

class InitVarCollector : public RecursiveASTVisitor<InitVarCollector> {
public:
    bool VisitVarDecl(VarDecl* vd) {
        if (vd && !vd->isImplicit()) {
            vars_.insert(vd->getCanonicalDecl());
        }
        return true;
    }

    bool VisitBinaryOperator(BinaryOperator* bop) {
        if (bop && bop->isAssignmentOp()) {
            if (const VarDecl* vd = extractVarDecl(bop->getLHS())) {
                vars_.insert(vd);
            }
        }
        return true;
    }

    std::unordered_set<const VarDecl*> vars_;
};

class CondVarCollector : public RecursiveASTVisitor<CondVarCollector> {
public:
    bool VisitDeclRefExpr(DeclRefExpr* dre) {
        if (dre) {
            if (const auto* vd = dyn_cast<VarDecl>(dre->getDecl())) {
                vars_.insert(vd->getCanonicalDecl());
            }
        }
        return true;
    }

    std::unordered_set<const VarDecl*> vars_;
};

class ModifiedVarCollector : public RecursiveASTVisitor<ModifiedVarCollector> {
public:
    bool VisitUnaryOperator(UnaryOperator* uop) {
        if (uop && uop->isIncrementDecrementOp()) {
            if (const VarDecl* vd = extractVarDecl(uop->getSubExpr())) {
                vars_.insert(vd);
            }
        }
        return true;
    }

    bool VisitBinaryOperator(BinaryOperator* bop) {
        if (bop && bop->isAssignmentOp()) {
            if (const VarDecl* vd = extractVarDecl(bop->getLHS())) {
                vars_.insert(vd);
            }
        }
        return true;
    }

    bool VisitCXXOperatorCallExpr(CXXOperatorCallExpr* oce) {
        if (!oce || oce->getNumArgs() == 0) return true;
        OverloadedOperatorKind op = oce->getOperator();
        if (oce->isAssignmentOp() || op == OO_PlusPlus || op == OO_MinusMinus) {
            if (const VarDecl* vd = extractVarDecl(oce->getArg(0))) {
                vars_.insert(vd);
            }
        }
        return true;
    }

    std::unordered_set<const VarDecl*> vars_;
};

bool isForLoopNonCompliant(const ForStmt* forStmt) {
    if (!forStmt) return false;

    InitVarCollector initCollector;
    if (const Stmt* init = forStmt->getInit()) {
        initCollector.TraverseStmt(const_cast<Stmt*>(init));
    }

    CondVarCollector condCollector;
    if (const Expr* cond = forStmt->getCond()) {
        condCollector.TraverseStmt(const_cast<Expr*>(cond));
    }

    ModifiedVarCollector incCollector;
    if (const Expr* inc = forStmt->getInc()) {
        incCollector.TraverseStmt(const_cast<Expr*>(inc));
    }

    // 1. Multiple loop counters declared/initialized in `init`.
    if (initCollector.vars_.size() > 1) {
        return true;
    }

    // 2. Any variable declared/initialized in `init` has floating-point type.
    for (const VarDecl* vd : initCollector.vars_) {
        if (vd->getType()->isFloatingType()) {
            return true;
        }
    }

    // 3. A loop counter must be modified in the `for` increment expression,
    //    and at most one loop counter may be modified there.
    if (incCollector.vars_.size() != 1) {
        return true;
    }

    const VarDecl* counter = *incCollector.vars_.begin();
    if (counter->getType()->isFloatingType()) {
        return true;
    }

    if (!initCollector.vars_.empty() &&
        initCollector.vars_.count(counter) == 0) {
        return true;
    }

    if (!condCollector.vars_.empty() &&
        condCollector.vars_.count(counter) == 0) {
        return true;
    }

    return false;
}

/// Scans an unparsed function body (when Clang skipped parsing a redefined
/// function in the same translation unit, such as a second `int main()` in a
/// test file) for `for (...)` headers violating Rule 6-5-1.
void scanInvalidFunctionForLoops(
    const FunctionDecl* fd,
    const SourceManager& sm,
    const LangOptions& langOpts,
    std::function<void(SourceLocation, const std::string&)> reportFn) {
    if (!fd) return;
    SourceLocation startLoc = sm.getSpellingLoc(fd->getLocation());
    if (startLoc.isInvalid()) return;

    std::pair<FileID, unsigned> startDec = sm.getDecomposedLoc(startLoc);
    bool invalid = false;
    llvm::StringRef fileBuf = sm.getBufferData(startDec.first, &invalid);
    if (invalid) return;

    Lexer lexer(sm.getLocForStartOfFile(startDec.first),
                langOpts,
                fileBuf.begin(),
                fileBuf.begin() + startDec.second,
                fileBuf.end());
    lexer.SetCommentRetentionState(false);

    std::vector<Token> tokens;
    Token tok;
    int braceDepth = 0;
    bool seenOpenBrace = false;

    while (!lexer.LexFromRawLexer(tok)) {
        tokens.push_back(tok);
        if (tok.is(tok::l_brace)) {
            ++braceDepth;
            seenOpenBrace = true;
        } else if (tok.is(tok::r_brace)) {
            --braceDepth;
            if (seenOpenBrace && braceDepth == 0) {
                break;
            }
        }
        if (tok.is(tok::eof)) break;
    }

    for (size_t i = 0; i + 2 < tokens.size(); ++i) {
        if (!tokens[i].is(tok::raw_identifier) ||
            tokens[i].getRawIdentifier() != "for") {
            continue;
        }
        if (!tokens[i + 1].is(tok::l_paren)) continue;

        SourceLocation forLoc = tokens[i].getLocation();
        int parenDepth = 0;
        size_t closeParen = i + 1;
        bool hasFloatingCounter = false;
        int semiCount = 0;
        int commaInInitOrInc = 0;

        for (size_t j = i + 1; j < tokens.size(); ++j) {
            if (tokens[j].is(tok::l_paren)) {
                ++parenDepth;
            } else if (tokens[j].is(tok::r_paren)) {
                --parenDepth;
                if (parenDepth == 0) {
                    closeParen = j;
                    break;
                }
            } else if (parenDepth == 1 && tokens[j].is(tok::semi)) {
                ++semiCount;
            } else if (parenDepth == 1 && tokens[j].is(tok::comma) &&
                       (semiCount == 0 || semiCount == 2)) {
                ++commaInInitOrInc;
            } else if (tokens[j].is(tok::raw_identifier)) {
                llvm::StringRef id = tokens[j].getRawIdentifier();
                if (id == "float" || id == "double") {
                    hasFloatingCounter = true;
                }
            }
        }

        if (hasFloatingCounter || commaInInitOrInc > 0) {
            reportFn(forLoc,
                     "For loop shall contain a single loop-counter which shall not have floating type [MISRA-6-5-1]");
        }
        i = closeParen;
    }
}

class ForLoopCounterVisitor : public RecursiveASTVisitor<ForLoopCounterVisitor> {
public:
    ForLoopCounterVisitor(
        ASTContext& ctx,
        const ViolationStore* store,
        std::function<void(SourceLocation, const std::string&)> reportFn)
        : ctx_(ctx), store_(store), reportFn_(std::move(reportFn)) {}

    bool VisitForStmt(ForStmt* forStmt) {
        if (!forStmt) return true;

        const SourceManager& sm = ctx_.getSourceManager();
        SourceLocation loc = forStmt->getForLoc();
        SourceLocation spellingLoc = sm.getSpellingLoc(loc);
        if (spellingLoc.isInvalid() || sm.isInSystemHeader(spellingLoc)) {
            return true;
        }

        PresumedLoc presumed = sm.getPresumedLoc(spellingLoc);
        if (presumed.isInvalid()) return true;
        if (store_ && !store_->getFilter().isProjectFile(presumed.getFilename())) {
            return true;
        }

        if (isForLoopNonCompliant(forStmt)) {
            emitOncePerLine(
                loc,
                "For loop shall contain a single loop-counter which shall not have floating type [MISRA-6-5-1]");
        }
        return true;
    }

    bool VisitFunctionDecl(FunctionDecl* fd) {
        if (!fd || fd->isImplicit()) return true;
        const SourceManager& sm = ctx_.getSourceManager();
        SourceLocation spellingLoc = sm.getSpellingLoc(fd->getLocation());
        if (spellingLoc.isInvalid() || sm.isInSystemHeader(spellingLoc)) {
            return true;
        }

        PresumedLoc presumed = sm.getPresumedLoc(spellingLoc);
        if (presumed.isInvalid()) return true;
        if (store_ && !store_->getFilter().isProjectFile(presumed.getFilename())) {
            return true;
        }

        if (fd->isInvalidDecl() && !fd->doesThisDeclarationHaveABody()) {
            scanInvalidFunctionForLoops(
                fd,
                sm,
                ctx_.getLangOpts(),
                [this](SourceLocation loc, const std::string& msg) {
                    emitOncePerLine(loc, msg);
                });
        }
        return true;
    }

private:
    void emitOncePerLine(SourceLocation loc, const std::string& msg) {
        const SourceManager& sm = ctx_.getSourceManager();
        PresumedLoc p = sm.getPresumedLoc(sm.getSpellingLoc(loc));
        if (p.isInvalid()) return;
        auto key = std::make_pair(std::string(p.getFilename()), p.getLine());
        if (reportedLines_.insert(key).second) {
            reportFn_(loc, msg);
        }
    }

    ASTContext& ctx_;
    const ViolationStore* store_;
    std::function<void(SourceLocation, const std::string&)> reportFn_;
    std::set<std::pair<std::string, unsigned>> reportedLines_;
};

} // namespace

Rule651::Rule651()
    : Rule("MISRA-6-5-1",
           "A for loop shall contain a single loop-counter which shall not have floating type.") {}

void Rule651::registerMatchers(MatchFinder& finder) {
    finder.addMatcher(translationUnitDecl().bind("tu"), this);
}

void Rule651::run(const MatchFinder::MatchResult& result) {
    const auto* tu = result.Nodes.getNodeAs<TranslationUnitDecl>("tu");
    if (!tu) return;

    setContext(result.Context);

    ForLoopCounterVisitor visitor(
        *result.Context,
        store_,
        [this](SourceLocation loc, const std::string& msg) {
            report(loc, msg);
        });
    visitor.TraverseDecl(const_cast<TranslationUnitDecl*>(tu));
}

} // namespace misra

