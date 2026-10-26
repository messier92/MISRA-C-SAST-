#ifndef MISRA_RULES_RULE_524_H
#define MISRA_RULES_RULE_524_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 5-2-4
/// "Do not use C-style casts (including functional notation casts)
///  except as void casts."
///
/// Rationale: C-style casts can silently perform dangerous conversions
/// (const_cast, reinterpret_cast, or combined casts) without warning.
/// Named casts make the programmer's intent explicit.
class Rule524 : public Rule,
                public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule524();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;

    // MatchCallback
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULES_RULE_524_H
