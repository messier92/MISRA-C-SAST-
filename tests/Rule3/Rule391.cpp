// MISRA C++:2008 Rule 3-9-1
// The types used for an object, a function return type, or a function parameter shall be token-for-token identical in all declarations and re-declarations

// The rule checker compares the current declaration with the last seen declaration.

typedef int* intptr;

int* map;
extern intptr map; //Noncompliant

intptr table;
extern intptr table; //Compliant