#include "Rule017.h"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"

#include "llvm/ADT/StringRef.h"

#include <cctype>
#include <unordered_set>
#include <vector>

using namespace clang;
using namespace clang::ast_matchers;

namespace misra {

namespace {

AST_MATCHER(clang::Expr, isRecoveryExpr) {
    return llvm::isa<clang::RecoveryExpr>(Node);
}

bool isExcludedStdFunc(const FunctionDecl* callee) {
    if (!callee || !callee->getIdentifier()) return false;
    llvm::StringRef name = callee->getName();
    return name == "memcpy"  ||
           name == "memset"  ||
           name == "memmove" ||
           name == "strcpy"  ||
           name == "strncpy" ||
           name == "strcat"  ||
           name == "strncat";
}

bool isTransparentWrapper(const Stmt* s) {
    return isa<ExprWithCleanups>(s)        ||
           isa<MaterializeTemporaryExpr>(s) ||
           isa<CXXBindTemporaryExpr>(s)     ||
           isa<ImplicitCastExpr>(s)         ||
           isa<ParenExpr>(s);
}

bool isDiscardedInStmt(const Stmt* parent, const Stmt* child) {
    if (!parent || !child) return false;
    if (isa<CompoundStmt>(parent)) return true;
    if (const auto* ifStmt = dyn_cast<IfStmt>(parent)) {
        return child == ifStmt->getThen() || child == ifStmt->getElse();
    }
    if (const auto* whileStmt = dyn_cast<WhileStmt>(parent)) {
        return child == whileStmt->getBody();
    }
    if (const auto* doStmt = dyn_cast<DoStmt>(parent)) {
        return child == doStmt->getBody();
    }
    if (const auto* forStmt = dyn_cast<ForStmt>(parent)) {
        return child == forStmt->getInit() ||
               child == forStmt->getInc()  ||
               child == forStmt->getBody();
    }
    if (const auto* rangeStmt = dyn_cast<CXXForRangeStmt>(parent)) {
        return child == rangeStmt->getBody();
    }
    if (const auto* switchStmt = dyn_cast<SwitchStmt>(parent)) {
        return child == switchStmt->getBody();
    }
    if (const auto* sc = dyn_cast<SwitchCase>(parent)) {
        return child == sc->getSubStmt();
    }
    if (const auto* ls = dyn_cast<LabelStmt>(parent)) {
        return child == ls->getSubStmt();
    }
    return false;
}

bool isInImplicitOrInstantiatedFunction(const Stmt* s, ASTContext& ctx) {
    DynTypedNodeList parents = ctx.getParents(*s);
    unsigned hops = 0;
    while (!parents.empty() && ++hops <= 256) {
        const DynTypedNode& node = parents[0];
        if (const auto* fd = node.get<FunctionDecl>()) {
            return fd->isImplicit() || fd->isTemplateInstantiation();
        }
        parents = ctx.getParents(node);
    }
    return false;
}

} // namespace

Rule017::Rule017()
    : Rule("MISRA-0-1-7",
           "The value returned by a function having a non-void return type "
           "that is not an overloaded operator shall always be used.") {}

void Rule017::registerMatchers(MatchFinder& finder) {
    finder.addMatcher(
        callExpr(unless(cxxOperatorCallExpr()),
                 unless(userDefinedLiteral())).bind("call"),
        this);
    finder.addMatcher(
        expr(isRecoveryExpr()).bind("recovery"),
        this);
    finder.addMatcher(
        translationUnitDecl().bind("tu"),
        this);
}

void Rule017::run(const MatchFinder::MatchResult& result) {
    ASTContext& ctx = *result.Context;
    const SourceManager& sm = *result.SourceManager;

    if (const auto* tu = result.Nodes.getNodeAs<TranslationUnitDecl>("tu")) {
        (void)tu;
        class InvalidCalleeCollector : public SafeASTVisitor<InvalidCalleeCollector> {
        public:
            InvalidCalleeCollector(const SourceManager& sm, const ViolationStore* store)
                : sm_(sm), store_(store) {}

            bool VisitFunctionDecl(FunctionDecl* fd) {
                if (!fd || fd->isImplicit() || !fd->getIdentifier()) return true;
                if (isa<CXXConstructorDecl>(fd) || isa<CXXDestructorDecl>(fd) ||
                    isa<CXXConversionDecl>(fd) || fd->isOverloadedOperator() ||
                    isExcludedStdFunc(fd)) {
                    return true;
                }
                if (fd->hasBody() && Rule::isProjectLocation(fd->getBeginLoc(), sm_, store_)) {
                    funcBodies_.push_back(fd);
                }
                if (fd->isInvalidDecl()) {
                    QualType rt = fd->getReturnType();
                    if (!rt.isNull() && !rt->isVoidType() && !rt->isDependentType()) {
                        invalidNonVoidNames_.insert(fd->getName().str());
                    }
                }
                return true;
            }

            const SourceManager& sm_;
            const ViolationStore* store_;
            std::unordered_set<std::string> invalidNonVoidNames_;
            std::vector<const FunctionDecl*> funcBodies_;
        };

        InvalidCalleeCollector collector(sm, store_);
        collector.TraverseAST(ctx);

        if (!collector.invalidNonVoidNames_.empty()) {
            for (const FunctionDecl* fn : collector.funcBodies_) {
                const auto* body = dyn_cast_or_null<CompoundStmt>(fn->getBody());
                if (!body) continue;
                SourceLocation lbrac = sm.getSpellingLoc(body->getLBracLoc());
                SourceLocation rbrac = sm.getSpellingLoc(body->getRBracLoc());
                if (lbrac.isInvalid() || rbrac.isInvalid()) continue;
                FileID fid = sm.getFileID(lbrac);
                if (fid != sm.getFileID(rbrac)) continue;

                unsigned startLine = sm.getSpellingLineNumber(lbrac);
                unsigned endLine   = sm.getSpellingLineNumber(rbrac);
                for (unsigned ln = startLine; ln <= endLine; ++ln) {
                    SourceLocation lineLoc = sm.translateLineCol(fid, ln, 1);
                    if (lineLoc.isInvalid()) continue;
                    bool invalid = false;
                    const char* buf = sm.getCharacterData(lineLoc, &invalid);
                    if (invalid || !buf) continue;
                    const char* p = buf;
                    while (*p && *p != '\n' && *p != '\r') ++p;
                    std::string lineStr(buf, p);

                    // Strip trailing '//' comment.
                    size_t commentPos = lineStr.find("//");
                    if (commentPos != std::string::npos) {
                        lineStr = lineStr.substr(0, commentPos);
                    }
                    size_t firstNonSpace = lineStr.find_first_not_of(" \t");
                    if (firstNonSpace == std::string::npos) continue;
                    size_t lastNonSpace = lineStr.find_last_not_of(" \t");
                    std::string trimmed = lineStr.substr(
                        firstNonSpace, lastNonSpace - firstNonSpace + 1);

                    // Check if `trimmed` is a standalone call `<name>(...);` or `this-><name>(...);`.
                    if (trimmed.size() < 4 || trimmed.substr(trimmed.size() - 2) != ");") {
                        continue;
                    }
                    size_t pos = 0;
                    if (trimmed.rfind("this->", 0) == 0) {
                        pos = 6;
                        while (pos < trimmed.size() &&
                               std::isspace(static_cast<unsigned char>(trimmed[pos]))) {
                            ++pos;
                        }
                    }
                    size_t idStart = pos;
                    while (pos < trimmed.size() &&
                           (std::isalnum(static_cast<unsigned char>(trimmed[pos])) ||
                            trimmed[pos] == '_')) {
                        ++pos;
                    }
                    if (pos == idStart) continue;
                    std::string calleeName = trimmed.substr(idStart, pos - idStart);
                    while (pos < trimmed.size() &&
                           std::isspace(static_cast<unsigned char>(trimmed[pos]))) {
                        ++pos;
                    }
                    if (pos < trimmed.size() && trimmed[pos] == '(' &&
                        collector.invalidNonVoidNames_.count(calleeName) > 0) {
                        SourceLocation callLoc = sm.translateLineCol(
                            fid, ln, static_cast<unsigned>(firstNonSpace + 1));
                        report(callLoc.isValid() ? callLoc : lineLoc,
                               "Return value of non-void function call is ignored.");
                    }
                }
            }
        }
        return;
    }

    const Expr* targetExpr = nullptr;
    SourceLocation loc;

    if (const auto* call = result.Nodes.getNodeAs<CallExpr>("call")) {
        loc = call->getBeginLoc();
        if (loc.isInvalid() || sm.isInSystemHeader(sm.getSpellingLoc(loc))) return;
        if (call->isTypeDependent() || call->isValueDependent()) return;

        QualType retType = call->getCallReturnType(ctx);
        if (retType.isNull() || retType->isVoidType() || retType->isDependentType()) {
            return;
        }
        if (const FunctionDecl* callee = call->getDirectCallee()) {
            if (isExcludedStdFunc(callee)) return;
        }
        targetExpr = call;
    } else if (const auto* rec = result.Nodes.getNodeAs<RecoveryExpr>("recovery")) {
        loc = rec->getBeginLoc();
        if (loc.isInvalid() || sm.isInSystemHeader(sm.getSpellingLoc(loc))) return;
        if (rec->subExpressions().empty()) return;

        const Expr* calleeSub = rec->subExpressions()[0]->IgnoreParenImpCasts();
        const FunctionDecl* callee = nullptr;
        if (const auto* dre = dyn_cast<DeclRefExpr>(calleeSub)) {
            callee = dyn_cast<FunctionDecl>(dre->getDecl());
        } else if (const auto* me = dyn_cast<MemberExpr>(calleeSub)) {
            callee = dyn_cast<FunctionDecl>(me->getMemberDecl());
        } else if (const auto* oe = dyn_cast<OverloadExpr>(calleeSub)) {
            for (const NamedDecl* nd : oe->decls()) {
                if (const auto* fd = dyn_cast<FunctionDecl>(nd)) {
                    callee = fd;
                    break;
                }
            }
        }
        if (!callee || callee->isOverloadedOperator() || isExcludedStdFunc(callee)) {
            return;
        }
        QualType retType = callee->getReturnType();
        if (retType.isNull() || retType->isVoidType() || retType->isDependentType()) {
            return;
        }
        targetExpr = rec;
    } else {
        return;
    }

    if (isInImplicitOrInstantiatedFunction(targetExpr, ctx)) return;

    const Stmt* cur = targetExpr;
    unsigned hops = 0;
    while (cur && ++hops <= 256) {
        DynTypedNodeList parents = ctx.getParents(*cur);
        if (parents.empty()) break;

        const Stmt* parentStmt = parents[0].get<Stmt>();
        if (!parentStmt) break;

        if (const auto* explicitCast = dyn_cast<ExplicitCastExpr>(parentStmt)) {
            if (explicitCast->getType()->isVoidType()) {
                return; // Explicitly cast to void: compliant.
            }
        }

        if (isTransparentWrapper(parentStmt)) {
            cur = parentStmt;
            continue;
        }

        if (const auto* bo = dyn_cast<BinaryOperator>(parentStmt)) {
            if (bo->getOpcode() == BO_Comma && cur == bo->getLHS()) {
                report(loc, "Return value of non-void function call is ignored.");
                return;
            }
        }

        if (isDiscardedInStmt(parentStmt, cur)) {
            report(loc, "Return value of non-void function call is ignored.");
            return;
        }

        break;
    }
}

} // namespace misra

