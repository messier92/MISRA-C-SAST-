#ifndef MISRA_RULE_16_0_1_H
#define MISRA_RULE_16_0_1_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 16-0-1
/// #include directives in a file shall only be preceded by other preprocessor
/// directives or comments.
class Rule1601 : public Rule,
                 public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule1601();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULE_16_0_1_H

