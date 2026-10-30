#ifndef MISRA_RULES_RULE_504_H
#define MISRA_RULES_RULE_504_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 5-0-4
/// "An implicit integral conversion shall not change the signedness of the
///  underlying type."
class Rule504 : public Rule,
                public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule504();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULES_RULE_504_H

