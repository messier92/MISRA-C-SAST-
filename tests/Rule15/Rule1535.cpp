// MISRA C++:2008 Rule 15-3-5
// A class type exception shall always be caught by reference

// The checker flags catch statements where class type exceptions are caught by value.
// Catch blocks for exceptions of type baseException, derivedException, and exampleException are noncompliant because the thrown class type exception is caught by value. These blocks might slice the exception objects.
// Catch blocks for exceptions of type baseException &, const baseException &, derivedException &, and const derivedException & are compliant because the class type exception is caught by reference or const reference.


#include <exception> 
#include <iostream> 

class baseException : public std::exception { 
public: 
	baseException() : exception() {} 
	const char* what() const noexcept(true) override { 
		return "Base Exception Object"; 
	} 
}; 

class derivedException : public baseException { 
public: 
	derivedException() : baseException() {} 
	const char* what() const noexcept(true) override { 
		return "Derived Exception Object"; 
	} 
}; 

class exampleException{}; 

void foo() { 
	try { 
		throw derivedException(); 
	} 
	catch (baseException e) { //Noncompliant 
		std::cout << e.what(); 
	} 
	catch (derivedException e) { //Noncompliant 
		std::cout << e.what(); 
	} 
	catch (exampleException e) { //Noncompliant 
	} 
	catch (baseException &e) { //Compliant 
		std::cout << e.what(); 
	} 
	catch (const baseException &e) { //Compliant 
		std::cout << e.what(); 
	} 
	catch (derivedException &e) { //Compliant 
		std::cout << e.what(); 
	} 
	catch (const derivedException &e) { //Compliant 
		std::cout << e.what(); 
	} 
} 