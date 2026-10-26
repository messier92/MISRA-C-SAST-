// MISRA C++:2008 Rule 6-4-5
// An unconditional throw or break statement shall terminate every non - empty switch-clause

// The checker raises this defect whenever a case-label contains any statements and a throw or break statement is not the final statement of the case-label.

#include <cstdint>

int x, y = 2;

int example(int x)	 
{
    switch (x) { //Noncompliant error shows here
    case 0:              //Compliant empty fall through
    case 1:
        break;           //Compliant break
    case 2:
        x = y ^ 2;       //Unintentional fall through
    case 3:              //Compliant throw
        throw;

    default:             //Compliant break
        break;
    }

    return x;
}