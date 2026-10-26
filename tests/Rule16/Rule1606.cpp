// MISRA C++:2008 Rule 16-0-6
// In the definition of a function-like macro, each instance of a parameter shall be enclosed in parentheses, unless it is used as the operand of # or ##

// Flags function-like macro definitions if the parameters are not enclosed in parenthesis. 
// Do not flag unparenthesized parameters if they are preceded by the operators ., ->, or the characters #, ##.

#include<iostream>
#include<cmath>
#define abs(x) (x>0) ? x:-x //Noncompliant

double foo(double num1, double num2){
	return log(abs(num1-num2));
}

int main(){
	std::cout<<foo(10,10.5);
}
