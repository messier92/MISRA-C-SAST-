#ifndef MISRA_RULES_RULE_018_H
#define MISRA_RULES_RULE_018_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 0-1-8
/// "All functions with void return type shall have external side effect(s)."
class Rule018 : public Rule,
                public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule018();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;

    // MatchCallback
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULES_RULE_018_H

