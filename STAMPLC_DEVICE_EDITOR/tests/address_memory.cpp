#include <cassert>
#include "editor.hpp"

int main() {
  editor::State state;
  assert(state.device().code == editor::DeviceCode::D && state.address == 100);
  for (int i = 0; i < 4; ++i) assert(state.select(-1)); // D → M
  assert(state.device().code == editor::DeviceCode::M && state.address == 0);
  for (int i = 0; i < 20; ++i) assert(state.moveAddress(1));
  for (int i = 0; i < 4; ++i) assert(state.select(1)); // M → D
  assert(state.address == 100);
  for (int i = 0; i < 4; ++i) assert(state.select(-1));
  assert(state.address == 20);
  state.received(state.generation, 0);
  assert(state.edit(1));
  assert(!state.select(1)); // 書き込み待ち中は切り替えない。
  assert(state.device().code == editor::DeviceCode::M && state.address == 20);
  state.written();
  const auto previousGeneration = state.generation;
  for (unsigned i = 0; i < editor::deviceCount; ++i) assert(state.select(1));
  assert(state.device().code == editor::DeviceCode::M && state.address == 20);
  state.received(previousGeneration, 99);
  assert(!state.valid); // 切り替え前の受信値は使わない。
  editor::State restarted;
  assert(restarted.address == 100);
  for (int i = 0; i < 4; ++i) assert(restarted.select(-1));
  assert(restarted.address == 0);
  state.received(state.generation, 0);
  assert(state.edit(1));
  const auto failedGeneration = state.generation;
  state.fail();
  assert(state.count == 0 && !state.valid && !state.edit(1));
  assert(state.address == 20); // 通信異常でも選択番地を保持。
  state.received(failedGeneration, 1);
  assert(!state.valid); // 異常前の値で書き込みを再開しない。
  state.received(state.generation, 0);
  assert(state.valid && state.value == 0 && state.count == 0);
  assert(state.edit(1)); // 読み直し成功後に新しい操作を受け付ける。
}
