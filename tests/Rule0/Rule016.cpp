// MISRA C++:2008 Rule 0-1-6
// A project shall not contain instances of non-volatile variables being given values that are never subsequently used.

// The rule checker reports violations for value assignments to local and static variables with file scope if the assigned values are not subsequently used. (The checker considers const-qualified global variables without the extern specifier as static variables with file scope.)
// The checker reports violations on:
// Initializations if the initialized variable is not used.
// Value assignments if the assigned values are not used.
// For instance, you assigned a value to a variable and before the next read of the variable, assigned a different value. In this case, the checker flags the first redundant assignment. The only exception is the case where a value is assigned at variable initialization and later overwritten.
// The checker does not report violations on assignments in the last iteration of a loop, if the assignments in the previous iterations are not redundant. For instance, the assignment prevIter = i in the last iteration of the loop in the function func() is redundant but the assignments in the previous iterations are not.

class largeInteger {
       largeInteger(int d1, int d2, int d3):
            lastFiveDigits(d1), nextFiveDigits(d2), firstFiveDigits(d3){}
       largeInteger& operator=(const largeInteger& other) {
            if(&other !=this) {
              firstFiveDigits = other.firstFiveDigits;
              nextFiveDigits = other.nextFiveDigits;
              lastFiveDigits = other.lastFiveDigits;
            }
            return *this;
       }
       void printIntegerValue();
    private:
        int firstFiveDigits;
        int nextFiveDigits;
        int lastFiveDigits;
};

bool compareValues(largeInteger, largeInteger);

void func() {
    largeInteger largeUnit{10000,0,0}; //Compliant
    largeInteger smallUnit{1,0,0}; //Compliant
    largeInteger tinyUnit{0,1,0}; //Noncompliant
    if(compareValues (largeUnit, smallUnit)) {
        //Perform some action   
    }
}