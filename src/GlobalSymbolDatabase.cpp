#include "GlobalSymbolDatabase.h"

#include <algorithm>

namespace misra {

// ── Recording ──────────────────────────────────────────────────

void GlobalSymbolDatabase::addDeclaration(const DeclRecord& rec) {
    DeclRecord canonRec = rec;
    canonRec.file = ProjectFileFilter::canonicalisePath(rec.file);
    std::string normFile = ProjectFileFilter::normalisePath(canonRec.file);
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& existing : declarations_) {
        if (existing.line          == canonRec.line          &&
            existing.col           == canonRec.col           &&
            existing.isDefinition  == canonRec.isDefinition  &&
            existing.qualifiedName == canonRec.qualifiedName &&
            ProjectFileFilter::normalisePath(existing.file) == normFile) {
            return;
        }
    }
    declarations_.push_back(std::move(canonRec));
}

void GlobalSymbolDatabase::addIdentifier(const IdentifierRecord& rec) {
    IdentifierRecord canonRec = rec;
    canonRec.file = ProjectFileFilter::canonicalisePath(rec.file);
    std::string normFile = ProjectFileFilter::normalisePath(canonRec.file);
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& existing : identifiers_) {
        if (existing.line  == canonRec.line  &&
            existing.name  == canonRec.name  &&
            existing.scope == canonRec.scope &&
            ProjectFileFilter::normalisePath(existing.file) == normFile) {
            return;
        }
    }
    identifiers_.push_back(std::move(canonRec));
}

// ── Evaluation ─────────────────────────────────────────────────

void GlobalSymbolDatabase::evaluateAll(ViolationStore& store) const {
    std::lock_guard<std::mutex> lock(mutex_);
    evalMultipleDefinitions(store);
    evalInconsistentTypes(store);
}

void GlobalSymbolDatabase::evalMultipleDefinitions(ViolationStore& store) const {
    // Group definitions by qualified name.
    std::map<std::string, std::vector<const DeclRecord*>> defs;
    for (const auto& rec : declarations_) {
        if (rec.isDefinition) {
            defs[rec.qualifiedName].push_back(&rec);
        }
    }
    for (const auto& [name, recs] : defs) {
        if (recs.size() > 1) {
            for (size_t i = 1; i < recs.size(); ++i) {
                Violation v;
                v.file    = recs[i]->file;
                v.line    = recs[i]->line;
                v.col     = recs[i]->col;
                v.ruleId  = "MISRA-3-2-2";
                v.message = "Multiple definitions of '" + name +
                            "' (first defined at " + recs[0]->file +
                            ":" + std::to_string(recs[0]->line) + ")";
                store.addViolation(v);
            }
        }
    }
}

void GlobalSymbolDatabase::evalInconsistentTypes(ViolationStore& store) const {
    // Group all declarations by qualified name and check type consistency.
    std::map<std::string, std::vector<const DeclRecord*>> byName;
    for (const auto& rec : declarations_) {
        byName[rec.qualifiedName].push_back(&rec);
    }
    for (const auto& [name, recs] : byName) {
        if (recs.size() < 2) continue;
        const std::string& firstType = recs[0]->type;
        for (size_t i = 1; i < recs.size(); ++i) {
            if (recs[i]->type != firstType) {
                Violation v;
                v.file    = recs[i]->file;
                v.line    = recs[i]->line;
                v.col     = recs[i]->col;
                v.ruleId  = "MISRA-3-2-3";
                v.message = "Inconsistent type for '" + name +
                            "': '" + recs[i]->type +
                            "' vs '" + firstType + "' at " +
                            recs[0]->file + ":" +
                            std::to_string(recs[0]->line);
                store.addViolation(v);
            }
        }
    }
}

} // namespace misra
