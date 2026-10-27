#include "Rule391.h"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/TypeLoc.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace clang::ast_matchers;

namespace misra {

namespace {

bool isStorageOrFuncSpecifier(llvm::StringRef tok) {
    return tok == "extern"       ||
           tok == "static"       ||
           tok == "thread_local" ||
           tok == "mutable"      ||
           tok == "register"     ||
           tok == "inline"       ||
           tok == "virtual"      ||
           tok == "explicit"     ||
           tok == "friend"       ||
           tok == "constexpr";
}

/// Extracts the exact sequence of written type tokens for a variable or
/// parameter declaration `vd` (including `const`/`volatile` qualifiers and
/// typedef names, while excluding storage-class specifiers, the declared
/// identifier token itself, and any initializer).
std::string extractVarTypeTokens(const clang::DeclaratorDecl* dd,
                                 const clang::SourceManager& sm,
                                 const clang::LangOptions& langOpts) {
    if (!dd) return "";

    clang::SourceLocation beginLoc = sm.getSpellingLoc(dd->getBeginLoc());
    clang::SourceLocation idLoc    = sm.getSpellingLoc(dd->getLocation());
    clang::SourceLocation endLoc   = idLoc;

    if (const auto* tsi = dd->getTypeSourceInfo()) {
        clang::SourceLocation tlEnd =
            sm.getSpellingLoc(tsi->getTypeLoc().getEndLoc());
        if (tlEnd.isValid() && sm.isBeforeInTranslationUnit(endLoc, tlEnd)) {
            endLoc = tlEnd;
        }
    }

    if (beginLoc.isInvalid() || endLoc.isInvalid()) return "";

    std::pair<clang::FileID, unsigned> bDec = sm.getDecomposedLoc(beginLoc);
    std::pair<clang::FileID, unsigned> eDec = sm.getDecomposedLoc(endLoc);
    std::pair<clang::FileID, unsigned> idDec = sm.getDecomposedLoc(idLoc);
    if (bDec.first != eDec.first || bDec.second > eDec.second) return "";

    bool invalid = false;
    llvm::StringRef fileBuf = sm.getBufferData(bDec.first, &invalid);
    if (invalid) return "";

    clang::Lexer lexer(sm.getLocForStartOfFile(bDec.first),
                       langOpts,
                       fileBuf.begin(),
                       fileBuf.begin() + bDec.second,
                       fileBuf.end());
    lexer.SetCommentRetentionState(false);

    std::string out;
    clang::Token tok;
    while (!lexer.LexFromRawLexer(tok)) {
        unsigned tokOffset = sm.getFileOffset(tok.getLocation());
        if (tokOffset > eDec.second) break;

        if (tok.isOneOf(clang::tok::equal,
                        clang::tok::l_brace,
                        clang::tok::semi,
                        clang::tok::comma,
                        clang::tok::r_paren)) {
            break;
        }

        if (dd->getIdentifier() && bDec.first == idDec.first &&
            tokOffset == idDec.second) {
            if (tokOffset == eDec.second) break;
            continue;
        }

        std::string spelling = clang::Lexer::getSpelling(tok, sm, langOpts);
        if (!spelling.empty() && !isStorageOrFuncSpecifier(spelling)) {
            if (!out.empty()) out.push_back(' ');
            out += spelling;
        }

        if (tok.is(clang::tok::eof) || tokOffset == eDec.second) break;
    }
    return out;
}

std::string extractReturnTypeTokens(const clang::FunctionDecl* fd,
                                    const clang::SourceManager& sm,
                                    const clang::LangOptions& langOpts) {
    if (!fd) return "";
    clang::SourceRange retRange = fd->getReturnTypeSourceRange();
    if (retRange.isInvalid()) return "";

    clang::SourceLocation beginLoc = sm.getSpellingLoc(retRange.getBegin());
    clang::SourceLocation endLoc   = sm.getSpellingLoc(retRange.getEnd());
    if (beginLoc.isInvalid() || endLoc.isInvalid()) return "";

    std::pair<clang::FileID, unsigned> bDec = sm.getDecomposedLoc(beginLoc);
    std::pair<clang::FileID, unsigned> eDec = sm.getDecomposedLoc(endLoc);
    if (bDec.first != eDec.first || bDec.second > eDec.second) return "";

    bool invalid = false;
    llvm::StringRef fileBuf = sm.getBufferData(bDec.first, &invalid);
    if (invalid) return "";

    clang::Lexer lexer(sm.getLocForStartOfFile(bDec.first),
                       langOpts,
                       fileBuf.begin(),
                       fileBuf.begin() + bDec.second,
                       fileBuf.end());
    lexer.SetCommentRetentionState(false);

    std::string out;
    clang::Token tok;
    while (!lexer.LexFromRawLexer(tok)) {
        unsigned tokOffset = sm.getFileOffset(tok.getLocation());
        if (tokOffset > eDec.second) break;

        std::string spelling = clang::Lexer::getSpelling(tok, sm, langOpts);
        if (!spelling.empty() && !isStorageOrFuncSpecifier(spelling)) {
            if (!out.empty()) out.push_back(' ');
            out += spelling;
        }

        if (tok.is(clang::tok::eof) || tokOffset == eDec.second) break;
    }
    return out;
}

class TypeTokenConsistencyVisitor
    : public clang::RecursiveASTVisitor<TypeTokenConsistencyVisitor> {
public:
    TypeTokenConsistencyVisitor(
        clang::ASTContext& ctx,
        const ViolationStore* store,
        std::function<void(clang::SourceLocation, const std::string&)> reportFn)
        : ctx_(ctx), store_(store), reportFn_(std::move(reportFn)) {}

    bool VisitVarDecl(clang::VarDecl* vd) {
        if (!vd || vd->isImplicit() || llvm::isa<clang::ParmVarDecl>(vd)) {
            return true;
        }

        const auto& sm = ctx_.getSourceManager();
        clang::SourceLocation spellingLoc = sm.getSpellingLoc(vd->getLocation());
        if (spellingLoc.isInvalid() || sm.isInSystemHeader(spellingLoc)) {
            return true;
        }

        clang::PresumedLoc presumed = sm.getPresumedLoc(spellingLoc);
        if (presumed.isInvalid()) return true;
        if (store_ && !store_->getFilter().isProjectFile(presumed.getFilename())) {
            return true;
        }

        std::string typeTokens =
            extractVarTypeTokens(vd, sm, ctx_.getLangOpts());
        if (typeTokens.empty()) return true;

        const clang::Decl* canon = vd->getCanonicalDecl();
        auto it = lastVarTypeTokens_.find(canon);
        if (it != lastVarTypeTokens_.end() && it->second != typeTokens) {
            reportFn_(vd->getLocation(),
                      "Type of object '" + vd->getNameAsString() +
                      "' ('" + typeTokens +
                      "') is not token-for-token identical to previous declaration ('" +
                      it->second + "') [MISRA-3-9-1]");
        }
        lastVarTypeTokens_[canon] = typeTokens;
        return true;
    }

    bool VisitFunctionDecl(clang::FunctionDecl* fd) {
        if (!fd || fd->isImplicit()) return true;
        if (llvm::isa<clang::CXXConstructorDecl>(fd) ||
            llvm::isa<clang::CXXDestructorDecl>(fd)  ||
            llvm::isa<clang::CXXConversionDecl>(fd)  ||
            fd->isOverloadedOperator()) {
            return true;
        }

        const auto& sm = ctx_.getSourceManager();
        clang::SourceLocation spellingLoc = sm.getSpellingLoc(fd->getLocation());
        if (spellingLoc.isInvalid() || sm.isInSystemHeader(spellingLoc)) {
            return true;
        }

        clang::PresumedLoc presumed = sm.getPresumedLoc(spellingLoc);
        if (presumed.isInvalid()) return true;
        if (store_ && !store_->getFilter().isProjectFile(presumed.getFilename())) {
            return true;
        }

        FuncSigTokens currSig;
        currSig.returnType =
            extractReturnTypeTokens(fd, sm, ctx_.getLangOpts());
        for (const auto* param : fd->parameters()) {
            currSig.paramTypes.push_back(
                extractVarTypeTokens(param, sm, ctx_.getLangOpts()));
        }

        const clang::Decl* canon = fd->getCanonicalDecl();
        auto it = lastFuncSigTokens_.find(canon);
        if (it != lastFuncSigTokens_.end()) {
            bool mismatch = (it->second.returnType != currSig.returnType) ||
                            (it->second.paramTypes.size() != currSig.paramTypes.size());
            if (!mismatch) {
                for (size_t i = 0; i < currSig.paramTypes.size(); ++i) {
                    if (it->second.paramTypes[i] != currSig.paramTypes[i]) {
                        mismatch = true;
                        break;
                    }
                }
            }
            if (mismatch) {
                reportFn_(fd->getLocation(),
                          "Return or parameter types of function '" +
                          fd->getNameAsString() +
                          "' are not token-for-token identical to previous declaration [MISRA-3-9-1]");
            }
        }
        lastFuncSigTokens_[canon] = std::move(currSig);
        return true;
    }

private:
    struct FuncSigTokens {
        std::string returnType;
        std::vector<std::string> paramTypes;
    };

    clang::ASTContext& ctx_;
    const ViolationStore* store_;
    std::function<void(clang::SourceLocation, const std::string&)> reportFn_;
    std::unordered_map<const clang::Decl*, std::string> lastVarTypeTokens_;
    std::unordered_map<const clang::Decl*, FuncSigTokens> lastFuncSigTokens_;
};

} // namespace

Rule391::Rule391()
    : Rule("MISRA-3-9-1",
           "The types used for an object, a function return type, or a function parameter shall be token-for-token identical in all declarations and re-declarations.") {}

void Rule391::registerMatchers(MatchFinder& finder) {
    finder.addMatcher(translationUnitDecl().bind("tu"), this);
}

void Rule391::run(const MatchFinder::MatchResult& result) {
    const auto* tu = result.Nodes.getNodeAs<clang::TranslationUnitDecl>("tu");
    if (!tu) return;

    setContext(result.Context);

    TypeTokenConsistencyVisitor visitor(
        *result.Context,
        store_,
        [this](clang::SourceLocation loc, const std::string& msg) {
            report(loc, msg);
        });
    visitor.TraverseDecl(const_cast<clang::TranslationUnitDecl*>(tu));
}

} // namespace misra

