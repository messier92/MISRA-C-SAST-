#include "Rule1606.h"
#include "PreprocessorTracker.h"

#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"

using namespace clang;
using namespace clang::ast_matchers;

namespace misra {

Rule1606::Rule1606()
    : Rule("MISRA-16-0-6",
           "In the definition of a function-like macro, each instance of a parameter shall be enclosed in parentheses, unless it is used as the operand of # or ##.") {}

void Rule1606::registerMatchers(MatchFinder& finder) {
    finder.addMatcher(translationUnitDecl().bind("tu"), this);
}

void Rule1606::run(const MatchFinder::MatchResult& result) {
    if (!result.Context || !result.SourceManager) return;

    setContext(result.Context);
    const SourceManager& sm = *result.SourceManager;
    const auto& ppData = PreprocessorTUData::instance();

    for (const MacroParamViolationRecord& v : ppData.macroParamViolations) {
        if (v.paramLoc.isInvalid() || sm.isInSystemHeader(v.paramLoc)) continue;

        PresumedLoc presumed = sm.getPresumedLoc(v.paramLoc);
        if (presumed.isInvalid()) continue;
        if (store_ && !store_->getFilter().isProjectFile(presumed.getFilename())) {
            continue;
        }

        report(v.paramLoc,
               "Parameter '" + v.paramName + "' in function-like macro '" +
               v.macroName + "' shall be enclosed in parentheses [MISRA-16-0-6]");
    }
}

} // namespace misra

