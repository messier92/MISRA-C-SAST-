#ifndef MISRA_RULE_7_3_1_H
#define MISRA_RULE_7_3_1_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 7-3-1
/// The global namespace shall only contain main, namespace declarations and
/// extern "C" declarations.
class Rule731 : public Rule,
                public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule731();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULE_7_3_1_H
