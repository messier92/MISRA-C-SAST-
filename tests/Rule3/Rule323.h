#ifndef RULE323_H
#define RULE323_H

// MISRA C++:2008 Rule 3-2-3
// A type, object or function that is used in multiple translation units
// shall be declared in one and only one file.
//
// The checker reports violations when an extern object is declared
// in multiple files.

extern int systemStatus;
extern unsigned int messageCount;

namespace Diagnostics
{
    extern int errorCode;
}

#endif // RULE323_H