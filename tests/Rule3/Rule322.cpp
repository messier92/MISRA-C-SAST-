// MISRA C++:2008 Rule 3-2-2
// The One Definition Rule shall not be violated

// The checker flags situations where the same function or object has multiple definitions and the definitions differ by some token. The checker is not raised on unused code such as
// Noninstantiated templates
// Uncalled static or extern functions
// Uncalled and undefined local functions
// Unused types and variables

typedef struct S //Noncompliant
{
   int x;
   int y;
}S; 
void foo(S& s){
//...
} 

typedef struct S 
{
   int y;
   int x;
}S ; 
void bar(S& s){
//...
}