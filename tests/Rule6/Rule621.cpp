// MISRA C++:2008 Rule 6-2-1
// Assignment operators shall not be used in sub-expressions

// The checker raises this defect whenever a subexpression contains an assignment operator.
// Because the assignment operator = is used in the subexpression (x = y), Polyspace flags it as noncompliant.

#include <cstdint>

bool example(int x, int y)
{
	if (x == 10) 		//Compliant
	{
		return true;
	}
	
       if ((x = y) == 0)            //Noncompliant
	{
		return false;
	}
	
	return false;
}