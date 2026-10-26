// MISRA C++:2008 Rule 5-0-8
// An explicit integral or floating-point conversion shall not increase the size of the underlying type of a cvalue expression

// In this example, the first cast on the sum violates the rule but the second cast does not.
// The first cast can lead to the incorrect expectation that the sum is evaluated with an underlying type int.
// The second cast first converts one of the operands to int so that the sum is actually evaluated with the underlying type int.

void func() {
     short op1;
     short op2;
     int res;

     res = static_cast<int> (op1 + op2); //Noncompliant
     res = static_cast<int> (op1) + op2; //Compliant

}
