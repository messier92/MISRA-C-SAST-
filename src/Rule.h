#ifndef MISRA_RULE_H
#define MISRA_RULE_H

#include "GlobalSymbolDatabase.h"
#include "ViolationStore.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

#include <string>
#include <unordered_map>

namespace misra {

// ── ParentBuilder ──────────────────────────────────────────────

/// Builds a stmt-to-parent map for a translation unit.
/// Clang does not provide cheap parent lookup, so rules that need it
/// use this helper to build the map once per TU.
class ParentBuilder : public clang::RecursiveASTVisitor<ParentBuilder> {
public:
    void build(clang::ASTContext& ctx);

    /// Returns the parent Stmt of `child`, or nullptr if not found.
    const clang::Stmt* getParent(const clang::Stmt* child) const;

    // RecursiveASTVisitor callbacks.
    bool VisitStmt(clang::Stmt* s);

private:
    /// Stack of parent statements used during traversal.
    std::vector<clang::Stmt*> stack_;
    std::unordered_map<const clang::Stmt*, const clang::Stmt*> parentMap_;
};

// ── Rule (abstract base class) ─────────────────────────────────

/// Abstract base class for all MISRA rule checkers.
///
/// Lifecycle:
///   1. Construct the rule (stores its rule ID and description).
///   2. `setStore()` / `setDatabase()` inject shared state (called once per TU).
///   3. `registerMatchers(finder)` adds AST matchers to the shared MatchFinder.
///   4. The matcher callbacks call `report()` for file-level violations
///      or record facts into the GlobalSymbolDatabase for project-level checks.
///
/// Subclass contract:
///   - Override `registerMatchers()` and add your matchers there.
///   - Implement one or more `MatchCallback`-derived inner classes
///     that do the actual checking and call `report()`.
class Rule {
public:
    /// @param ruleId      e.g. "MISRA-5-0-4"
    /// @param description e.g. "Implicit integral conversions shall not
    ///                    change the signedness of the underlying type."
    Rule(const std::string& ruleId, const std::string& description);
    virtual ~Rule() = default;

    // Non-copyable, movable.
    Rule(const Rule&) = delete;
    Rule& operator=(const Rule&) = delete;
    Rule(Rule&&) = default;
    Rule& operator=(Rule&&) = default;

    // ── Injected dependencies (set once per TU by the consumer) ──

    void setStore(ViolationStore* store)           { store_ = store; }
    void setDatabase(GlobalSymbolDatabase* db)     { db_    = db; }
    void setContext(clang::ASTContext* ctx)         { ctx_   = ctx; }

    // ── Interface for subclasses ─────────────────────────────────

    /// Register AST matchers on `finder`. Override in every rule.
    virtual void registerMatchers(
        clang::ast_matchers::MatchFinder& finder) = 0;

    // ── Accessors ────────────────────────────────────────────────

    const std::string& getRuleId()      const { return ruleId_; }
    const std::string& getDescription() const { return description_; }

protected:
    // ── Helpers available to subclasses ──────────────────────────

    /// Report a file-level violation at the given source location.
    /// Extracts file/line/col and the source line from `loc`.
    void report(clang::SourceLocation loc, const std::string& message);

    /// Returns true if the given statement is reachable
    /// (i.e. not after a return/break/throw in its parent compound).
    static bool isReachable(const clang::Stmt* stmt,
                            clang::ASTContext& ctx,
                            ParentBuilder& parentBuilder);

    /// Convenience: read the source line at `loc`.
    std::string getSourceLine(clang::SourceLocation loc) const;

    ViolationStore*       store_ = nullptr;
    GlobalSymbolDatabase* db_    = nullptr;
    clang::ASTContext*    ctx_   = nullptr;

private:
    std::string ruleId_;
    std::string description_;
};

} // namespace misra

#endif // MISRA_RULE_H
