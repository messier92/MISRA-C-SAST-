#include "Rule.h"

#include "clang/AST/Stmt.h"
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

void Rule::report(clang::SourceLocation loc, const std::string& message) {
    if (!store_ || !ctx_) return;

    const auto& sm = ctx_->getSourceManager();
    // Resolve to the spelling location (handles macros).
    clang::SourceLocation spellingLoc = sm.getSpellingLoc(loc);
    if (spellingLoc.isInvalid()) return;

    clang::PresumedLoc presumed = sm.getPresumedLoc(spellingLoc);
    if (presumed.isInvalid()) return;

    Violation v;
    v.file       = presumed.getFilename();
    v.line       = presumed.getLine();
    v.col        = presumed.getColumn();
    v.ruleId     = ruleId_;
    v.message    = message;
    v.sourceLine = getSourceLine(spellingLoc);

    store_->addViolation(v);
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
    while (cur) {
        const clang::Stmt* parent = parentBuilder.getParent(cur);
        if (!parent) break;

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
