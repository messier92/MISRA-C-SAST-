// MISRA C++:2008 Rule 7-1-1
// A variable which is not modified shall be const qualified

// The checker flags:
// Function parameters or local variables that are not const-qualified but never modified in the function body.
// Pointers that are not const-qualified but point to the same location during its lifetime.
// Function parameters of integer, float, enum, and Boolean types are not flagged.
// If a variable is passed to another function by reference or pointers, the checker assumes that the variable can be modified. These variables are not flagged.

#include <cstddef>

char getNthChar(const char* str, int N){//Noncompliant
	int index=0;
	while(*(str+index)!='\0'){
		if(index==N)
			return *(str+N);
		++index;
	}
	return '\0';
}
char getNthChar_const_safe(const char* const str, int N){
	int index=0;

	while(*(str+index)!='\0'){
		if(index==N)
		    return *(str+N);
		++index;
	}
	return '\0';
}