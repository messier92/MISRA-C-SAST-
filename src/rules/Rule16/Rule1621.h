#ifndef MISRA_RULE_16_2_1_H
#define MISRA_RULE_16_2_1_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 16-2-1
/// The preprocessor shall only be used for file inclusion and include guards.
class Rule1621 : public Rule,
                 public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule1621();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULE_16_2_1_H

