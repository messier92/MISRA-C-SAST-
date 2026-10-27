#ifndef RULE311_H
#define RULE311_H

// MISRA C++:2008 Rule 3-1-1
// It shall be possible to include any header file in multiple translation units without violating the One Definition Rule
//
// The rule checker flags variable and function definitions in header files.
// Reports violation of this rule in header files. In a nonheader source file, violation of this rule is not reported.

#include <cstdint>

void F1();        // Compliant
extern void F2(); // Compliant

void F3()         //Noncompliant
{
}

static inline void F4()
{
} // Compliant

template <typename T>
void F5(T) // Compliant
{
}

std::int32_t a;                       //Noncompliant
extern std::int32_t b;                // Compliant
constexpr static std::int32_t c = 10; // Compliant

namespace ns
{
    constexpr static std::int32_t d = 100; // Compliant
    const static std::int32_t e = 50;      // Compliant
    static std::int32_t f;                 //Noncompliant
    static void F6() noexcept;             //Noncompliant
}

#endif // RULE311_H