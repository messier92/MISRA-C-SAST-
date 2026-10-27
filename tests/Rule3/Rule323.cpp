#include "Rule323.h"

// MISRA C++:2008 Rule 3-2-3
// These external objects are already declared in Rule323.h.

extern int systemStatus; //Noncompliant

extern unsigned int messageCount; //Noncompliant

namespace Diagnostics
{
    extern int errorCode; //Noncompliant
}

// A separate object declared only in this source file.
// This should not be flagged by this test.
extern long processIdentifier;

void updateDiagnostics()
{
    systemStatus = 1;
    ++messageCount;
    Diagnostics::errorCode = 42;
}