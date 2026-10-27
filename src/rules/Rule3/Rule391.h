#ifndef MISRA_RULES_RULE_391_H
#define MISRA_RULES_RULE_391_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 3-9-1
/// "The types used for an object, a function return type, or a function
///  parameter shall be token-for-token identical in all declarations and
///  re-declarations."
class Rule391 : public Rule,
                public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule391();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULES_RULE_391_H

