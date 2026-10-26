#include <iostream>

void testFunction() {
    int uninitialized_var; // Intentional: Uninitialized local variable
    std::cout << "Value: " << uninitialized_var << std::endl;

    double pi = 3.14159; // Magic number / style check example
}

int main() {
    testFunction();
    return 0;
}