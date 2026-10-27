#include "Rule322.h"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"

#include <functional>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace clang::ast_matchers;

namespace misra {

namespace {

/// Collects names of types, functions, and objects that are actually used
/// in the translation unit so unused code (uninstantiated templates, uncalled
/// functions, unused types) is not reported by Rule 3-2-2.
class UsedEntityCollector : public clang::RecursiveASTVisitor<UsedEntityCollector> {
public:
    explicit UsedEntityCollector(const clang::SourceManager& sm) : sm_(sm) {}

    void markTypeUsed(clang::QualType qt) {
        if (qt.isNull()) return;
        qt = qt.getCanonicalType();
        while (qt->isPointerType() || qt->isReferenceType() || qt->isArrayType()) {
            if (qt->isPointerType() || qt->isReferenceType()) {
                qt = qt->getPointeeType();
            } else if (const auto* at = qt->getAsArrayTypeUnsafe()) {
                qt = at->getElementType();
            } else {
                break;
            }
        }
        if (const auto* tag = qt->getAsTagDecl()) {
            if (tag->getIdentifier()) {
                usedTagNames_.insert(tag->getName().str());
                usedTagNames_.insert(tag->getQualifiedNameAsString());
            }
        }
    }

    bool VisitFunctionDecl(clang::FunctionDecl* fd) {
        if (!fd || fd->isImplicit()) return true;
        if (fd->getBeginLoc().isInvalid() || sm_.isInSystemHeader(fd->getBeginLoc())) {
            return true;
        }
        if (fd->getDescribedFunctionTemplate() != nullptr) {
            return true;
        }
        markTypeUsed(fd->getReturnType());
        for (const auto* param : fd->parameters()) {
            if (param) markTypeUsed(param->getType());
        }
        if (fd->isMain()) {
            usedFunctionNames_.insert(fd->getQualifiedNameAsString());
        }
        return true;
    }

    bool VisitVarDecl(clang::VarDecl* vd) {
        if (!vd || vd->isImplicit()) return true;
        if (vd->getBeginLoc().isInvalid() || sm_.isInSystemHeader(vd->getBeginLoc())) {
            return true;
        }
        markTypeUsed(vd->getType());
        return true;
    }

    bool VisitCallExpr(clang::CallExpr* call) {
        if (const auto* callee = call->getDirectCallee()) {
            usedFunctionNames_.insert(callee->getQualifiedNameAsString());
        }
        return true;
    }

    bool VisitDeclRefExpr(clang::DeclRefExpr* dre) {
        if (!dre) return true;
        if (const auto* fd = llvm::dyn_cast<clang::FunctionDecl>(dre->getDecl())) {
            usedFunctionNames_.insert(fd->getQualifiedNameAsString());
        } else if (const auto* vd = llvm::dyn_cast<clang::VarDecl>(dre->getDecl())) {
            usedObjectNames_.insert(vd->getQualifiedNameAsString());
            markTypeUsed(vd->getType());
        }
        return true;
    }

