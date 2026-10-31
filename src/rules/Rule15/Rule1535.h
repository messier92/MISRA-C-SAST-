#ifndef MISRA_RULE_15_3_5_H
#define MISRA_RULE_15_3_5_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 15-3-5
/// A class type exception shall always be caught by reference.
class Rule1535 : public Rule,
                 public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule1535();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULE_15_3_5_H

