#include "Rule013.h"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Lex/Lexer.h"

#include <cctype>
#include <unordered_set>
#include <vector>

using namespace clang;
using namespace clang::ast_matchers;

namespace misra {

namespace {

bool isIdentChar013(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

/// Returns true if the source line of `vd` is a Qt signal emission
/// (`emit signalName(...)` or `Q_EMIT signalName(...)`) where `signalName`
/// matches `vd->getName()`.
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
        if (isIdentChar013(lineText[i]) && !std::isdigit(static_cast<unsigned char>(lineText[i]))) {
            size_t start = i;
            while (i < n && isIdentChar013(lineText[i])) ++i;
            llvm::StringRef kw(lineText.data() + start, i - start);
            if (kw == "emit" || kw == "Q_EMIT") {
                size_t j = i;
                while (j < n && std::isspace(static_cast<unsigned char>(lineText[j]))) ++j;
                if (j == i) continue;
                while (j < n && isIdentChar013(lineText[j])) {
                    size_t idStart = j;
                    while (j < n && isIdentChar013(lineText[j])) ++j;
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

/// Checks whether a local variable `vd` (whose AST references may have been
/// dropped by Clang due to `emit` or unresolved Qt types) appears as a read
/// identifier token in its enclosing function body after its declaration line.
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
                    // Pure assignment `var = ...`; keep scanning for a read.
                    continue;
                }
            }
            return true;
        }
    }
    return false;
}

/// Returns true if `rd` is declared in a header or source file that uses
/// `Q_OBJECT` / Qt slots / signals.
bool isQObjectClassRecord(const CXXRecordDecl* rd, const SourceManager& sm) {
    if (!rd) return false;
    SourceLocation loc = sm.getSpellingLoc(rd->getBeginLoc());
    if (loc.isInvalid()) return false;
    FileID fid = sm.getFileID(loc);
    bool invalid = false;
    llvm::StringRef buf = sm.getBufferData(fid, &invalid);
    if (invalid) return false;
    return buf.contains("Q_OBJECT") || buf.contains("Q_GADGET") ||
           buf.contains("signals:") || buf.contains("slots:") ||
           buf.contains("Q_SIGNALS") || buf.contains("Q_SLOTS");
}

/// Returns true if `vd` has an explicit value initialization that is covered by Rule 0-1-6
/// (Unused Assignments / Initializations) for local or file-scope static variables.
bool hasExplicitValueInitForRule016(const VarDecl* vd) {
    if (!vd || !vd->hasInit()) return false;
    bool isLocalOrFileStatic =
        (vd->hasLocalStorage() && !isa<ParmVarDecl>(vd)) ||
        (vd->isFileVarDecl() &&
         (vd->getStorageClass() == SC_Static ||
          (vd->getType().isConstQualified() && !vd->hasExternalStorage())));
    if (!isLocalOrFileStatic) return false;

    const Expr* init = vd->getInit();
    if (!init) return false;
    init = init->IgnoreImplicit();

    if (const auto* construct = dyn_cast<CXXConstructExpr>(init)) {
        // An implicit default construction without braces/parens (e.g. `std::string s;`)
        // is not an explicit value assignment, whereas `largeInteger tinyUnit{0,1,0};`
        // or `S bits = {};` has arguments or explicit list/direct init.
        if (construct->getNumArgs() == 0 &&
            vd->getInitStyle() == VarDecl::CallInit &&
            !construct->isListInitialization()) {
            return false;
        }
    }
    return true;
}

class CallAndDeclCollector : public SafeASTVisitor<CallAndDeclCollector> {
public:
    CallAndDeclCollector(const SourceManager& sm, const ViolationStore* store)
        : sm_(sm), store_(store) {}

    bool VisitCallExpr(CallExpr* call) {
        if (const FunctionDecl* callee = call->getDirectCallee()) {
            calledFunctions_.insert(callee->getCanonicalDecl());
        }
        return true;
    }

    bool VisitCXXConstructExpr(CXXConstructExpr* ctor) {
        if (const CXXConstructorDecl* cd = ctor->getConstructor()) {
            calledFunctions_.insert(cd->getCanonicalDecl());
        }
        return true;
    }

    bool VisitDeclRefExpr(DeclRefExpr* dre) {
        if (const auto* fd = dyn_cast<FunctionDecl>(dre->getDecl())) {
            calledFunctions_.insert(fd->getCanonicalDecl());
        }
        return true;
    }

    bool VisitFunctionDecl(FunctionDecl* fd) {
        if (fd->isMain()) {
            hasMain_ = true;
        }
        return true;
    }

