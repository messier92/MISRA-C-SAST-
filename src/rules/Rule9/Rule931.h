#ifndef MISRA_RULE_9_3_1_H
#define MISRA_RULE_9_3_1_H

#include "Rule.h"

namespace misra {

/// MISRA C++:2008 Rule 9-3-1
/// Const member functions shall not return non-const pointers or references
/// to class-data.
class Rule931 : public Rule,
                public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    Rule931();
    void registerMatchers(clang::ast_matchers::MatchFinder& finder) override;
    void run(const clang::ast_matchers::MatchFinder::MatchResult& result) override;
};

} // namespace misra

#endif // MISRA_RULE_9_3_1_H
