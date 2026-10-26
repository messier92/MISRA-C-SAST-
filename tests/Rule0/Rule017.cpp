// MISRA C++:2008 Rule 0-1-7
// The value returned by a function having a non- void return type that is not an overloaded operator shall always be used

// The checker flags functions with non-void return if the return value is not used or not explicitly cast to a void type.
// The checker does not flag the functions memcpy, memset, memmove, strcpy, strncpy, strcat, strncat because these functions simply return a pointer to their first arguments.

#include <iostream>
#include <new>
#include <algorithm>
#include <cstdint>
#include <vector>

int assignMemory(int * ptr)
{
    int res = 1;
    ptr = new (std::nothrow) int;
    if(ptr==NULL)
        {
            res = 0;
        }
    return res;
}

void main()
{
    int val;
    int status;
    std::vector<std::int8_t> numVec{10,10,20,20,30,40,50,50,60};

    assignMemory(&val);   //Noncompliant
    status = assignMemory(&val); //Compliant
    (void)assignMemory(&val); //Compliant

    numVec.erase(std::unique(numVec.begin(), numVec.end()), numVec.end());// Noncompliant
}