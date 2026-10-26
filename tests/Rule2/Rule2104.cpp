// MISRA C++:2008 Rule 2-10-4
// A class, union or enum name (including qualification, if any) shall be a unique identifier

// The rule flags identifier declarations where the identifier name is the same as a previously declared class, union or typedef name. The checker does not flag situations where the conflicting names occur in different namespaces.

void f1() {
    class floatVar {};
}

void f2() {
    float floatVar; //Noncompliant
}