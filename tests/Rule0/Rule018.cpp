// MISRA C++:2008 Rule 0-1-8
// All functions with void return type shall have external side effect(s)

// Flag the definition of a void type function if the function has no side effects.
// A function has a side effect if the function performs any of these tasks: 
// Calls an impure function other than itself.
// Changes the value or dereferences a reference or pointer type argument.
// Changes the value or deferences of a nonlocal variable.
// Uses a volatile object.
// Contains assembly instructions.
// Accesses the this pointer of its parent class.
// Raises an exception. Raising exceptions is considered as a side effect even if the function does not exit with an exception.
// Dereferences a local pointer that is assigned an absolute address.
// Accesses a volatile object, class member, or struct member.
// Sets the value of a class or struct member.
// This checker does not flag these entities:
// Standard implementations of placement new and delete functions.
// Constructors and destructors.
// Virtual class member functions that are empty.
// Lambda objects.

#include<cmath>

void init ( int refToInt )          // Noncompliant
{
	refToInt = 0;
}

void transform(double theta) //Noncompliant
{
	theta = sin(theta)/(1-cos(theta));
}
void foo(int val){ //Noncompliant
	if(val){
		foo(val--);
	}
}
