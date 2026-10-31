#include "Rule1537.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/StmtCXX.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"

#include <set>
#include <vector>

using namespace clang;
using namespace clang::ast_matchers;

namespace misra {

namespace {

/// Token-stream parser for `try-catch` statements and function-try-blocks.
/// Because C++ compilers (including Clang's frontend parser) reject `catch`
/// clauses that follow `catch (...)` with `error: catch-all handler must come last`
/// and omit the trailing handler from the `CXXTryStmt` AST node, this helper
/// inspects the Clang lexer token stream for each translation unit file to
/// accurately detect handlers following `catch (...)` while respecting nested
/// `try-catch` scopes.
class TryCatchTokenScanner {
public:
    TryCatchTokenScanner(const SourceManager& sm, const LangOptions& langOpts,
                         FileID fid)
        : sm_(sm), langOpts_(langOpts) {
        bool invalid = false;
        llvm::StringRef buf = sm.getBufferData(fid, &invalid);
        if (invalid) return;

        SourceLocation fileStart = sm.getLocForStartOfFile(fid);
        Lexer lexer(fileStart, langOpts, buf.begin(), buf.begin(), buf.end());
        lexer.SetCommentRetentionState(false);

        Token tok;
        while (!lexer.LexFromRawLexer(tok)) {
            if (tok.is(tok::eof)) break;
            tokens_.push_back(tok);
        }
    }

    std::vector<SourceLocation> findViolations() {
        std::vector<SourceLocation> violations;
        size_t idx = 0;
        while (idx < tokens_.size()) {
            if (isTry(tokens_[idx])) {
                parseTryBlock(idx, violations);
            } else {
                ++idx;
            }
        }
        return violations;
    }

private:
    static bool isTry(const Token& tok) {
        return tok.is(tok::kw_try) ||
               (tok.is(tok::raw_identifier) && tok.getRawIdentifier() == "try");
    }

    static bool isCatch(const Token& tok) {
        return tok.is(tok::kw_catch) ||
               (tok.is(tok::raw_identifier) && tok.getRawIdentifier() == "catch");
    }

    /// Parse a balanced `{ ... }` compound statement starting at `tokens_[idx]`
    /// (where `tokens_[idx]` is `{`), recursively scanning any nested `try`
    /// blocks inside the compound statement.
    void parseCompoundBlock(size_t& idx, std::vector<SourceLocation>& violations) {
        if (idx >= tokens_.size() || !tokens_[idx].is(tok::l_brace)) return;
        ++idx; // consume '{'
        int depth = 1;
        while (idx < tokens_.size() && depth > 0) {
            if (isTry(tokens_[idx])) {
                parseTryBlock(idx, violations);
                continue;
            }
            if (tokens_[idx].is(tok::l_brace)) {
                ++depth;
            } else if (tokens_[idx].is(tok::r_brace)) {
                --depth;
            }
            ++idx;
        }
    }

    /// Parse a `try` or function-try-block starting at `tokens_[idx] == kw_try`.
    void parseTryBlock(size_t& idx, std::vector<SourceLocation>& violations) {
        ++idx; // consume 'try'

        // For constructor function-try-blocks (`C() try : a(1) { ... }`),
        // advance to the opening `{` of the try body.
        while (idx < tokens_.size() && !tokens_[idx].is(tok::l_brace)) {
            ++idx;
        }
        if (idx >= tokens_.size()) return;

        // Parse the `{ ... }` body of the `try` statement (recursively handling
        // any nested `try-catch` statements inside it).
        parseCompoundBlock(idx, violations);

        // Now parse all consecutive `catch (...) { ... }` handlers belonging to
        // this `try` statement.
        bool seenCatchAll = false;
        while (idx < tokens_.size() && isCatch(tokens_[idx])) {
            SourceLocation catchLoc = tokens_[idx].getLocation();
            ++idx; // consume 'catch'

            bool isCatchAll = false;
            if (idx < tokens_.size() && tokens_[idx].is(tok::l_paren)) {
                ++idx; // consume '('
                if (idx < tokens_.size() && tokens_[idx].is(tok::ellipsis)) {
                    isCatchAll = true;
                }
                int parenDepth = 1;
                while (idx < tokens_.size() && parenDepth > 0) {
                    if (tokens_[idx].is(tok::l_paren)) ++parenDepth;
                    else if (tokens_[idx].is(tok::r_paren)) --parenDepth;
                    ++idx;
                }
            }

            if (seenCatchAll) {
                violations.push_back(catchLoc);
            }
            if (isCatchAll) {
                seenCatchAll = true;
            }

            if (idx < tokens_.size() && tokens_[idx].is(tok::l_brace)) {
                parseCompoundBlock(idx, violations);
            }
        }
    }

