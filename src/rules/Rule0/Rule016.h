#ifndef MISRA_RULES_RULE_016_H
#define MISRA_RULES_RULE_016_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 0-1-6
/// "A project shall not contain instances of non-volatile variables being
///  given values that are never subsequently used."
class Rule016 : public Rule,
                public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule016();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;

    // MatchCallback
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULES_RULE_016_H