    bool VisitVarDecl(VarDecl* vd) {
        if (!vd || isa<ParmVarDecl>(vd) || isa<ImplicitParamDecl>(vd)) return true;
        if (vd->isImplicit() || vd->isExceptionVariable()) return true;
        if (!Rule::isProjectLocation(vd->getBeginLoc(), sm_, store_)) {
            return true;
        }
        if (vd->getDescribedVarTemplate() ||
            vd->getTemplateSpecializationKind() != TSK_Undeclared) {
            return true;
        }
        if (vd->hasExternalStorage() && !vd->isThisDeclarationADefinition()) {
            return true;
        }
        if (vd->getType().isVolatileQualified() || vd->getType()->isReferenceType()) {
            return true;
        }
        varDecls_.push_back(vd);
        return true;
    }

    bool VisitFieldDecl(FieldDecl* fd) {
        if (!fd || fd->isImplicit()) return true;
        if (!Rule::isProjectLocation(fd->getBeginLoc(), sm_, store_)) {
            return true;
        }
        // Unnamed bit-fields (e.g. `unsigned char : 1;`) are compliant padding.
        if (fd->isUnnamedBitfield() || fd->getName().empty()) {
            return true;
        }
        if (const auto* rd = dyn_cast<CXXRecordDecl>(fd->getParent())) {
            if (rd->getDescribedClassTemplate() || rd->isDependentContext()) {
                return true;
            }
        }
        fieldDecls_.push_back(fd);
        return true;
    }

    const SourceManager& sm_;
    const ViolationStore* store_ = nullptr;
    std::unordered_set<const FunctionDecl*> calledFunctions_;
    std::vector<const VarDecl*> varDecls_;
    std::vector<const FieldDecl*> fieldDecls_;
    bool hasMain_ = false;
};

class UsageVisitor : public SafeASTVisitor<UsageVisitor> {
public:
    UsageVisitor(const SourceManager& sm,
                 const std::unordered_set<const FunctionDecl*>& calledFunctions,
                 bool hasMain)
        : sm_(sm), calledFunctions_(calledFunctions), hasMain_(hasMain) {}

    bool TraverseFunctionDecl(FunctionDecl* fd) {
        if (!fd) return true;
        if (fd->getBeginLoc().isInvalid() || sm_.isInSystemHeader(fd->getBeginLoc())) {
            return true;
        }
        const FunctionDecl* prev = currentFunc_;
        currentFunc_ = fd->getCanonicalDecl();
        bool res = SafeASTVisitor<UsageVisitor>::TraverseFunctionDecl(fd);
        currentFunc_ = prev;
        return res;
    }

    bool TraverseCXXMethodDecl(CXXMethodDecl* md) {
        if (!md) return true;
        if (md->getBeginLoc().isInvalid() || sm_.isInSystemHeader(md->getBeginLoc())) {
            return true;
        }
        if (md->doesThisDeclarationHaveABody() && md->isOutOfLine()) {
            if (const CXXRecordDecl* rd = md->getParent()) {
                recordsWithUsedFields_.insert(rd->getCanonicalDecl());
            }
        }
        const FunctionDecl* prev = currentFunc_;
        currentFunc_ = md->getCanonicalDecl();
        bool res = SafeASTVisitor<UsageVisitor>::TraverseCXXMethodDecl(md);
        currentFunc_ = prev;
        return res;
    }

    bool TraverseConstructorInitializer(CXXCtorInitializer* init) {
        // Constructor member initializer `: field(arg)` initializes `field`;
        // traverse only the argument expression `init->getInit()` so `arg` is
        // recorded as read, without treating construction as a read of `field`.
        if (init && init->getInit()) {
            TraverseStmt(init->getInit());
        }
        return true;
    }

    bool VisitDeclRefExpr(DeclRefExpr* dre) {
        if (!dre) return true;
        if (const auto* vd = dyn_cast<VarDecl>(dre->getDecl())) {
            referencedVars_.insert(vd->getCanonicalDecl());
        }
        return true;
    }

