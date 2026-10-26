// MISRA C++:2008 Rule 6-5-1
// A for loop shall contain a single loop-counter which shall not have floating type

// This rule checker flags these situations:
// The for loop index has a floating point type.
// More than one loop counter is incremented in the for loop increment statement.
// For instance:
// for(i=0, j=0; i<10 && j < 10;i++, j++) {}
// A loop counter is not incremented in the for loop increment statement.
//For instance:
// for(i=0; i<10;) {}
// Even if you increment the loop counter in the loop body, the checker still raises a violation. 
// According to the MISRA C++ specifications, a loop counter is one that is initialized in or prior to the loop expression, acts as an operand to a relational operator in the loop expression and is modified in the loop expression. If the increment statement in the loop expression is missing, the checker cannot find the loop counter modification and considers as if a loop counter is not present.

#include <iostream>

int main()
{
	int x = 0;
	for (int i = 0; i >= x; i = x++)   // Non-compliant
	{
		//...
	}
}

#include <iostream>

int main()
{
	for (float x = 0.1f; x <= 1.0f; x += 0.1f) {   //Noncompliant
		std::cout << x << std::endl;
	}
}