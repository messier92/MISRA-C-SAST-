#ifndef MISRA_RULE_16_0_6_H
#define MISRA_RULE_16_0_6_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 16-0-6
/// In the definition of a function-like macro, each instance of a parameter
/// shall be enclosed in parentheses, unless it is used as the operand of
/// # or ##.
class Rule1606 : public Rule,
                 public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule1606();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULE_16_0_6_H

