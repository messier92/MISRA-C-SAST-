#ifndef MISRA_RULES_RULE_311_H
#define MISRA_RULES_RULE_311_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 3-1-1
/// "It shall be possible to include any header file in multiple translation
///  units without violating the One Definition Rule."
class Rule311 : public Rule,
                public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule311();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULES_RULE_311_H

