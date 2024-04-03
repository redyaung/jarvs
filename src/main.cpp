#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <vector>
#include <string>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/table.hpp>
#include <ftxui/screen/screen.hpp>
#include "assembler.hpp"
#include "processor.hpp"

using namespace ftxui;
using namespace std::chrono_literals;
using namespace std::string_literals;

Element render(RegisterFile<RegisterType::Integer> regFile) {
  Elements elements;
  for (auto num = 0; num < registerCount; ++num) {
    std::string name = "x" + std::to_string(num);
    std::string val = std::to_string(regFile.regs[num]);
    elements.push_back(hbox(
      text(name) | size(WIDTH, EQUAL, 5), 
      text(val) | align_right | color(Color::GrayDark) | size(WIDTH, EQUAL, 3),
      text("     ")
    ));
  }
  return hflow(elements);
}

Element renderInstructions(
  const PipelinedProcessor &cpu, 
  std::vector<std::string> readableInstructions
) {
  std::vector<std::vector<Element>> tableEntries;
  for (auto idx = 0; idx < readableInstructions.size(); idx++) {
    auto curPc = idx * 4;
    std::vector<Element> curRow;
    curRow.push_back(text(std::to_string(idx)) | size(WIDTH, EQUAL, 3) | align_right);
    curRow.push_back(text(readableInstructions[idx]) | size(WIDTH, EQUAL, 30));

    std::string _stage = 
      (cpu.clockCycle == 0) ? " " :     // for the case when clock cycle = 0
      (cpu.issueUnit.out.pcOut.val == curPc) ? "IF" :
      (cpu.IF_ID.pcOut.val == curPc) ? "ID" :
      (cpu.ID_EX.pcOut.val == curPc) ? "EX" :
      (cpu.EX_MEM.pcOut.val == curPc) ? "MEM" :
      (cpu.MEM_WB.out.pcOut.val == curPc) ? "WB" : " ";

    std::string stage;
    bool isNop = false;
    if (cpu.clockCycle == 0) {    // for the case when clock cycle = 0
      stage = " ";
    } else if (cpu.issueUnit.out.pcOut.val == curPc) {
      stage = "IF";
    } else if (cpu.IF_ID.pcOut.val == curPc) {
      stage = "ID";
      isNop = cpu.IF_ID.instructionOut.val == 0x0;
    } else if (cpu.ID_EX.pcOut.val == curPc) {
      stage = "EX";
      isNop = cpu.ID_EX.instructionOut.val == 0x0;
    } else if (cpu.EX_MEM.pcOut.val == curPc) {
      stage = "MEM";
      isNop = cpu.EX_MEM.instructionOut.val == 0x0;
    } else if (cpu.MEM_WB.out.pcOut.val == curPc) {
      stage = "WB";
      isNop = cpu.MEM_WB.out.instructionOut.val == 0x0;
    } else {
      stage = " ";
    }

    Element stageTxt = text(stage);
    if (isNop) {
      stageTxt |= bold;
    }
    curRow.push_back(stageTxt | size(WIDTH, EQUAL, 5) | center);
    tableEntries.push_back(curRow);
  }
  Table table(tableEntries);
  table.SelectAll().Border();
  table.SelectColumn(0).Border();
  table.SelectColumn(1).Border();
  table.SelectColumn(2).Border();
  return table.Render();
}

Element render(MainMemory<8> mainMem) {
  auto fmtEntry = [](std::string content, int width = 5) {
    return hbox(
      text(" "),
      text(content) | align_right | size(WIDTH, EQUAL, width),
      text(" ")
    );
  };

  std::vector<std::vector<Element>> tableEntries;
  std::vector<Element> firstRow;
  firstRow.push_back(fmtEntry(" ", 5));
  for (auto col = 0; col < 8; ++col) {
    firstRow.push_back(fmtEntry("+" + std::to_string(col * 4)) | bold);
  }
  tableEntries.push_back(firstRow);

  // 1 row = 8 words (32 bytes); each entry = 1 word (4 bytes)
  for (auto row = 0; row < 8; ++row) {
    std::vector<Element> curRow;
    curRow.push_back(fmtEntry(std::to_string(row * 32), 5) | bold);
    for (auto col = 0; col < 8; ++col) {
      auto addr = row * 32 + col * 4;
      int32_t val = mainMem.readBlock<1>(addr)[0];
      curRow.push_back(fmtEntry(std::to_string(val)));
    }
    tableEntries.push_back(curRow);
  }

  Table table(tableEntries);
  table.SelectAll().Border();
  table.SelectRow(0).Border();
  for (auto col = 0; col < 8; ++col) {
    table.SelectColumn(col).Border();
  }
  return table.Render();
}

int main(int argc, const char *argv[]) {
  if (argc < 2) {
    std::cerr << "usage: " << argv[0] << " [file] [opt:forwarding]" << std::endl;
    return 1;
  }
  std::string asmFileName(argv[1]);
  bool useForwarding = (argc == 3) ? std::stoi(argv[2]) : true;

  std::ifstream asmStream(asmFileName);
  auto instructions = encodeInstructions(asmStream);

  // read the file again (todo: inelegant)
  asmStream = std::ifstream(asmFileName);
  std::vector<std::string> readableInstructions;
  for (std::string line; std::getline(asmStream, line); ) {
    // skip empty lines
    if (line == "") {
      continue;
    }
    readableInstructions.push_back(line);
  }
 
  PipelinedProcessor cpu(useForwarding);

  // register instructions into instruction memory
  for (auto i = 0; i < instructions.size(); ++i) {
    cpu.instructionMemory.memory.writeBlock(i * 4, Block<1>{instructions[i]});
  }

  PipelinedProcessor initialCpu(cpu);     // used for reset

  std::string statsStr = "forwarding = "s + (useForwarding ? "on" : "off");
  std::string _resetPosition;
  while (true) {
    std::string clockCycleStr = "Clock cycle: " + std::to_string(cpu.clockCycle);

    auto document = vbox(
      window(
        text("Registers"),
        render(cpu.registers.intRegs) | center
      ),
      hbox(
        window(
          text("Instructions"),
          renderInstructions(cpu, readableInstructions)
        ),
        window(
          text("Memory (RAM)"),
          render(cpu.dataMemory.memory)
        ) | flex
      ),
      filler(),
      window(
        text("Settings"),
        text(statsStr)
      ),
      text(clockCycleStr) | bold | border,
      text("Press enter to step a single cycle, r to reset, q to quit: ")
    );

    auto screen = Screen::Create(Dimension::Full(), Dimension::Full());
    Render(screen, document.get());

    // see https://github.com/chximn/CPU/blob/master/src/ui/ui.cc#L334
    std::cout << _resetPosition << "\r" << "\x1B[2K" << "\x1B[1A" << "\x1B[2K";

    std::cout << screen.ToString() << std::flush;
    _resetPosition = screen.ResetPosition();

    std::string _input;
    std::getline(std::cin, _input);
    if (_input == "") {   // enter
      cpu.executeOneCycle();
    } else if (_input == "r") {   // reset
      cpu = initialCpu;
      continue;
    } else if (_input == "q") {    // quit
      break;
    } else {
      continue;
    }
  }

  return 0;
}
