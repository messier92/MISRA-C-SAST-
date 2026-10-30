#ifndef MISRA_RULES_RULE_506_H
#define MISRA_RULES_RULE_506_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 5-0-6
/// "An implicit integral or floating-point conversion shall not reduce the
///  size of the underlying type."
class Rule506 : public Rule,
                public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule506();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULES_RULE_506_H

