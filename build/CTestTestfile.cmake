# CMake generated Testfile for 
# Source directory: C:/Users/HanLo/Projects/misra
# Build directory: C:/Users/HanLo/Projects/misra/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test([=[ViolationStorePrecedenceTest]=] "C:/Users/HanLo/Projects/misra/build/Release/Debug/violation_store_test.exe")
  set_tests_properties([=[ViolationStorePrecedenceTest]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/HanLo/Projects/misra/CMakeLists.txt;143;add_test;C:/Users/HanLo/Projects/misra/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test([=[ViolationStorePrecedenceTest]=] "C:/Users/HanLo/Projects/misra/build/Release/Release/violation_store_test.exe")
  set_tests_properties([=[ViolationStorePrecedenceTest]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/HanLo/Projects/misra/CMakeLists.txt;143;add_test;C:/Users/HanLo/Projects/misra/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test([=[ViolationStorePrecedenceTest]=] "C:/Users/HanLo/Projects/misra/build/Release/MinSizeRel/violation_store_test.exe")
  set_tests_properties([=[ViolationStorePrecedenceTest]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/HanLo/Projects/misra/CMakeLists.txt;143;add_test;C:/Users/HanLo/Projects/misra/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test([=[ViolationStorePrecedenceTest]=] "C:/Users/HanLo/Projects/misra/build/Release/RelWithDebInfo/violation_store_test.exe")
  set_tests_properties([=[ViolationStorePrecedenceTest]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/HanLo/Projects/misra/CMakeLists.txt;143;add_test;C:/Users/HanLo/Projects/misra/CMakeLists.txt;0;")
else()
  add_test([=[ViolationStorePrecedenceTest]=] NOT_AVAILABLE)
endif()
