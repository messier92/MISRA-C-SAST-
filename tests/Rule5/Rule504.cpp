// MISRA C++:2008 Rule 5-0-4
// An implicit integral conversion shall not change the signedness of the underlying type

// The checker flags implicit conversions from a signed to an unsigned integer data type or vice versa.
// The checker assumes that ptrdiff_t is a signed integer.

typedef char int8_t;
typedef unsigned char uint8_t;

void func()
  {
    int8_t s8;
    uint8_t u8;

    s8 = u8; //Noncompliant
    u8 = s8 + u8; //Noncompliant
    u8 = static_cast< uint8_t > ( s8 ) + u8; //Compliant
}