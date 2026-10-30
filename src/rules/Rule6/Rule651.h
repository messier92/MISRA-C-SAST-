#ifndef MISRA_RULES_RULE_651_H
#define MISRA_RULES_RULE_651_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 6-5-1
/// "A for loop shall contain a single loop-counter which shall not have
///  floating type."
class Rule651 : public Rule,
                public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule651();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULES_RULE_651_H

