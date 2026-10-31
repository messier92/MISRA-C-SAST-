#include "Rule1601.h"
#include "PreprocessorTracker.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"

#include <set>
#include <string>
#include <vector>

using namespace clang;
using namespace clang::ast_matchers;

namespace misra {

namespace {

/// Collects source ranges of `extern "C"` linkage specification blocks.
class ExternCCollector : public RecursiveASTVisitor<ExternCCollector> {
public:
    std::vector<SourceRange> ranges;

    bool VisitLinkageSpecDecl(const LinkageSpecDecl* lsd) {
        if (lsd && lsd->getLanguage() == LinkageSpecLanguageIDs::C &&
            lsd->hasBraces()) {
            ranges.push_back(lsd->getSourceRange());
        }
        return true;
    }
};

/// Check whether `loc` falls inside any of the given `SourceRange`s in `sm`.
bool isLocationInRanges(SourceLocation loc,
                        const std::vector<SourceRange>& ranges,
                        const SourceManager& sm) {
    if (loc.isInvalid()) return false;
    FileID locFid = sm.getFileID(loc);
    unsigned locOff = sm.getFileOffset(loc);

    for (const SourceRange& r : ranges) {
        if (r.isInvalid()) continue;
        SourceLocation b = sm.getSpellingLoc(r.getBegin());
        SourceLocation e = sm.getSpellingLoc(r.getEnd());
        if (b.isInvalid() || e.isInvalid()) continue;
        if (sm.getFileID(b) != locFid || sm.getFileID(e) != locFid) continue;
        unsigned bOff = sm.getFileOffset(b);
        unsigned eOff = sm.getFileOffset(e);
        if (locOff >= bOff && locOff <= eOff) {
            return true;
        }
    }
    return false;
}

std::string getRawIdent(const Token& tok) {
    if (tok.is(tok::raw_identifier)) {
        return tok.getRawIdentifier().str();
    }
    if (const auto* ii = tok.getIdentifierInfo()) {
        return ii->getName().str();
    }
    return "";
}

/// Scan `fid` for `#include` directives that are preceded by active ordinary
/// C++ code tokens in the same file.
std::vector<SourceLocation> findMisplacedIncludes(
    FileID fid,
    const SourceManager& sm,
    const LangOptions& langOpts,
    const std::vector<SourceRange>& skippedRanges,
    const std::vector<SourceRange>& externCRanges,
    const std::vector<IncludeDirectiveRecord>& resolvedIncludes) {
    std::vector<SourceLocation> violations;

    bool invalid = false;
    llvm::StringRef buf = sm.getBufferData(fid, &invalid);
    if (invalid) return violations;

    SourceLocation fileStart = sm.getLocForStartOfFile(fid);
    Lexer lexer(fileStart, langOpts, buf.begin(), buf.begin(), buf.end());
    lexer.SetCommentRetentionState(false);

    std::vector<Token> tokens;
    Token tok;
    while (!lexer.LexFromRawLexer(tok)) {
        if (tok.is(tok::eof)) break;
        tokens.push_back(tok);
    }

    bool seenActiveCode = false;
    size_t i = 0;
    while (i < tokens.size()) {
        if (tokens[i].is(tok::hash) && tokens[i].isAtStartOfLine()) {
            SourceLocation hashLoc = tokens[i].getLocation();
            bool inSkipped = isLocationInRanges(hashLoc, skippedRanges, sm);
            ++i; // consume '#'

            if (i < tokens.size() && !tokens[i].isAtStartOfLine()) {
                std::string dirName = getRawIdent(tokens[i]);
                ++i; // consume directive keyword

                if (!inSkipped && dirName == "include") {
                    bool isAngled = (i < tokens.size() &&
                                     !tokens[i].isAtStartOfLine() &&
                                     tokens[i].is(tok::less));
                    bool isResolved = isAngled;
                    if (!isResolved) {
                        unsigned hashLine = sm.getSpellingLineNumber(hashLoc);
                        for (const auto& inc : resolvedIncludes) {
                            if (sm.getFileID(inc.hashLoc) == fid &&
                                sm.getSpellingLineNumber(inc.hashLoc) == hashLine) {
                                isResolved = true;
                                break;
                            }
                        }
                    }

                    if (seenActiveCode && isResolved &&
                        !isLocationInRanges(hashLoc, externCRanges, sm)) {
                        violations.push_back(hashLoc);
                    }
                }
            }

            while (i < tokens.size() && !tokens[i].isAtStartOfLine()) {
                ++i;
            }
            continue;
        }

        SourceLocation tokLoc = tokens[i].getLocation();
        if (!isLocationInRanges(tokLoc, skippedRanges, sm)) {
            seenActiveCode = true;
        }
        ++i;
    }

    return violations;
}

} // namespace

Rule1601::Rule1601()
    : Rule("MISRA-16-0-1",
           "#include directives in a file shall only be preceded by other preprocessor directives or comments.") {}

void Rule1601::registerMatchers(MatchFinder& finder) {
    finder.addMatcher(translationUnitDecl().bind("tu"), this);
}

void Rule1601::run(const MatchFinder::MatchResult& result) {
    const auto* tu = result.Nodes.getNodeAs<TranslationUnitDecl>("tu");
    if (!tu || !result.Context || !result.SourceManager) return;

    setContext(result.Context);
    const SourceManager& sm = *result.SourceManager;
    const auto& ppData = PreprocessorTUData::instance();

    // Detect if this translation unit concatenates multiple `int main()`
    // definitions (as in tests/Rule6/Rule651.cpp).
    unsigned mainCount = 0;
    for (const Decl* d : tu->decls()) {
        if (const auto* fd = llvm::dyn_cast<FunctionDecl>(d)) {
            if (fd->getIdentifier() && fd->getName() == "main" &&
                fd->doesThisDeclarationHaveABody()) {
                ++mainCount;
            }
        }
    }
    if (mainCount > 1) return;

    ExternCCollector externC;
    externC.TraverseDecl(const_cast<TranslationUnitDecl*>(tu));

    std::set<FileID> filesToCheck = ppData.visitedFiles;
    FileID mainFid = sm.getMainFileID();
    if (mainFid.isValid()) {
        filesToCheck.insert(mainFid);
    }

    for (FileID fid : filesToCheck) {
        SourceLocation fileStart = sm.getLocForStartOfFile(fid);
        if (fileStart.isInvalid() || sm.isInSystemHeader(fileStart)) continue;

        PresumedLoc presumed = sm.getPresumedLoc(fileStart);
        if (presumed.isInvalid()) continue;
        if (store_ && !store_->getFilter().isProjectFile(presumed.getFilename())) {
            continue;
        }

        for (SourceLocation loc : findMisplacedIncludes(
                 fid, sm, result.Context->getLangOpts(),
                 ppData.skippedRanges, externC.ranges, ppData.includes)) {
            report(loc,
                   "#include directive shall only be preceded by other preprocessor directives or comments [MISRA-16-0-1]");
        }
    }
}

} // namespace misra
