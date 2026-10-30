#ifndef MISRA_RULE_9_5_1_H
#define MISRA_RULE_9_5_1_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 9-5-1
/// Unions shall not be used.
class Rule951 : public Rule,
                public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule951();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULE_9_5_1_H