    const SourceManager& sm_;
    const LangOptions& langOpts_;
    std::vector<Token> tokens_;
};

} // namespace

Rule1537::Rule1537()
    : Rule("MISRA-15-3-7",
           "Where multiple handlers are provided in a single try-catch statement or function-try-block, any ellipsis (catch-all) handler shall occur last.") {}

void Rule1537::registerMatchers(MatchFinder& finder) {
    finder.addMatcher(cxxTryStmt().bind("tryStmt"), this);
    finder.addMatcher(translationUnitDecl().bind("tu"), this);
}

void Rule1537::run(const MatchFinder::MatchResult& result) {
    if (!result.Context || !result.SourceManager) return;
    setContext(result.Context);
    const SourceManager& sm = *result.SourceManager;

    if (const auto* tryStmt = result.Nodes.getNodeAs<CXXTryStmt>("tryStmt")) {
        unsigned numHandlers = tryStmt->getNumHandlers();
        if (numHandlers < 2) return;

        SourceLocation tryLoc = sm.getSpellingLoc(tryStmt->getTryLoc());
        if (tryLoc.isInvalid() || sm.isInSystemHeader(tryLoc)) return;

        PresumedLoc presumed = sm.getPresumedLoc(tryLoc);
        if (presumed.isInvalid()) return;
        if (store_ && !store_->getFilter().isProjectFile(presumed.getFilename())) {
            return;
        }

        bool seenCatchAll = false;
        for (unsigned i = 0; i < numHandlers; ++i) {
            const CXXCatchStmt* handler = tryStmt->getHandler(i);
            if (!handler) continue;

            bool isCatchAll = (handler->getExceptionDecl() == nullptr &&
                               handler->getCaughtType().isNull());
            if (seenCatchAll) {
                report(handler->getCatchLoc(),
                       "Exception handler appears after catch-all '...' handler [MISRA-15-3-7]");
            }
            if (isCatchAll) {
                seenCatchAll = true;
            }
        }
        return;
    }

    if (const auto* tu = result.Nodes.getNodeAs<TranslationUnitDecl>("tu")) {
        std::set<FileID> filesToScan;
        FileID mainFid = sm.getMainFileID();
        if (mainFid.isValid()) {
            filesToScan.insert(mainFid);
        }
        for (const Decl* d : tu->decls()) {
            if (!d) continue;
            SourceLocation spell = sm.getSpellingLoc(d->getLocation());
            if (spell.isInvalid() || sm.isInSystemHeader(spell)) continue;
            FileID fid = sm.getFileID(spell);
            if (fid.isValid()) {
                filesToScan.insert(fid);
            }
        }

        for (FileID fid : filesToScan) {
            SourceLocation fileStart = sm.getLocForStartOfFile(fid);
            if (fileStart.isInvalid() || sm.isInSystemHeader(fileStart)) continue;

            PresumedLoc presumed = sm.getPresumedLoc(fileStart);
            if (presumed.isInvalid()) continue;
            if (store_ && !store_->getFilter().isProjectFile(presumed.getFilename())) {
                continue;
            }

            TryCatchTokenScanner scanner(sm, result.Context->getLangOpts(), fid);
            for (SourceLocation loc : scanner.findViolations()) {
                report(loc,
                       "Exception handler appears after catch-all '...' handler [MISRA-15-3-7]");
            }
        }
    }
}

} // namespace misra
