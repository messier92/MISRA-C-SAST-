// MISRA C++:2008 Rule 9-5-1
// Unions shall not be used

// Flags the declaration of a union. You might consider the use of union necessary or acceptable in your code. In such cases, justify the violation by annotating the result or by using code comments. 

#include <iostream>

union Pi{ //Noncompliant
	int i;
	double d;
};

void foo(void){

	std::cout << std::endl;

	Pi pi;
	pi.d = 3.1416;// pi holds a double
	std::cout << "pi.d: " << pi.d << std::endl;     
	std::cout << "pi.i: " << pi.i << std::endl;      // Undefined Behavior

	std::cout << std::endl;

	pi.i = 4;     // pi holds an int
	std::cout << "pi.i: " << pi.i << std::endl;
	std::cout << "pi.d: " << pi.d << std::endl;      // Undefined Behavior

	std::cout << std::endl;

}