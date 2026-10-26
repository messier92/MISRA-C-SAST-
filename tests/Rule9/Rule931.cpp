// MISRA C++:2008 Rule 9-3-1
// const member functions shall not return non-const pointers or references to class-data

// The checker flags a violation of this rule only if a const member function returns a non-const pointer or reference to a non-static data member. The rule does not apply to static data members.
// In this example, the const member function getR() returns a non-const pointer and violates the rule.
// the const member function getConstR() returns a const pointer and is compliant with the rule.

class A{
	
	private:
	
	int& rInt;
	public:
	
	int* getR() const{ //Noncompliant
		return &rInt;
	}
	
	const int* getConstR() const{ //Compliant
		return &rInt;
	}
};