    const clang::SourceManager& sm_;
    std::unordered_set<std::string> usedTagNames_;
    std::unordered_set<std::string> usedFunctionNames_;
    std::unordered_set<std::string> usedObjectNames_;
};

std::string lexRangeTokens(clang::SourceLocation beginLoc,
                           clang::SourceLocation endLoc,
                           const clang::SourceManager& sm,
                           const clang::LangOptions& langOpts) {
    clang::SourceLocation b = sm.getSpellingLoc(beginLoc);
    clang::SourceLocation e = sm.getSpellingLoc(endLoc);
    if (b.isInvalid() || e.isInvalid()) return "";

    std::pair<clang::FileID, unsigned> bDec = sm.getDecomposedLoc(b);
    std::pair<clang::FileID, unsigned> eDec = sm.getDecomposedLoc(e);
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
        if (!spelling.empty()) {
            if (!out.empty()) out.push_back(' ');
            out += spelling;
        }
        if (tok.is(clang::tok::eof) || tokOffset == eDec.second) break;
    }
    return out;
}

/// Scans a project file buffer for `struct`/`class`/`union`/`enum` definitions
/// so that even when Clang's parser recovers from a same-TU redefinition error,
/// both definitions' token sequences and closing-brace locations are captured.
void scanFileTagDefinitions(
    clang::FileID fid,
    const clang::SourceManager& sm,
    const clang::LangOptions& langOpts,
    const std::unordered_set<std::string>& usedTagNames,
    std::unordered_map<std::string, std::string>& firstTagDefTokens,
    std::function<void(clang::SourceLocation, const std::string&)> reportFn) {
    if (fid.isInvalid()) return;
    bool invalid = false;
    llvm::StringRef fileBuf = sm.getBufferData(fid, &invalid);
    if (invalid) return;

    clang::Lexer lexer(sm.getLocForStartOfFile(fid),
                       langOpts,
                       fileBuf.begin(),
                       fileBuf.begin(),
                       fileBuf.end());
    lexer.SetCommentRetentionState(false);

    std::vector<clang::Token> tokens;
    clang::Token tok;
    while (!lexer.LexFromRawLexer(tok)) {
        tokens.push_back(tok);
        if (tok.is(clang::tok::eof)) break;
    }

    for (size_t i = 0; i + 2 < tokens.size(); ++i) {
        if (!tokens[i].is(clang::tok::raw_identifier)) continue;
        llvm::StringRef kw = tokens[i].getRawIdentifier();
        if (kw != "struct" && kw != "class" && kw != "union" && kw != "enum") {
            continue;
        }

        size_t nameIdx = i + 1;
        if (kw == "enum" && nameIdx + 1 < tokens.size() &&
            tokens[nameIdx].is(clang::tok::raw_identifier) &&
            (tokens[nameIdx].getRawIdentifier() == "class" ||
             tokens[nameIdx].getRawIdentifier() == "struct")) {
            ++nameIdx;
        }

        if (nameIdx + 1 >= tokens.size() ||
            !tokens[nameIdx].is(clang::tok::raw_identifier)) {
            continue;
        }

        std::string tagName = tokens[nameIdx].getRawIdentifier().str();
        size_t braceIdx = nameIdx + 1;
        if (!tokens[braceIdx].is(clang::tok::l_brace)) {
            continue;
        }

        // Find matching closing brace.
        int depth = 0;
        size_t endIdx = braceIdx;
        std::string defTokens = kw.str() + " " + tagName;
        for (size_t j = braceIdx; j < tokens.size(); ++j) {
            if (tokens[j].is(clang::tok::l_brace)) {
                ++depth;
            } else if (tokens[j].is(clang::tok::r_brace)) {
                --depth;
            }
            std::string sp = clang::Lexer::getSpelling(tokens[j], sm, langOpts);
            if (!sp.empty()) {
                defTokens += " " + sp;
            }
            if (depth == 0) {
                endIdx = j;
                break;
            }
        }

        if (depth == 0 && usedTagNames.count(tagName) > 0) {
            auto it = firstTagDefTokens.find(tagName);
            if (it == firstTagDefTokens.end()) {
                firstTagDefTokens.emplace(tagName, defTokens);
            } else if (it->second != defTokens) {
                reportFn(tokens[endIdx].getLocation(),
                         "Multiple definitions of '" + tagName +
                         "' differ by at least one token [MISRA-3-2-2]");
            }
            i = endIdx;
        }
    }
}

class ODRDefinitionVisitor : public clang::RecursiveASTVisitor<ODRDefinitionVisitor> {
public:
    ODRDefinitionVisitor(
        clang::ASTContext& ctx,
        const ViolationStore* store,
        const UsedEntityCollector& used,
        std::unordered_map<std::string, std::string>& funcDefs,
        std::unordered_map<std::string, std::string>& objDefs,
        std::function<void(clang::SourceLocation, const std::string&)> reportFn)
        : ctx_(ctx),
          store_(store),
          used_(used),
          funcDefs_(funcDefs),
          objDefs_(objDefs),
          reportFn_(std::move(reportFn)) {}

    bool VisitFunctionDecl(clang::FunctionDecl* fd) {
        if (!fd || fd->isImplicit() || !fd->doesThisDeclarationHaveABody()) {
            return true;
        }
        if (fd->getDescribedFunctionTemplate() != nullptr) {
            return true;
        }

        const auto& sm = ctx_.getSourceManager();
        clang::SourceLocation spellingLoc = sm.getSpellingLoc(fd->getLocation());
        if (spellingLoc.isInvalid() || sm.isInSystemHeader(spellingLoc)) return true;

        clang::PresumedLoc presumed = sm.getPresumedLoc(spellingLoc);
        if (presumed.isInvalid()) return true;
        if (store_ && !store_->getFilter().isProjectFile(presumed.getFilename())) {
            return true;
        }

        std::string qname = fd->getQualifiedNameAsString();
        if (used_.usedFunctionNames_.count(qname) == 0) {
            return true;
        }

        std::string defTokens = lexRangeTokens(
            fd->getBeginLoc(), fd->getEndLoc(), sm, ctx_.getLangOpts());
        if (defTokens.empty()) return true;

        auto it = funcDefs_.find(qname);
        if (it == funcDefs_.end()) {
            funcDefs_.emplace(qname, defTokens);
        } else if (it->second != defTokens) {
            reportFn_(fd->getLocation(),
                      "Multiple definitions of function '" + qname +
                      "' differ by at least one token [MISRA-3-2-2]");
        }
        return true;
    }

