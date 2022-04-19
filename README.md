# SG14
[![Build Status](https://travis-ci.org/WG21-SG14/SG14.svg?branch=master)](https://travis-ci.org/WG21-SG14/SG14)

A library for Study Group 14 of Working Group 21 (C++)

/docs - Documentation for implementations without proposals, or supplementary documentation which does not easily fit within a proposal.

/docs/proposals - C++ standard proposals.

/include - Source files for implementations.

/test - Individual tests for implementations.

http://lists.isocpp.org/mailman/listinfo.cgi/sg14 for more information

## Build Instructions
Clone the repo. Navigate to the folder in your favorite terminal.

`mkdir build && cd build`

### Windows
`cmake .. -A x64 && cmake --build . && bin\utest.exe`

### Unixes
`cmake .. && cmake --build . && ./bin/utest`
