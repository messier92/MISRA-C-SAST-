// MISRA C++:2008 Rule 2-10-1
// Different identifiers shall be typographically unambiguous

// Report a defect if two identifiers differ by a combination of ambiguous characters mentioned in the preceding list. The rule checker does not consider the fully qualified names of variables when checking this rule. The rule checker does not consider the scope of the identifiers. 
// Use MISRA C++:2008 Rule 2-10-1 to identify scope-specific identifier declaration violations.

void func(void) {
    int id1_numval;
    int id1_num_val;  //Non-compliant

    int id2_numval;
    int id2_numVal;   //Non-compliant

    int id3_lvalue;
    int id3_Ivalue;   //Non-compliant

    int id4_xyZ;
    int id4_xy2;      //Non-compliant

    int id5_zerO;
    int id5_zer0;     //Non-compliant

    int id6_rn;
    int id6_m;        //Non-compliant
}