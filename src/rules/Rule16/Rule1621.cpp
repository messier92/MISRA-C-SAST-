#include "Rule1621.h"
#include "PreprocessorTracker.h"

#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/StringRef.h"

#include <set>
#include <string>
#include <vector>

using namespace clang;
using namespace clang::ast_matchers;

namespace misra {

namespace {

struct DirectiveInfo {
    std::string kind;      // e.g. "ifndef", "define", "ifdef", "if", "endif"
    std::string argIdent;  // macro name following #ifndef or #define
    SourceLocation loc;    // location of `#`
    int condDepthBefore = 0;
    bool precededByCode = false;
    bool hasExtraDefineTokens = false;
};

bool hasHeaderExtension(llvm::StringRef filename) {
    llvm::StringRef lower = filename;
    return lower.ends_with_insensitive(".h") ||
           lower.ends_with_insensitive(".hpp") ||
           lower.ends_with_insensitive(".hh") ||
           lower.ends_with_insensitive(".hxx");
}

std::string getTokenSpelling(const Token& tok) {
    if (tok.is(tok::raw_identifier)) {
        return tok.getRawIdentifier().str();
    }
    if (const auto* ii = tok.getIdentifierInfo()) {
        return ii->getName().str();
    }
    return "";
}

std::vector<DirectiveInfo> parseFileDirectives(FileID fid,
                                               const SourceManager& sm,
                                               const LangOptions& langOpts) {
    std::vector<DirectiveInfo> directives;
    bool invalid = false;
    llvm::StringRef buf = sm.getBufferData(fid, &invalid);
    if (invalid) return directives;

    SourceLocation fileStart = sm.getLocForStartOfFile(fid);
    Lexer lexer(fileStart, langOpts, buf.begin(), buf.begin(), buf.end());
    lexer.SetCommentRetentionState(false);

    std::vector<Token> tokens;
    Token tok;
    while (!lexer.LexFromRawLexer(tok)) {
        if (tok.is(tok::eof)) break;
        tokens.push_back(tok);
    }

    bool seenOrdinaryCode = false;
    int condDepth = 0;
    size_t i = 0;
    while (i < tokens.size()) {
        if (tokens[i].is(tok::hash) && tokens[i].isAtStartOfLine()) {
            SourceLocation hashLoc = tokens[i].getLocation();
            ++i; // consume '#'

            if (i < tokens.size() && !tokens[i].isAtStartOfLine()) {
                DirectiveInfo info;
                info.kind = getTokenSpelling(tokens[i]);
                info.loc = hashLoc;
                info.condDepthBefore = condDepth;
                info.precededByCode = seenOrdinaryCode;
                ++i; // consume directive name

                if (info.kind == "ifndef" || info.kind == "define" ||
                    info.kind == "ifdef") {
                    if (i < tokens.size() && !tokens[i].isAtStartOfLine()) {
                        info.argIdent = getTokenSpelling(tokens[i]);
                        ++i;
                    }
                    if (info.kind == "define" &&
                        i < tokens.size() && !tokens[i].isAtStartOfLine()) {
                        info.hasExtraDefineTokens = true;
                    }
                }

                // Consume remaining tokens on the directive line.
                while (i < tokens.size() && !tokens[i].isAtStartOfLine()) {
                    ++i;
                }

                if (info.kind == "ifndef" || info.kind == "ifdef" ||
                    info.kind == "if") {
                    ++condDepth;
                } else if (info.kind == "endif") {
                    if (condDepth > 0) --condDepth;
                }

                directives.push_back(info);
            }
            continue;
        }

        seenOrdinaryCode = true;
        ++i;
    }

    return directives;
}

} // namespace

Rule1621::Rule1621()
    : Rule("MISRA-16-2-1",
           "The preprocessor shall only be used for file inclusion and include guards.") {}

void Rule1621::registerMatchers(MatchFinder& finder) {
    finder.addMatcher(translationUnitDecl().bind("tu"), this);
}

void Rule1621::run(const MatchFinder::MatchResult& result) {
    const auto* tu = result.Nodes.getNodeAs<TranslationUnitDecl>("tu");
    if (!tu || !result.Context || !result.SourceManager) return;

    setContext(result.Context);
    const SourceManager& sm = *result.SourceManager;
    const auto& ppData = PreprocessorTUData::instance();

    std::set<FileID> filesToCheck = ppData.visitedFiles;
    FileID mainFid = sm.getMainFileID();
    if (mainFid.isValid()) {
        filesToCheck.insert(mainFid);
    }
    for (const Decl* d : tu->decls()) {
        if (!d) continue;
        SourceLocation spell = sm.getSpellingLoc(d->getLocation());
        if (spell.isInvalid() || sm.isInSystemHeader(spell)) continue;
        FileID fid = sm.getFileID(spell);
        if (fid.isValid()) {
            filesToCheck.insert(fid);
        }
    }

    for (FileID fid : filesToCheck) {
        SourceLocation fileStart = sm.getLocForStartOfFile(fid);
        if (fileStart.isInvalid() || sm.isInSystemHeader(fileStart)) continue;

        PresumedLoc presumed = sm.getPresumedLoc(fileStart);
        if (presumed.isInvalid()) continue;
        std::string filename = presumed.getFilename();
        if (store_ && !store_->getFilter().isProjectFile(filename)) {
            continue;
        }

        std::vector<DirectiveInfo> directives =
            parseFileDirectives(fid, sm, result.Context->getLangOpts());
        if (directives.empty()) continue;

        // Check if the file starts with a valid matching include guard:
        //   Directive 0: `#ifndef <IDENT>` at depth 0 before any code
        //   Directive 1: `#define <IDENT>` at depth 1 before any code
        //   Closed by a matching `#endif`
        bool hasValidIncludeGuard = false;
        if (directives.size() >= 3 &&
            directives[0].kind == "ifndef" &&
            !directives[0].argIdent.empty() &&
            !directives[0].precededByCode &&
            directives[0].condDepthBefore == 0 &&
            directives[1].kind == "define" &&
            directives[1].argIdent == directives[0].argIdent &&
            !directives[1].precededByCode &&
            !directives[1].hasExtraDefineTokens &&
            directives[1].condDepthBefore == 1) {
            for (size_t k = 2; k < directives.size(); ++k) {
                if (directives[k].kind == "endif" &&
                    directives[k].condDepthBefore == 1) {
                    hasValidIncludeGuard = true;
                    break;
                }
            }
        }

        bool isHeader = hasHeaderExtension(filename);
        // Inspect header files (.h/.hpp/.hh/.hxx) and test fixtures that begin
        // with an include guard structure (such as tests/Rule16/Rule1621.cpp).
        if (!isHeader && !hasValidIncludeGuard) {
            continue;
        }

        for (size_t idx = 0; idx < directives.size(); ++idx) {
            if (hasValidIncludeGuard && (idx == 0 || idx == 1)) {
                continue;
            }
            const DirectiveInfo& d = directives[idx];
            if (d.kind == "define") {
                report(d.loc,
                       "#define directive used outside of include guard [MISRA-16-2-1]");
            } else if (d.kind == "ifndef") {
                report(d.loc,
                       "#ifndef directive used outside of include guard [MISRA-16-2-1]");
            }
        }
    }
}

} // namespace misra

