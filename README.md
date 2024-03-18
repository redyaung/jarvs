# just another risc-v simulator

## building
1. go to the root directory in the project
2. run `cmake -S . -B build` to generate build files
3. run `cmake --build build` to build your targets

## testing
1. complete the steps in the section "building"
2. run `ctest --test-dir build`
3. to selectively run a test suite, pass the `--gtest_filter` flag to the test executable
directly instead of using `ctest`. (`ctest` doesn't support this sadly).
    - a caveat is to *prepend the asterisks `*` with a backslash* to avoid funky zsh problems.
    - for example, if you want to run only pipelined processor tests, you would run
    `./build/simulator_test --gtest_filter=Pipelined\*`
