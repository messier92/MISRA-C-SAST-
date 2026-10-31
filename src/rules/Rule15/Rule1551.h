#ifndef MISRA_RULE_15_5_1_H
#define MISRA_RULE_15_5_1_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 15-5-1
/// A class destructor shall not exit with an exception.
class Rule1551 : public Rule,
                 public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule1551();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULE_15_5_1_H

