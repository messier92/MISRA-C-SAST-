misra_checker

1. Purpose
misra_checker is a command-line static analysis tool that checks C++ source code against a subset of the MISRA C++:2008 coding guidelines. It is built on Clang LibTooling, so it parses code with the real Clang front end and inspects the resulting AST instead of using regexes or ad-hoc parsing.
What it is trying to do:
• Take a set of C++ sources (a directory, a list of files, or a compile_commands.json).
• Parse each translation unit (TU) with the correct compiler flags.
• Run a library of rule checkers against each TU.
• Combine results from all TUs, including rules that only make sense across the whole project.
• Report violations to the console and to a text file, with a per-rule summary.

2. Build and Environment
• Platform: Windows, MSVC (Visual Studio 2022), C++17.
• Primary dependency: LLVM/Clang built from source in Release mode. LibTooling and ASTMatchers are the key parts.
• Libraries to link: clangTooling, clangASTMatchers, clangFrontend, clangAST, clangBasic, clangLex, clangSerialization, the LLVM component libs (LLVM_AVAILABLE_LIBS), and the Win32 import libs.
• Output binary: build/Release/misra_checker.exe.
• Practical note (inferred): the LLVM build configuration (Release, same CRT setting as the checker) must match the checker's, otherwise you get linker or runtime mismatches on MSVC.

3. Two Analysis Levels
Level
What it answers
How it works
File-level
Violations visible inside one TU (e.g. a C-style cast, an unreachable statement)
Rule matchers run on the TU's AST; report() adds a violation to the ViolationStore immediately
Project-level
Violations that need the whole program (e.g. duplicate or conflicting declarations across files, ODR-style issues)
Rules record plain-value facts into GlobalSymbolDatabase while each TU is processed; evaluation happens once, after all TUs are done
The original note suggests splitting these into more levels if useful. One possible split (inferred): preprocessor-level rules (macros and includes), per-TU AST rules, and cross-TU rules.
4. How It Works: Data Flow
argv (dir / files / compile_commands.json)
  └─ main(): parse CLI, discover sources, build clang argv
       └─ per TU:
            MisraChecker (ASTFrontendAction)
              └─ CreateASTConsumer: installs PPCallbacks, returns MisraConsumer
                   └─ MisraConsumer::HandleTranslationUnit(ctx)
                        └─ for each rule: setStore / setDB / registerMatchers (finder)
                           MatchFinder::matchAST(ctx)
                             ├─ file-level rules    → report() adds to ViolationStore
                             └─ project-level rules → GlobalSymbolDatabase.addXxx
  └─ main() afterwards:
       store.setScannedFiles / setReportTag
       store.print() → finalize() → misra_output.txt
Step by step:
1. main() parses the CLI and discovers source files. It builds the Clang command-line arguments, either from compile_commands.json or from defaults.
2. A FrontendActionFactory creates a fresh MisraChecker action for every TU.
3. CreateASTConsumer installs PPCallbacks (so preprocessor-level rules can see includes and macros) and returns a MisraConsumer.
4. HandleTranslationUnit(ctx) runs once the whole TU is parsed. For each rule it injects the store and database (setStore / setDB) and lets the rule register its AST matchers on a shared MatchFinder.
5. matchAST(ctx) runs all matchers in one traversal. Callbacks either report a violation directly or record facts in the database.
6. Back in main(), once every TU is done, project-level rules are evaluated (evalXxx) from the database, the store is given the scanned files and a report tag, then output is printed and finalized to misra_output.txt.
5. Source Layout
src/
• main.cpp: CLI handling and the FrontendActionFactory.
• MisraChecker.h/.cpp: the ASTFrontendAction, the ASTConsumer, and SEH handling. SEH is presumably there so that a crash in one TU does not kill the whole run (inferred).
• Rule.h/.cpp: the abstract Rule base class, ParentBuilder, report(), and isReachable().
• ViolationStore.h/.cpp: the Violation struct, the store, suppression handling, and output.
• ProjectFileFilter.h/.cpp: filters results to files under the project root.
• GlobalSymbolDatabase.h/.cpp: cross-TU records and project-level evaluation.

