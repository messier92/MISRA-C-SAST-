// MISRA C++:2008 Rule 2-10-6
// If an identifier refers to a type, it shall not also refer to an object or a function in the same scope

// Reports a violation of this rule if you use an identifier both as a type name and as a function or object name. If the identifier is a function name and the function is both declared and defined, then the violation is reported once.

struct vector{
    int x;
    int y;
    int z;
}vector; //Noncompliant