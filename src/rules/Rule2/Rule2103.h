#ifndef MISRA_RULES_RULE_2103_H
#define MISRA_RULES_RULE_2103_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 2-10-3
/// "A typedef name (including qualification, if any) shall be a unique identifier."
class Rule2103 : public Rule,
                 public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule2103();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULES_RULE_2103_H

