#ifndef MISRA_RULE_7_1_1_H
#define MISRA_RULE_7_1_1_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 7-1-1
/// A variable which is not modified shall be const qualified.
class Rule711 : public Rule,
                public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule711();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULE_7_1_1_H
