// MISRA C++:2008 Rule 0-1-1
// A project shall not contain unreachable code.
// Report a defect if a statement in your code is not reachable.

int func(int arg) {
 int temp = 0;
 switch(arg) {
     temp = arg; // Noncompliant
     case 1:
     {
         break;
     }
     default:
     {
         break;
     }
 }
 return arg;
 arg++; // Noncompliant
}