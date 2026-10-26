// MISRA C++:2008 Rule 0-1-3
// A project shall not contain unused variables.
// The rule checker reports a violation of this rule if:
// A local or global variable is declared or defined but not read in any source files of the project.
// A member variable of a structure or a class is declared or defined but not read or written in any source files of the project.

#include <iostream>
#include <string>

class MyClass
{
private:
    std::string string1; //Compliant
    std::string string2; //Compliant
    std::string string3; //Noncompliant

public:
    MyClass(const std::string& first,
            const std::string& second,
            const std::string& third)
        : string1(first), string2(second), string3(third)
    {
    }

    const std::string& getString1() const
    {
        return string1; //Compliant
    }

    const std::string& getString2() const
    {
        return string2; //Compliant
    }

    const std::string& getString3() const
    {
        return string3; //Compliant when used; declaration is intentionally marked Noncompliant above
    }
};

struct S
{
    unsigned char b1 : 3; //Compliant
    unsigned char pad : 1; // Noncompliant
    unsigned char b2 : 4; //Compliant
};

void init(S& object)
{
    object.b1 = 0U; //Compliant
    object.b2 = 0U; //Compliant
}

struct SCompliant
{
    unsigned char b1 : 3; //Compliant
    unsigned char : 1; //Compliant
    unsigned char b2 : 4; //Compliant
};

void initCompliant(SCompliant& object)
{
    object.b1 = 0U; //Compliant
    object.b2 = 0U; //Compliant
}

int main()
{
    MyClass example("one", "two", "three"); //Compliant
    S bits = {}; //Compliant
    SCompliant compliantBits = {}; //Compliant

    init(bits); //Compliant
    initCompliant(compliantBits); //Compliant

    std::cout << example.getString1() << '\n'; //Compliant
    std::cout << example.getString2() << '\n'; //Compliant
    // string3 is intentionally not read here, so it remains a noncompliant example.

    std::cout << static_cast<unsigned int>(bits.b1) << ' '
              << static_cast<unsigned int>(bits.b2) << '\n'; //Compliant
    std::cout << static_cast<unsigned int>(compliantBits.b1) << ' '
              << static_cast<unsigned int>(compliantBits.b2) << '\n'; //Compliant

    return 0; //Compliant
}