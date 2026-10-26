// MISRA C++:2008 Rule 2-10-3
// A typedef name (including qualification, if any) shall be a unique identifier

// The rule reports a violation on identifier declarations where the identifier name is the same as a previously declared typedef name. The checker does not flag situations where the conflicting names occur in different namespaces.

namespace NS1 {
    typedef int WIDTH;
}

namespace NS2 {
    float WIDTH; //Compliant
}

void f1() {
    typedef int TYPE;
}

void f2() {
    float TYPE; //Noncompliant
}