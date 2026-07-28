// main.cpp — AY2D test runner entry point.
//
// Mirrors AYPhysics/unittest/main.cpp: defers to AYTest's runAllTests
// runner. No custom flags / fixtures exist yet (Phase 1+ stub state).
//
// Note: ARGV/ENVP are accepted-but-ignored; AYTest's runAllTests takes
// them by reference but does not parse them in Phase 1. Future phases
// (e.g. hot-reload tests) WILL want access to the build dir; see
// design.md §6.2.1 + F-11 hot-reload policy.

#include "AYTest.h"

int main(int /*argc*/, char** /*argv*/) {
    return ayt::test::runAllTests("AY2D");
}
