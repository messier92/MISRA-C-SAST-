// MISRA C++:2008 Rule 15-5-1
// A class destructor shall not exit with an exception

// The checker flags exceptions thrown in the body of the destructor. If the destructor calls another function, the checker does not detect if that function throws an exception.
// The checker does not detect these situations:
// A catch statement does not catch exceptions of all types that are thrown.
// The checker considers the presence of a catch statement corresponding to a try block as indication that an exception is caught.
// throw statements inside catch blocks

#include <exception>
#include <stdexcept>
class myClass {
	//...
	~myClass()  { 
		//...
        if(cond)
            throw std::logic_error("Error"); //Noncompliant
	}
	private:
    volatile int cond;
};