#ifndef MISRA_RULES_RULE_524_H
#define MISRA_RULES_RULE_524_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 5-2-4
/// "C-style casts (other than void casts) and functional notation casts
///  (other than explicit constructor calls) shall not be used."
class Rule524 : public Rule,
                public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule524();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULES_RULE_524_H
