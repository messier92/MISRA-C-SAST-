#ifndef MISRA_SAFE_AST_VISITOR_H
#define MISRA_SAFE_AST_VISITOR_H

#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/TypeLoc.h"
#include "llvm/ADT/SmallPtrSet.h"

namespace misra {

/// Recursion-bounded and cycle-guarded wrapper around `clang::RecursiveASTVisitor`.
/// Prevents stack exhaustion from deep ASTs or 3-function mutual recursion cycles
/// (`TraverseDecl` -> `Traverse*Decl` -> `TraverseDeclContextHelper` -> `TraverseDecl`,
/// or `TraverseStmt` -> `Traverse*Stmt` -> `TraverseStmt`).
template <typename Derived>
class SafeASTVisitor : public clang::RecursiveASTVisitor<Derived> {
public:
    using Base = clang::RecursiveASTVisitor<Derived>;
    static constexpr unsigned kMaxTraversalDepth = 256;

    bool TraverseDecl(clang::Decl* d) {
        if (!d) return true;
        if (depth_ >= kMaxTraversalDepth) return true;
        if (!activeDecls_.insert(d).second) return true;
        ++depth_;
        bool result = Base::TraverseDecl(d);
        --depth_;
        activeDecls_.erase(d);
        return result;
    }

    bool TraverseStmt(clang::Stmt* s) {
        if (!s) return true;
        if (depth_ >= kMaxTraversalDepth) return true;
        if (!activeStmts_.insert(s).second) return true;
        ++depth_;
        bool result = Base::TraverseStmt(s);
        --depth_;
        activeStmts_.erase(s);
        return result;
    }

    bool TraverseType(clang::QualType t) {
        if (t.isNull()) return true;
        if (depth_ >= kMaxTraversalDepth) return true;
        ++depth_;
        bool result = Base::TraverseType(t);
        --depth_;
        return result;
    }

    bool TraverseTypeLoc(clang::TypeLoc tl) {
        if (!tl) return true;
        if (depth_ >= kMaxTraversalDepth) return true;
        ++depth_;
        bool result = Base::TraverseTypeLoc(tl);
        --depth_;
        return result;
    }

protected:
    unsigned getTraversalDepth() const { return depth_; }

private:
    unsigned depth_ = 0;
    llvm::SmallPtrSet<const clang::Decl*, 32> activeDecls_;
    llvm::SmallPtrSet<const clang::Stmt*, 32> activeStmts_;
};

} // namespace misra

#endif // MISRA_SAFE_AST_VISITOR_H

