// MISRA C++:2008 Rule 5-2-4
// C-style casts (other than void casts) and functional notation casts (other than explicit constructor calls) shall not be used

// The checker flags c-style casts in your code. Compliant C++ style casting operations include:
// std::static_cast
// std::reinterpret_cast
// std::const_cast
 // std::dynamic_cast
// Cast by using {} notation
// In addition, the reference_cast operation from the boost library and the safe_cast operation from the Miscrosoft library are also allowed.

#include <cstdint>
class Base
{
public:
	explicit Base(std::int32_t) {}
	virtual void Fn() noexcept {}
};
class Derived : public Base
{
public:
	void Fn() noexcept override {}
};
class myClass
{
};
std::int32_t G() noexcept
{
	return 7;
}

void Foo() noexcept(false)
{
	Base obj = Base{10};        // Compliant
	const Base obj3(5);
	Base* obj4 = const_cast<Base*>(&obj3);        //Compliant
	Base* obj2;
	myClass* d1 = reinterpret_cast<myClass*>(obj4); // Compliant 
	Derived* d2 = dynamic_cast<Derived*>(obj2);       // Compliant
	std::int16_t var1 = 20;
	std::int32_t var2 = static_cast<std::int32_t>(var1);      // Compliant
	std::int32_t var4 = 10;
	float f1 = static_cast<float>(var4);                    // Compliant

	std::int32_t var5 = static_cast<std::int32_t>(f1);      //Compliant

	std::uint32_t var7 = std::uint32_t{0};//Compliant
	static_cast<void>(G());                               //Compliant
}
void Bar(){
	Base obj = Base{1};
	Base* obj2 = (Base*)(&obj); //Noncompliant
	std::int16_t var1 = 20;
	std::int32_t var3 = (std::int32_t)var1;                   // Noncompliant
	float f = (float)var3;                                 // Noncompliant
	std::int32_t var6 = (std::int32_t)f;                   // Noncompliant
}