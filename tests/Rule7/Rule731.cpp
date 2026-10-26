// MISRA C++:2008 Rule 7-3-1
// The global namespace shall only contain main, namespace declarations and extern "C" declarations

// Other than the main function, the checker flags all names used at global scope that are not part of a namespace.
// The checker does not flag names at global scope if they are declared in extern "C" blocks (C code included within C++ code).

#include <cstdint>
void foo(int32_t);                //Noncompliant
int32_t var1;                      //Noncompliant

namespace                       // Compliant
{
	void foo2(int32_t);             // Rule does not apply
	int32_t var2;                 // Rule does not apply
}

namespace ns_foo                // Compliant
{
	void bar(int32_t);             // Rule does not apply
	int32_t var3;                   // Rule does not apply
}

namespace ns_x = ns_foo;          // Compliant

int main()                      // Compliant
{
	extern void foo3();             //Noncompliant
}