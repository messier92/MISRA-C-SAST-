#ifndef MISRA_RULES_RULE_508_H
#define MISRA_RULES_RULE_508_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 5-0-8
/// "An explicit integral or floating-point conversion shall not increase the
///  size of the underlying type of a cvalue expression."
class Rule508 : public Rule,
                public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule508();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULES_RULE_508_H

