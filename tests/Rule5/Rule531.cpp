// MISRA C++:2008 Rule 5-3-1
// Each operand of the ! operator, the logical && or the logical || operators shall have type bool

// The checker reports a violation if the operands of the logical operators !, && or ||, are not effectively boolean. Allowed operands are:
// Variables of type bool or specified as effectively boolean using the option Effective boolean types (-boolean-types).
// Expressions that compare two variables using operators such as == or < and return a boolean.

#include <cstdint>

class Pair
{
    int32_t leftVal;
    int32_t rightVal;
public:
    bool operator==(const Pair& anotherPair) {
        return(((*this).leftVal == anotherPair.leftVal)
             &&((*this).rightVal == anotherPair.rightVal));
    }
};

Pair getAPair();
int32_t checkPair(const Pair*);

void main() {
    Pair aPair = getAPair();
    Pair anotherPair = getAPair();
    Pair athirdPair = getAPair();
    
    if((aPair == anotherPair) &&
       (aPair == athirdPair)) //Compliant
       {}
    else if((aPair == anotherPair) && //Noncompliant
            (checkPair(&athirdPair))) 
       {} 
}