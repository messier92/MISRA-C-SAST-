// MISRA C++:2008 Rule 5-0-6
// An implicit integral or floating-point conversion shall not reduce the size of the underlying type

// The checker flags implicit conversions that reduce the size of a type.
// If the conversion is to a narrower integer with a different sign, then rule 5-0-4 takes precedence over rule 5-0-6. Only rule 5-0-4 is shown.

typedef signed short int16_t;
typedef signed int int32_t;

void func ( )
  {
    int16_t   s16;;
    int32_t   s32;
    s16 = s32;   //Noncompliant
    s16 = static_cast< int16_t > ( s32 ); //Compliant
  }