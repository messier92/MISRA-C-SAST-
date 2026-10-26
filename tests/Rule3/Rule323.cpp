// MISRA C++:2008 Rule 3-2-3
// A type, object or function that is used in multiple translation units shall be declared in one and only one file

// Report a violation of this rule if you declare an extern object in multiple files.

#include "header.hpp"
extern int flag;//Noncompliant
class A{/**/};
void foo(void){
	//..
} 

extern int flag;

// The external identifier flag is declared in both header.hpp and file1.cpp. Report a violation of this rule.

