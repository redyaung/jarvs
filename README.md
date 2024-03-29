# Just Another Risc-v Simulator

## Building
1. Go to the root directory in the project.
2. Run `cmake -S . -B build` to generate build files.
3. Run `cmake --build build` to build your targets.

## Testing
1. Complete the steps in the section "Building".
2. Run `ctest --test-dir build`.
3. To selectively run a test suite, pass the `--gtest_filter` flag to the test executable
directly instead of using `ctest`. (`ctest` doesn't support this sadly).
    - A caveat is to *prepend the asterisks `*` with a backslash* to avoid funky zsh problems.
    - For example, if you want to run only pipelined processor tests, you would run
    `./build/simulator_test --gtest_filter=Pipelined\*`.

## Development Notes

### Adding new instructions to assembler
1. Go to `src/assembler.cpp`.
2. Add the instruction to the `fixedFieldsLookup` map -- you will need the `opcode`, `func3`
and `func7` fields (if applicable).
3. Add the name of the instruction (e.g. `add`) to the appropriate `vector` containing the
instruction names -- one of `Rs`, `Is`, `Ss`, `SBs`, `Us`, `UJs`.

*Note: Adding new instructions to the processor doesn't end at the assembler. You will also*
*need to configure the processor to handle the instruction.*
