#ifndef MISRA_RULES_RULE_323_H
#define MISRA_RULES_RULE_323_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 3-2-3
/// "A type, object or function that is used in multiple translation units
///  shall be declared in one and only one file."
class Rule323 : public Rule,
                public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule323();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULES_RULE_323_H

