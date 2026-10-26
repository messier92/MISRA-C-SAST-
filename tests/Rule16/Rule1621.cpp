// MISRA C++:2008 Rule 16-2-1
// The preprocessor shall only be used for file inclusion and include guards

// The checker raises a violation of this rule in an included header file when either of these conditions is true:
// #define is used outside of an include guard. These #define statements typically define constants and function-like macros.
// #ifndef is used outside of an include guard.
// The checker considers this idiom as the correct include guard idiom:
// #ifndef <identifier>
// #define <identifier>
// #endif

#ifndef MY_FILE
#define MY_FILE //Compliant
#endif
#define PI 3.1416 //Noncompliant
constexpr double pi = 3.1416;

#include"file1.h"
//...