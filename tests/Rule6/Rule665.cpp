// MISRA C++:2008 Rule 6-6-5
// A function shall have a single point of exit at the end of the function

// The checker flags these situations:
// A function has more than one return statement.
// A non-void function has one return statement only but the return statement is not the last statement in the function.
// A void function need not have a return statement. If a return statement exists, it need not be the last statement in the function.

