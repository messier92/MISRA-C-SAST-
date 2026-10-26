// MISRA C++:2008 Rule 16-0-1
// #include directives in a file shall only be preceded by other preprocessor directives or comments

// The checker raises this defect when an #include directive comes after any code that is not a comment or preprocessor directive. Ignores code that is hidden by using conditional compilation directives such as #if or #ifdef. 
// Do not report a violation of this rule when an #include directive is located within an extern "C" block.

//this comment is compliant     //Compliant
/*   This comment is compliant */

#ifndef TESTING_H               //Compliant
#define TESTING_H               //Compliant

#include <iostream>             //Compliant	
using namespace std;            //Compliant
#include <exception>            //Noncompliant

#endif