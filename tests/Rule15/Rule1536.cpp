// MISRA C++:2008 Rule 15-3-6
// Where multiple handlers are provided in a single try-catch statement or function-try-block for a derived class and some or all of its bases, the handlers shall be ordered most-derived to base class

// Flags a handler block if it follows a handler of a base class.

// In this example, three classes in a hierarchy might arise in the try block. The handler catch blocks handle the exceptions.
// The block catch ( NotANumber &nan ) follows the handler of its base class catch ( MathError &e ). Because the exception of class NotANumber also matches to the handler catch ( MathError &e ), the handler block catch ( NotANumber &nan ) becomes unreachable code. The order of this block is noncompliant with this rule. The checker flags the handler block.
// The block catch ( DivideByZero &dbz ) becomes unreachable code because exceptions of the class DivideByZero match to the preceding handlers of its base classes. The checker flags the handler block catch ( DivideByZero &dbz ).

#include<exception>
// classes used for exception handling
class MathError { };
class NotANumber: public MathError { };
class DivideByZero: public NotANumber{};

void bar(void){
	try
	{
		// ...
	}
	catch ( MathError &e ) 
	{
		// ...
	}
	catch ( NotANumber &nan ) // Noncompliant
	{
		// Unreachable Code
		
	}
	catch (DivideByZero &dbz)//Noncompliant
	{
		//Unreachable Code
	}
}