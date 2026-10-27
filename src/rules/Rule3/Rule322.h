#ifndef MISRA_RULES_RULE_322_H
#define MISRA_RULES_RULE_322_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 3-2-2
/// "The One Definition Rule shall not be violated."
class Rule322 : public Rule,
                public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule322();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULES_RULE_322_H

