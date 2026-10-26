// MISRA C++:2008 Rule 9-3-3
// If a member function can be made static then it shall be made static, otherwise if it can be made const then it shall be made const

// The checker performs these checks in this order:
// The checker first checks if a class member function accesses a data member of the class. Functions that do not access data members can be declared static.
// The checker then checks functions that access data members to determine if the function modifies any of the data members. Functions that do not modify data members can be declared const.
// A violation on a const member function means that the function does not even access a data member of the class and can be declared static.

// Functions foo, foo2, foo3, and getref are marked as noncompliant.
// The functions foo and foo3 do not modify any nonstatic data members. Because their data access is not explicitly restricted by declaring them as const, Polyspace flags these functions. To fix these defects, declare foo and foo3 as const.
// The function foo2 does not explicitly modify any of the data members. Because it is not declared as const, Polyspace flags the function. foo2 calls the global function Connector, which might have side effects. Do not declare foo2 as a const function. In C++11 or later, const member functions are expected to be thread-safe, but foo2 might not be thread-safe because of the side effects of Connector. To avoid data races, keep foo2 as a nonconst function. Justify the defect by using review information or code comments.
// The function getref does not modify any data members. Because it is not declared as const, Polyspace flags it. Declaring getref as const resolves this defect, but that is not enough to restrict write access of getref because it returns a nonconst reference to m_i_ref. To restrict getref from modifying m_i_ref, the return type of getref must also be const.

#include<cstdint>
void Connector(void);
class A
{
public:
	int16_t foo ( ) // Noncompliant 
	{
		return m_i;
	}
	int16_t foo2 ( ) // Noncompliant 
	{
		Connector();// Might have side-effect
		return m_i;
	}
	int16_t foo3 ( ) // Noncompliant
	{
		return m_s;
	}
	int16_t inc_m ( ) // Compliant 
	{
		return ++m_i;
	}
	int16_t& getref()//Noncompliant 
	{
		return m_i_ref; 
	}
private:
	int16_t m_i;
	static int16_t m_s;
	int16_t& m_i_ref;
};