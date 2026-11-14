#include "Rule2101.h"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"

#include <cctype>
#include <string>
#include <unordered_map>

using namespace clang::ast_matchers;

namespace misra {

namespace {

bool isIdentChar2101(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

bool isEmitPseudoVarDecl(const clang::VarDecl* vd, const clang::SourceManager& sm) {
    if (!vd || !vd->isInvalidDecl() || vd->getName().empty()) return false;
    clang::SourceLocation spellingLoc = sm.getSpellingLoc(vd->getLocation());
    if (spellingLoc.isInvalid()) return false;
    clang::FileID fid = sm.getFileID(spellingLoc);
    unsigned lineNo = sm.getSpellingLineNumber(spellingLoc);
    clang::SourceLocation lineStart = sm.translateLineCol(fid, lineNo, 1);
    if (lineStart.isInvalid()) return false;
    bool invalid = false;
    const char* buf = sm.getCharacterData(lineStart, &invalid);
    if (invalid || !buf) return false;
    const char* p = buf;
    while (*p && *p != '\n' && *p != '\r') ++p;
    std::string lineText(buf, p);

    size_t n = lineText.size();
    for (size_t i = 0; i < n;) {
        if (isIdentChar2101(lineText[i]) && !std::isdigit(static_cast<unsigned char>(lineText[i]))) {
            size_t start = i;
            while (i < n && isIdentChar2101(lineText[i])) ++i;
            llvm::StringRef kw(lineText.data() + start, i - start);
            if (kw == "emit" || kw == "Q_EMIT") {
                size_t j = i;
                while (j < n && std::isspace(static_cast<unsigned char>(lineText[j]))) ++j;
                if (j == i) continue;
                while (j < n && isIdentChar2101(lineText[j])) {
                    size_t idStart = j;
                    while (j < n && isIdentChar2101(lineText[j])) ++j;
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

bool isTrackedDecl(const clang::NamedDecl* d,
                   clang::ASTContext& ctx,
                   const ViolationStore* store) {
    if (!d || d->isImplicit() || !d->getIdentifier()) return false;

    if (const auto* fn = llvm::dyn_cast<clang::FunctionDecl>(d)) {
        if (llvm::isa<clang::CXXConstructorDecl>(fn) ||
            llvm::isa<clang::CXXDestructorDecl>(fn)  ||
            llvm::isa<clang::CXXConversionDecl>(fn)  ||
            fn->isOverloadedOperator()) {
            return false;
        }
    }

    if (!(llvm::isa<clang::VarDecl>(d)          ||
          llvm::isa<clang::FunctionDecl>(d)     ||
          llvm::isa<clang::FieldDecl>(d)        ||
          llvm::isa<clang::TypedefNameDecl>(d)  ||
          llvm::isa<clang::EnumConstantDecl>(d) ||
          llvm::isa<clang::NamespaceDecl>(d))) {
        return false;
    }

    const auto& sm = ctx.getSourceManager();
    if (const auto* vd = llvm::dyn_cast<clang::VarDecl>(d)) {
        if (isEmitPseudoVarDecl(vd, sm)) return false;
    }
    clang::SourceLocation spellingLoc = sm.getSpellingLoc(d->getLocation());
    if (spellingLoc.isInvalid() || sm.isInSystemHeader(spellingLoc)) return false;

    clang::PresumedLoc presumed = sm.getPresumedLoc(spellingLoc);
    if (presumed.isInvalid()) return false;
    if (store && !store->getFilter().isProjectFile(presumed.getFilename())) {
        return false;
    }
    return true;
}

/// Normalize an identifier into its canonical typographic skeleton:
///   - Remove all '_' characters.
///   - Fold case to lowercase.
///   - Replace "rn" with "m".
///   - Map visually ambiguous characters to a single representative:
///       'O', 'o', '0'       -> '0'
///       'I', 'i', 'l', '1'  -> '1'
///       'Z', 'z', '2'       -> '2'
///       'S', 's', '5'       -> '5'
///       'B', 'b', '8'       -> '8'
std::string normalizeIdentifier(llvm::StringRef name) {
    std::string stripped;
    stripped.reserve(name.size());
    for (char c : name) {
        if (c != '_') {
            stripped.push_back(
                static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }

    // Replace "rn" with "m".
    std::string rnCollapsed;
    rnCollapsed.reserve(stripped.size());
    for (size_t i = 0; i < stripped.size(); ++i) {
        if (i + 1 < stripped.size() && stripped[i] == 'r' && stripped[i + 1] == 'n') {
            rnCollapsed.push_back('m');
            ++i;
        } else {
            rnCollapsed.push_back(stripped[i]);
        }
    }

    // Canonicalize ambiguous characters.
    for (char& c : rnCollapsed) {
        switch (c) {
            case 'o': c = '0'; break;
            case 'i':
            case 'l': c = '1'; break;
            case 'z': c = '2'; break;
            case 's': c = '5'; break;
            case 'b': c = '8'; break;
            default: break;
        }
    }

    return rnCollapsed;
}

class TypographicVisitor : public SafeASTVisitor<TypographicVisitor> {
public:
    TypographicVisitor(clang::ASTContext& ctx,
                       const ViolationStore* store,
                       std::function<void(clang::SourceLocation, const std::string&)> reportFn)
        : ctx_(ctx), store_(store), reportFn_(std::move(reportFn)) {}

    bool VisitNamedDecl(clang::NamedDecl* d) {
        if (!isTrackedDecl(d, ctx_, store_)) return true;

        std::string rawName = d->getName().str();
        if (rawName.empty()) return true;

        std::string norm = normalizeIdentifier(rawName);
        auto it = seenByNormalized_.find(norm);
        if (it == seenByNormalized_.end()) {
            seenByNormalized_.emplace(norm, rawName);
        } else if (it->second != rawName) {
            reportFn_(d->getLocation(),
                      "Identifier '" + rawName +
                      "' is typographically ambiguous with '" + it->second +
                      "' [MISRA-2-10-1]");
        }
        return true;
    }

private:
    clang::ASTContext& ctx_;
    const ViolationStore* store_;
    std::function<void(clang::SourceLocation, const std::string&)> reportFn_;
    std::unordered_map<std::string, std::string> seenByNormalized_;
};

} // namespace

Rule2101::Rule2101()
    : Rule("MISRA-2-10-1",
           "Different identifiers shall be typographically unambiguous.") {}

void Rule2101::registerMatchers(MatchFinder& finder) {
    finder.addMatcher(translationUnitDecl().bind("tu"), this);
}

void Rule2101::run(const MatchFinder::MatchResult& result) {
    const auto* tu = result.Nodes.getNodeAs<clang::TranslationUnitDecl>("tu");
    if (!tu) return;

    setContext(result.Context);

    TypographicVisitor visitor(
        *result.Context,
        store_,
        [this](clang::SourceLocation loc, const std::string& msg) {
            report(loc, msg);
        });
    visitor.TraverseAST(*result.Context);
}

} // namespace misra

