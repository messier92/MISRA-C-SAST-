#ifndef MISRA_RULE_9_3_3_H
#define MISRA_RULE_9_3_3_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 9-3-3
/// If a member function can be made static then it shall be made static,
/// otherwise if it can be made const then it shall be made const.
class Rule933 : public Rule,
                public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule933();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULE_9_3_3_H
