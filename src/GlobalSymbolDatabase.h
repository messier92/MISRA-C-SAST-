#ifndef MISRA_GLOBAL_SYMBOL_DATABASE_H
#define MISRA_GLOBAL_SYMBOL_DATABASE_H

#include "ViolationStore.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace misra {

/// Stores cross-translation-unit facts for project-level MISRA rules.
///
/// During per-TU AST traversal, rules call the `add*` methods to record
/// plain-value facts (no AST pointers!).  After all TUs are processed,
/// `main()` calls the `eval*` methods, which examine the accumulated
/// facts and push violations into the ViolationStore.
class GlobalSymbolDatabase {
public:
    // ── Data recorded per TU ───────────────────────────────────

    struct DeclRecord {
        std::string qualifiedName; // e.g. "ns::foo"
        std::string file;
        uint32_t    line = 0;
        uint32_t    col  = 0;
        bool        isDefinition = false;
        std::string type;          // Stringified type.
    };

    struct IdentifierRecord {
        std::string name;
        std::string scope;         // scopeKey() from the declaring context.
        std::string file;
        uint32_t    line = 0;
    };

    // ── Recording (called during per-TU traversal) ─────────────

    /// Record a declaration / definition for ODR and consistency checks.
    void addDeclaration(const DeclRecord& rec);

    /// Record an identifier for uniqueness / hiding checks.
    void addIdentifier(const IdentifierRecord& rec);

    // ── Evaluation (called once after all TUs) ─────────────────

    /// Evaluate all project-level rules and emit violations into `store`.
    void evaluateAll(ViolationStore& store) const;

    // ── Accessors ──────────────────────────────────────────────

    const std::vector<DeclRecord>& getDeclarations() const { return declarations_; }
    const std::vector<IdentifierRecord>& getIdentifiers() const { return identifiers_; }

private:
    /// Check for multiple conflicting definitions (ODR-like).
    void evalMultipleDefinitions(ViolationStore& store) const;

    /// Check for type-inconsistent external declarations.
    void evalInconsistentTypes(ViolationStore& store) const;

    std::vector<DeclRecord>       declarations_;
    std::vector<IdentifierRecord> identifiers_;
};

} // namespace misra

#endif // MISRA_GLOBAL_SYMBOL_DATABASE_H
