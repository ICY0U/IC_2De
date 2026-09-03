// The single translation unit that supplies doctest's entry point.
//
// Every test executable compiles this file, so no test file needs a main() of
// its own. That is what lets one executable hold several test files: previously
// each carried its own main and its own failure counter, so pairing two files
// in one target meant one of them calling into the other.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