    bool VisitVarDecl(clang::VarDecl* vd) {
        if (!vd || vd->isImplicit() || !vd->isFileVarDecl()) return true;
        if (vd->isThisDeclarationADefinition() == clang::VarDecl::DeclarationOnly) {
            return true;
        }
        if (vd->getDescribedVarTemplate() != nullptr ||
            vd->getTemplateSpecializationKind() != clang::TSK_Undeclared) {
            return true;
        }

        const auto& sm = ctx_.getSourceManager();
        clang::SourceLocation spellingLoc = sm.getSpellingLoc(vd->getLocation());
        if (spellingLoc.isInvalid() || sm.isInSystemHeader(spellingLoc)) return true;

        clang::PresumedLoc presumed = sm.getPresumedLoc(spellingLoc);
        if (presumed.isInvalid()) return true;
        if (store_ && !store_->getFilter().isProjectFile(presumed.getFilename())) {
            return true;
        }

        std::string qname = vd->getQualifiedNameAsString();
        if (used_.usedObjectNames_.count(qname) == 0) {
            return true;
        }

        std::string defTokens = lexRangeTokens(
            vd->getBeginLoc(), vd->getEndLoc(), sm, ctx_.getLangOpts());
        if (defTokens.empty()) return true;

        auto it = objDefs_.find(qname);
        if (it == objDefs_.end()) {
            objDefs_.emplace(qname, defTokens);
        } else if (it->second != defTokens) {
            reportFn_(vd->getLocation(),
                      "Multiple definitions of object '" + qname +
                      "' differ by at least one token [MISRA-3-2-2]");
        }
        return true;
    }

private:
    clang::ASTContext& ctx_;
    const ViolationStore* store_;
    const UsedEntityCollector& used_;
    std::unordered_map<std::string, std::string>& funcDefs_;
    std::unordered_map<std::string, std::string>& objDefs_;
    std::function<void(clang::SourceLocation, const std::string&)> reportFn_;
};

} // namespace

Rule322::Rule322()
    : Rule("MISRA-3-2-2",
           "The One Definition Rule shall not be violated.") {}

void Rule322::registerMatchers(MatchFinder& finder) {
    finder.addMatcher(translationUnitDecl().bind("tu"), this);
}

void Rule322::run(const MatchFinder::MatchResult& result) {
    const auto* tu = result.Nodes.getNodeAs<clang::TranslationUnitDecl>("tu");
    if (!tu) return;

    setContext(result.Context);
    const auto& sm = result.Context->getSourceManager();

    UsedEntityCollector used(sm);
    used.TraverseDecl(const_cast<clang::TranslationUnitDecl*>(tu));

    // Collect project FileIDs referenced by top-level declarations in inclusion order.
    std::vector<clang::FileID> projectFiles;
    std::set<clang::FileID> seenFiles;
    for (const auto* d : tu->decls()) {
        if (!d || d->isImplicit()) continue;
        clang::SourceLocation sloc = sm.getSpellingLoc(d->getLocation());
        if (sloc.isInvalid() || sm.isInSystemHeader(sloc)) continue;
        clang::PresumedLoc presumed = sm.getPresumedLoc(sloc);
        if (presumed.isInvalid()) continue;
        if (store_ && !store_->getFilter().isProjectFile(presumed.getFilename())) {
            continue;
        }
        clang::FileID fid = sm.getFileID(sloc);
        if (fid.isValid() && fid != sm.getMainFileID() && seenFiles.insert(fid).second) {
            projectFiles.push_back(fid);
        }
    }
    clang::FileID mainFid = sm.getMainFileID();
    if (mainFid.isValid() && seenFiles.insert(mainFid).second) {
        projectFiles.push_back(mainFid);
    }

    std::unordered_map<std::string, std::string> firstTagDefTokens;
    for (clang::FileID fid : projectFiles) {
        scanFileTagDefinitions(
            fid,
            sm,
            result.Context->getLangOpts(),
            used.usedTagNames_,
            firstTagDefTokens,
            [this](clang::SourceLocation loc, const std::string& msg) {
                report(loc, msg);
            });
    }

    std::unordered_map<std::string, std::string> funcDefs;
    std::unordered_map<std::string, std::string> objDefs;
    ODRDefinitionVisitor visitor(
        *result.Context,
        store_,
        used,
        funcDefs,
        objDefs,
        [this](clang::SourceLocation loc, const std::string& msg) {
            report(loc, msg);
        });
    visitor.TraverseDecl(const_cast<clang::TranslationUnitDecl*>(tu));
}

} // namespace misra

