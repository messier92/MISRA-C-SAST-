#include "Rule322.h"

// MISRA C++:2008 Rule 3-2-2
// The One Definition Rule shall not be violated.
//
// The checker flags multiple definitions of the same function or object
// when their definitions differ by at least one token.
// Unused code, including uninstantiated templates and uncalled functions,
// is not reported by this checker.

struct Packet
{
    int length;
    int id;
}; //Noncompliant

int calculateChecksum(const Packet& packet)
{
    return packet.id + packet.length;
}

int main()
{
    Packet packet{};
    return calculateChecksum(packet);
}