6. Implemented Rules
Numbering is MISRA C++:2008. The descriptions are the standard rule titles, paraphrased from memory, (inferred) and worth checking against the standard. Rule numbers 3-1-1 and 15-3-5/6/7 are the least certain decodings of the corrupted text.
Group
Rules
Topic (inferred)
Rule 0
0-1-1, 0-1-3, 0-1-6, 0-1-7, 0-1-8, 0-1-9
Unreachable code, unused variables, unused assignments, ignored return values, side-effect-free functions, dead operations
Rule 2
2-10-1, 2-10-2, 2-10-3, 2-10-4, 2-10-6
Identifier uniqueness, hiding, and naming clashes
Rule 3
3-1-1, 3-2-2, 3-2-3, 3-9-1
Declarations and definitions, ODR, consistent types
Rule 5
5-0-4, 5-0-6, 5-0-8, 5-2-4, 5-3-1
Implicit and explicit conversions, C-style casts, boolean operands
Rule 6
6-2-1, 6-4-5, 6-5-1, 6-6-5
Assignment in sub-expressions, switch clauses, for-loop rules, single exit point
Rule 7
7-1-1, 7-3-1
const-correctness, global namespace contents
Rule 9
9-3-1, 9-3-3, 9-5-1
const member functions, static member functions, unions
Rule 15
15-3-5, 15-3-6, 15-3-7, 15-5-1
Exception handler rules, destructors and exceptions
Rule 16
16-0-1, 16-0-6, 16-2-1
Preprocessor usage
7. Tests
tests/
• run_tests.ps1: PowerShell driver that runs the checker over the test inputs.
• Rule*/…: per-rule test directories with sample code. The expectation is that compliant code gives no violation and non-compliant code gives the expected one (inferred).

8. AST Infrastructure
Visitors (helpers for logic that matchers alone cannot express):
• ParentBuilder: builds a statement → parent map, because Clang does not give cheap parent lookup.
• FindVarUse, FindDeclStmt, FindVarRead: locate uses, declarations, and reads of a variable.
• FindExternalSideEffects: determines whether code has externally visible side effects.
• ReturnCollector: collects return statements in a function.
• Rules 3-9-1, 6-5-1, 7-1-1, 9-3-1, 9-3-3, and 15-5-1 use manual child recursion or RecursiveASTVisitor with depth caps. The caps are probably there to avoid stack overflow or runaway traversal on deep ASTs (inferred).
Matchers: only stock ASTMatchers are used. No custom matcher definitions. Extra filtering happens inside the callbacks via dyn_cast.

9. Key Helpers
• isReachable(stmt, ctx, parentBuilder): returns false if the statement follows a terminator (return, break, throw, etc.) or sits in a constant-false branch. This lets rules ignore dead code.
• scopeKey(decl): produces a TU-safe scope string, so scopes from different TUs can be compared without pointers.
• normalizeIdentifier: normalizes names for rule 2-10-1 (identifier distinctness).

10. Design Rules and Invariants
• No cross-TU AST pointers, ever. AST nodes die with their TU's ASTContext. All cross-file analysis goes through GlobalSymbolDatabase, identifying entities by a qualified-name string or a (scope string, name) pair.
• GlobalSymbolDatabase records store plain values only. This is the invariant that makes the point above safe.
• Ownership: main() owns both the ViolationStore and the GlobalSymbolDatabase. Rules and consumers get raw pointers, so they must not outlive main()'s scope.
• Matchers over hand-rolled traversal where possible. Custom visitors are used only when matchers cannot express the rule.

11. Data Structures
• Violation: file, line, col, rule, message. Created in report() (file-level) or evalXxx (project-level) and stored in the ViolationStore.
• ViolationStore members: a ProjectFileFilter, vector<Violation> violations_, registeredRules, scannedFiles, reportTag. It also handles suppression and output.
• ProjectFileFilter (owned by value in the store): root (normalized; empty means the filter is disabled), normalisePath, isProjectFile. It keeps system and third-party headers out of the report.

12. Output
Two output channels only: the console and a per-run .txt file (misra_output.txt).
Console line:
<file>:<line>:<col> <rule> <message>  <source line>
Summary:
MISRA-x-x-X: N
...
Total: N violations
• Violations are sorted by file, then line, then column.
• Every registered rule appears in the summary, even with a count of 0. This shows which rules ran, not only which fired.

13. Adding a New Rule
1. Subclass the Rule ABC in Rule.h and give it a rule ID.
2. In registerMatchers, add stock ASTMatchers and filter further in the callback with dyn_cast.
3. For file-level checks, call report() (use isReachable() where dead code should be ignored).
4. For cross-file checks, add plain-value records to GlobalSymbolDatabase via a new addXxx, and add an evalXxx that runs after all TUs.
5. Register the rule so it appears in the summary.
6. Add a tests/RuleN/ case with compliant and non-compliant samples and run run_tests.ps1.