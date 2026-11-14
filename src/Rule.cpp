#include "Rule.h"
#include "ProjectFileFilter.h"

#include "clang/AST/Stmt.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"

#include "llvm/ADT/StringRef.h"

#include <sstream>

namespace misra {

// ── ParentBuilder ──────────────────────────────────────────────

void ParentBuilder::build(clang::ASTContext& ctx) {
    parentMap_.clear();
    stack_.clear();
    TraverseAST(ctx);
}

void ParentBuilder::buildStmt(clang::Stmt* root) {
    parentMap_.clear();
    stack_.clear();
    if (root) {
        TraverseStmt(root);
    }
}

bool ParentBuilder::VisitStmt(clang::Stmt* s) {
    // Record parent relationship for every direct child.
    for (auto* child : s->children()) {
        if (child) {
            parentMap_[child] = s;
        }
    }
    return true; // Continue traversal.
}

const clang::Stmt* ParentBuilder::getParent(const clang::Stmt* child) const {
    auto it = parentMap_.find(child);
    return it != parentMap_.end() ? it->second : nullptr;
}

// ── Rule ───────────────────────────────────────────────────────

Rule::Rule(const std::string& ruleId, const std::string& description)
    : ruleId_(ruleId), description_(description) {}

std::string Rule::getCanonicalFilePath(clang::SourceLocation loc,
                                       const clang::SourceManager& sm) {
    clang::SourceLocation spellingLoc = sm.getSpellingLoc(loc);
    if (spellingLoc.isInvalid()) return "";

    clang::FileID fid = sm.getFileID(spellingLoc);
    if (fid.isValid()) {
        if (auto fe = sm.getFileEntryRefForID(fid)) {
            llvm::StringRef canonName =
                sm.getFileManager().getCanonicalName(*fe);
            if (!canonName.empty()) {
                return ProjectFileFilter::canonicalisePath(canonName.str());
            }
        }
    }

    clang::PresumedLoc presumed = sm.getPresumedLoc(spellingLoc);
    if (presumed.isInvalid()) return "";
    return ProjectFileFilter::canonicalisePath(presumed.getFilename());
}

bool Rule::isProjectLocation(clang::SourceLocation loc,
                             const clang::SourceManager& sm,
                             const ViolationStore* store) {
    if (loc.isInvalid()) return false;

    clang::SourceLocation spellingLoc = sm.getSpellingLoc(loc);
    if (spellingLoc.isInvalid()) return false;

    clang::SourceLocation expansionLoc = sm.getExpansionLoc(loc);
    if (sm.isInSystemHeader(spellingLoc) ||
        (expansionLoc.isValid() && sm.isInSystemHeader(expansionLoc)) ||
        sm.isInSystemMacro(loc) ||
        sm.isWrittenInBuiltinFile(spellingLoc) ||
        sm.isWrittenInCommandLineFile(spellingLoc) ||
        sm.isWrittenInScratchSpace(spellingLoc)) {
        return false;
    }

    std::string canonFile = getCanonicalFilePath(spellingLoc, sm);
    if (canonFile.empty()) return false;

    if (store) {
        return store->getFilter().isProjectFile(canonFile);
    }
    return !ProjectFileFilter::isExcludedExternalOrThirdPartyPath(canonFile);
}

bool Rule::isProjectLocation(clang::SourceLocation loc) const {
    if (!ctx_) return false;
    return isProjectLocation(loc, ctx_->getSourceManager(), store_);
}

void Rule::report(clang::SourceLocation loc, const std::string& message) {
    if (!store_ || !ctx_) return;

    const auto& sm = ctx_->getSourceManager();
    if (!isProjectLocation(loc, sm, store_)) return;

    // Resolve to the spelling location (handles macros).
    clang::SourceLocation spellingLoc = sm.getSpellingLoc(loc);
    if (spellingLoc.isInvalid()) return;

    clang::PresumedLoc presumed = sm.getPresumedLoc(spellingLoc);
    if (presumed.isInvalid()) return;

    std::string canonFile = getCanonicalFilePath(spellingLoc, sm);
    if (canonFile.empty()) return;

    Violation v;
    v.file       = std::move(canonFile);
    v.line       = presumed.getLine();
    v.col        = presumed.getColumn();
    v.ruleId     = ruleId_;
    v.message    = message;
    v.sourceLine = getSourceLine(spellingLoc);

    store_->addViolation(v);
}

void Rule::reportExcluded(clang::SourceLocation loc,
                          const std::string& message,
                          const std::string& justification) {
    if (!store_ || !ctx_) return;

    const auto& sm = ctx_->getSourceManager();
    if (!isProjectLocation(loc, sm, store_)) return;

    clang::SourceLocation spellingLoc = sm.getSpellingLoc(loc);
    if (spellingLoc.isInvalid()) return;

    clang::PresumedLoc presumed = sm.getPresumedLoc(spellingLoc);
    if (presumed.isInvalid()) return;

    std::string canonFile = getCanonicalFilePath(spellingLoc, sm);
    if (canonFile.empty()) return;

    Violation v;
    v.file       = std::move(canonFile);
    v.line       = presumed.getLine();
    v.col        = presumed.getColumn();
    v.ruleId     = ruleId_;
    v.message    = message;
    v.sourceLine = getSourceLine(spellingLoc);

    store_->addExcludedViolation(v, justification);
}

std::string Rule::getSourceLine(clang::SourceLocation loc) const {
    if (!ctx_) return "";
    const auto& sm = ctx_->getSourceManager();

    // Get the start of the line.
    clang::SourceLocation lineStart = sm.translateLineCol(
        sm.getFileID(loc),
        sm.getSpellingLineNumber(loc),
        1);

    if (lineStart.isInvalid()) return "";

    bool invalid = false;
    llvm::StringRef lineData = clang::Lexer::getSourceText(
        clang::CharSourceRange::getCharRange(
            lineStart,
            clang::Lexer::getLocForEndOfToken(loc, 0, sm, ctx_->getLangOpts())),
        sm, ctx_->getLangOpts(), &invalid);

    if (invalid) return "";

    // Find the full line by reading from the buffer.
    const char* bufStart = sm.getCharacterData(lineStart, &invalid);
    if (invalid || !bufStart) return "";

    // Read until newline or end of buffer.
    const char* p = bufStart;
    while (*p && *p != '\n' && *p != '\r') ++p;

    std::string line(bufStart, p);
    // Trim leading whitespace.
    size_t start = line.find_first_not_of(" \t");
    if (start != std::string::npos) {
        line = line.substr(start);
    }
    return line;
}

bool Rule::isReachable(const clang::Stmt* stmt,
                       clang::ASTContext& ctx,
                       ParentBuilder& parentBuilder) {
    const clang::Stmt* cur = stmt;
    unsigned hops = 0;
    while (cur && ++hops <= 256) {
        const clang::Stmt* parent = parentBuilder.getParent(cur);
        if (!parent || parent == cur) break;

        if (const auto* compound = llvm::dyn_cast<clang::CompoundStmt>(parent)) {
            const clang::Stmt* grandParent = parentBuilder.getParent(compound);
            bool inSwitch = grandParent && llvm::isa<clang::SwitchStmt>(grandParent);
            bool seenTerminator = inSwitch;

            for (const auto* child : compound->body()) {
                if (llvm::isa<clang::SwitchCase>(child) ||
                    llvm::isa<clang::LabelStmt>(child)) {
                    seenTerminator = false;
                }
                if (child == cur) {
                    if (seenTerminator) return false;
                    break;
                }
                const clang::Stmt* sub = child;
                while (sub) {
                    if (const auto* sc = llvm::dyn_cast<clang::SwitchCase>(sub)) {
                        sub = sc->getSubStmt();
                    } else if (const auto* ls = llvm::dyn_cast<clang::LabelStmt>(sub)) {
                        sub = ls->getSubStmt();
                    } else {
                        break;
                    }
                }
                if (sub) {
                    if (const auto* expr = llvm::dyn_cast<clang::Expr>(sub)) {
                        sub = expr->IgnoreImplicit();
                    }
                    if (llvm::isa<clang::ReturnStmt>(sub)  ||
                        llvm::isa<clang::BreakStmt>(sub)    ||
                        llvm::isa<clang::ContinueStmt>(sub) ||
                        llvm::isa<clang::CXXThrowExpr>(sub) ||
                        llvm::isa<clang::GotoStmt>(sub)) {
                        seenTerminator = true;
                    }
                }
            }
        } else if (const auto* ifStmt = llvm::dyn_cast<clang::IfStmt>(parent)) {
            const clang::Expr* cond = ifStmt->getCond();
            bool condVal = false;
            if (cond && !cond->isValueDependent() &&
                cond->EvaluateAsBooleanCondition(condVal, ctx)) {
                if (!condVal && cur == ifStmt->getThen()) return false;
                if (condVal && cur == ifStmt->getElse()) return false;
            }
        } else if (const auto* whileStmt = llvm::dyn_cast<clang::WhileStmt>(parent)) {
            const clang::Expr* cond = whileStmt->getCond();
            bool condVal = false;
            if (cond && !cond->isValueDependent() &&
                cond->EvaluateAsBooleanCondition(condVal, ctx)) {
                if (!condVal && cur == whileStmt->getBody()) return false;
            }
        } else if (const auto* forStmt = llvm::dyn_cast<clang::ForStmt>(parent)) {
            const clang::Expr* cond = forStmt->getCond();
            bool condVal = false;
            if (cond && !cond->isValueDependent() &&
                cond->EvaluateAsBooleanCondition(condVal, ctx)) {
                if (!condVal && (cur == forStmt->getBody() || cur == forStmt->getInc())) {
                    return false;
                }
            }
        }

        cur = parent;
    }
    return true;
}

} // namespace misra
