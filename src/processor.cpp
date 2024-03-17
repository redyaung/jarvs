#include "processor.hpp"

OutputSignal& OutputSignal::operator>>(InputSignal &signal) {
  syncedInputs.push_back(&signal);
  return *this;
}

void OutputSignal::operator<<(Word newValue) {
  value = newValue;
  for (InputSignal *signal : syncedInputs) {
    signal->unit->notifyInputChange();
  }
}
