// MISRA C++:2008 Rule 2-10-2
// Identifiers declared in an inner scope shall not hide an identifier declared in an outer scope

// The rule checker flags all cases of variable shadowing including when:
// The same identifier name is used in an outer and inner named namespace.
// The same name is used for a class data member and a variable outside the class.
// The same name is used for a method in a base and derived class.
// To exclude these cases, switch to the more modern standard AUTOSAR C++14 and check for the rule AUTOSAR C++14 Rule A2-10-1.

int varInit = 1;

void doSomething(void);

void step(void) {
    int varInit = 0; //Noncompliant
    if(varInit)
       doSomething(); 
}

void runSomeCheck(int);

void checkMatrix(int dim1, int dim2) {
  for(int index = 0; index < dim1; index++) {
      for(int index = 0; index < dim2; index++) { // Noncompliant
          runSomeCheck(index);
      }
  }
}