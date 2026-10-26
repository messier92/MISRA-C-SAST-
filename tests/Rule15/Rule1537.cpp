// MISRA C++:2008 Rule 15-3-7
// Where multiple handlers are provided in a single try-catch statement or function-try-block, any ellipsis (catch-all) handler shall occur last

// The checker raises this defect whenever a handler appears after the catch-all handler within the try-catch statement or function try-block.

// Because the catch (exception& e2) handler comes after the catch(â€¦) handler, the checker flags the handler before the catch-all handler as noncompliant. This issue might result in a compilation error.


#include <iostream>
#include <exception>

using namespace std;

int main()
{

	try
	{
		//some code
	} catch(exception& e1) {     //Compliant
	
		//...
	
	} catch(...) {               //Compliant
		
		//...
	
	} catch(exception& e2) {     //Noncompliant
	
		//...
	
	}
	}
	
	return 0;
}