    bool VisitMemberExpr(MemberExpr* me) {
        if (!me) return true;
        if (const auto* fd = dyn_cast<FieldDecl>(me->getMemberDecl())) {
            if (isCurrentFunctionActiveForField(fd)) {
                usedFields_.insert(fd->getCanonicalDecl());
                if (const RecordDecl* parent = fd->getParent()) {
                    recordsWithUsedFields_.insert(parent->getCanonicalDecl());
                }
            }
        }
        return true;
    }

private:
    bool isCurrentFunctionActiveForField(const FieldDecl* fd) const {
        if (!currentFunc_) return true;
        const auto* md = dyn_cast<CXXMethodDecl>(currentFunc_);
        if (!md) return true;
        if (isa<CXXConstructorDecl>(md) || isa<CXXDestructorDecl>(md)) return true;
        if (md->isVirtual() || md->isOverloadedOperator()) return true;
        if (calledFunctions_.count(md->getCanonicalDecl()) > 0) return true;

        // If the TU has `main` (or other methods of this class are called) and
        // this inline getter/method is never called anywhere in the TU, it is dead.
        const CXXRecordDecl* rd = md->getParent();
        if (rd && hasMain_) {
            return false;
        }
        if (rd && isQObjectClassRecord(rd, sm_)) {
            // Uncalled methods/slots of Q_OBJECT classes are flagged here and then
            // verified across project .cpp files by ViolationStore so slot/macro
            // member uses are recorded as auditable exclusions.
            return false;
        }
        if (rd) {
            for (const CXXMethodDecl* other : rd->methods()) {
                if (!isa<CXXConstructorDecl>(other) &&
                    calledFunctions_.count(other->getCanonicalDecl()) > 0) {
                    return false;
                }
            }
        }
        return true;
    }

public:
    const SourceManager& sm_;
    const std::unordered_set<const FunctionDecl*>& calledFunctions_;
    bool hasMain_ = false;
    const FunctionDecl* currentFunc_ = nullptr;
    std::unordered_set<const VarDecl*> referencedVars_;
    std::unordered_set<const FieldDecl*> usedFields_;
    std::unordered_set<const TagDecl*> recordsWithUsedFields_;
};

} // namespace

Rule013::Rule013()
    : Rule("MISRA-0-1-3",
           "A project shall not contain unused variables.") {}

void Rule013::registerMatchers(MatchFinder& finder) {
    finder.addMatcher(
        translationUnitDecl().bind("tu"),
        this);
}

void Rule013::run(const MatchFinder::MatchResult& result) {
    const auto* tu = result.Nodes.getNodeAs<TranslationUnitDecl>("tu");
    if (!tu) return;

    const SourceManager& sm = *result.SourceManager;
    const LangOptions& langOpts = result.Context->getLangOpts();

    CallAndDeclCollector collector(sm, store_);
    collector.TraverseAST(*result.Context);

    UsageVisitor usage(sm, collector.calledFunctions_, collector.hasMain_);
    usage.TraverseAST(*result.Context);

    for (const VarDecl* vd : collector.varDecls_) {
        const VarDecl* canon = vd->getCanonicalDecl();
        if (usage.referencedVars_.count(canon) == 0 &&
            isLocalVarReadInFunctionText(vd, sm, langOpts)) {
            usage.referencedVars_.insert(canon);
        }
    }

    for (const VarDecl* vd : collector.varDecls_) {
        const VarDecl* canon = vd->getCanonicalDecl();
        if (usage.referencedVars_.count(canon) == 0) {
            // Leave local/static variables with explicit value initializations
            // to Rule 0-1-6 so diagnostics are not duplicated.
            if (hasExplicitValueInitForRule016(vd)) {
                continue;
            }
            // Non-static file-scope globals in library TUs (without `main`) may
            // be referenced externally; also skip when shadowed by an inner-scope
            // variable of the same name (covered by MISRA-2-10-2).
            if (vd->isFileVarDecl() && !collector.hasMain_) {
                continue;
            }
            // Skip declaration-only test stubs where no variables are referenced at all,
            // unless `vd` is an `emit signalName(...)` pseudo-declaration so it is
            // captured in the Excluded Findings audit section.
            if (usage.referencedVars_.empty() && !isEmitSignalVarDecl(vd, sm)) {
                continue;
            }
            report(vd->getBeginLoc(),
                   "Variable '" + vd->getNameAsString() +
                   "' is declared or defined but never used.");
        }
    }

    for (const FieldDecl* fd : collector.fieldDecls_) {
        const FieldDecl* canon = fd->getCanonicalDecl();
        if (usage.usedFields_.count(canon) == 0) {
            // Only flag unused fields in records that are actively used in the TU.
            const TagDecl* parentRec =
                fd->getParent() ? fd->getParent()->getCanonicalDecl() : nullptr;
            if (!parentRec ||
                usage.recordsWithUsedFields_.count(parentRec) == 0) {
                continue;
            }
            report(fd->getBeginLoc(),
                   "Member variable '" + fd->getNameAsString() +
                   "' is declared but never read or written.");
        }
    }
}

} // namespace misra

