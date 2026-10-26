// MISRA C++:2008 Rule 0-1-9
// There shall be no dead code

// Reports violation of this rule on statements that have no effect on the code, such as writing a value to a variable that is not used subsequently.

#define ULIM 10000

int func(int arg) {
    int res;
    res = arg*arg + arg;
    if (res > ULIM)
        res = 0; //Noncompliant
    return arg;
}