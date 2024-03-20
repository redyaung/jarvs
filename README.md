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

## development notes

### adding new instructions to assembler
- go to `src/assembler.cpp`
- add the instruction to the `fixedFieldsLookup` map -- you will need the `opcode`, `func3`
and `func7` fields (if applicable).
- add the name of the instruction (e.g. `add`) to the appropriate `vector` containing the
instruction names -- one of `Rs`, `Is`, `Ss`, `SBs`, `Us`, `UJs`.

*note: adding new instructions to the processor doesn't end at the assembler. you will also*
*need to configure the processor to handle the instruction.*
