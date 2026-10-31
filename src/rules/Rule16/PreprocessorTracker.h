#ifndef MISRA_PREPROCESSOR_TRACKER_H
#define MISRA_PREPROCESSOR_TRACKER_H

#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/Token.h"
#include "llvm/ADT/SmallPtrSet.h"

#include <set>
#include <string>
#include <vector>

namespace misra {

struct IncludeDirectiveRecord {
    clang::SourceLocation hashLoc;
    bool fileExists = true;
};

struct MacroParamViolationRecord {
    clang::SourceLocation paramLoc;
    std::string macroName;
    std::string paramName;
};

struct PreprocessorTUData {
    std::vector<IncludeDirectiveRecord> includes;
    std::vector<clang::SourceRange> skippedRanges;
    std::vector<MacroParamViolationRecord> macroParamViolations;
    std::set<clang::FileID> visitedFiles;

    void clear() {
        includes.clear();
        skippedRanges.clear();
        macroParamViolations.clear();
        visitedFiles.clear();
    }

    static PreprocessorTUData& instance() {
        static PreprocessorTUData data;
        return data;
    }
};

/// Clang PPCallbacks implementation that records preprocessor events
/// (#include transitions, skipped conditional compilation ranges, function-like
/// macro definitions, and visited files) for MISRA Rule 16 checkers.
class MisraPPCallbacks : public clang::PPCallbacks {
public:
    explicit MisraPPCallbacks(clang::SourceManager& sm) : sm_(sm) {
        PreprocessorTUData::instance().clear();
    }

    void FileChanged(clang::SourceLocation Loc,
                     FileChangeReason Reason,
                     clang::SrcMgr::CharacteristicKind FileType,
                     clang::FileID /*PrevFID*/) override {
        if (Reason != EnterFile || Loc.isInvalid()) return;

        clang::FileID enteredFid = sm_.getFileID(Loc);
        if (enteredFid.isInvalid()) return;

        if (FileType == clang::SrcMgr::C_User) {
            clang::SourceLocation spell = sm_.getSpellingLoc(Loc);
            if (spell.isValid() && !sm_.isInSystemHeader(spell)) {
                PreprocessorTUData::instance().visitedFiles.insert(enteredFid);
            }
        }

        // If this file was entered via an `#include` directive, `getIncludeLoc`
        // points to the `#include` location in the including file.
        clang::SourceLocation incLoc = sm_.getIncludeLoc(enteredFid);
        if (incLoc.isValid()) {
            clang::SourceLocation spellInc = sm_.getSpellingLoc(incLoc);
            if (spellInc.isValid() && !sm_.isInSystemHeader(spellInc)) {
                clang::FileID includingFid = sm_.getFileID(spellInc);
                if (includingFid.isValid()) {
                    PreprocessorTUData::instance().visitedFiles.insert(includingFid);
                }
                IncludeDirectiveRecord rec;
                rec.hashLoc = spellInc;
                rec.fileExists = true;
                PreprocessorTUData::instance().includes.push_back(rec);
            }
        }
    }

    void SourceRangeSkipped(clang::SourceRange Range,
                            clang::SourceLocation /*EndifLoc*/) override {
        if (Range.isValid()) {
            PreprocessorTUData::instance().skippedRanges.push_back(Range);
        }
    }

    void MacroDefined(const clang::Token& MacroNameTok,
                      const clang::MacroDirective* MD) override {
        if (!MD) return;
        const clang::MacroInfo* MI = MD->getMacroInfo();
        if (!MI || !MI->isFunctionLike()) return;

        clang::SourceLocation defLoc = sm_.getSpellingLoc(MacroNameTok.getLocation());
        if (defLoc.isInvalid() || sm_.isInSystemHeader(defLoc) ||
            sm_.isWrittenInBuiltinFile(defLoc) ||
            sm_.isWrittenInCommandLineFile(defLoc)) {
            return;
        }

        llvm::SmallPtrSet<const clang::IdentifierInfo*, 8> paramSet(
            MI->param_begin(), MI->param_end());
        if (paramSet.empty()) return;

        std::string macroName;
        if (const auto* ii = MacroNameTok.getIdentifierInfo()) {
            macroName = ii->getName().str();
        }

        llvm::ArrayRef<clang::Token> tokens = MI->tokens();
        size_t n = tokens.size();
        for (size_t i = 0; i < n; ++i) {
            const clang::IdentifierInfo* ii = tokens[i].getIdentifierInfo();
            if (!ii || !paramSet.contains(ii)) continue;

            // Exception 1: Operand of stringification operator `#`.
            if (i > 0 && tokens[i - 1].is(clang::tok::hash)) {
                continue;
            }
            // Exception 2: Operand of token-pasting operator `##`.
            if ((i > 0 && tokens[i - 1].is(clang::tok::hashhash)) ||
                (i + 1 < n && tokens[i + 1].is(clang::tok::hashhash))) {
                continue;
            }
            // Exception 3 & 4: Immediately preceded by `.` or `->`.
            if (i > 0 && (tokens[i - 1].is(clang::tok::period) ||
                          tokens[i - 1].is(clang::tok::arrow))) {
                continue;
            }

            // Compliant if enclosed directly in parentheses `(param)`.
            if (i > 0 && tokens[i - 1].is(clang::tok::l_paren) &&
                i + 1 < n && tokens[i + 1].is(clang::tok::r_paren)) {
                continue;
            }

            MacroParamViolationRecord v;
            v.paramLoc = sm_.getSpellingLoc(tokens[i].getLocation());
            if (v.paramLoc.isInvalid()) {
                v.paramLoc = defLoc;
            }
            v.macroName = macroName;
            v.paramName = ii->getName().str();
            PreprocessorTUData::instance().macroParamViolations.push_back(v);
        }
    }

private:
    clang::SourceManager& sm_;
};

} // namespace misra

#endif // MISRA_PREPROCESSOR_TRACKER_H
