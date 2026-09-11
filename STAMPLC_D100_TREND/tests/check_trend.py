"""Run with Python and a C++17 compiler: python tests/check_trend.py g++"""
from pathlib import Path
import subprocess
import sys
import tempfile

source = (Path(__file__).resolve().parents[1] / "src/main.cpp").read_text(encoding="utf-8")
history = source[source.index("constexpr unsigned HISTORY_SIZE"):source.index("// LCDは")]
display = source[source.index("void showDisplay("):source.index("// 異常時は")]
append = source[source.index("  history[historyHead] ="):source.index("  showResultLed(true);", source.index("void onRead("))]
stub = r'''
#include <cassert>
#include <cstdint>
#include <vector>
constexpr int TFT_BLACK=0,TFT_WHITE=1,TFT_RED=2,TFT_DARKGREY=3,TFT_CYAN=4;
struct Point { int x, y; };
struct LCD {
  std::vector<Point> points;
  void fillScreen(int) { points.clear(); }
  void setTextColor(int, int) {}
  void setTextSize(int) {}
  void setCursor(int, int) {}
  template<class... T> void printf(const char*, T...) {}
  void print(const char*) {}
  void drawRect(int,int,int,int,int) {}
  void drawFastHLine(int,int,int,int) {}
  void drawPixel(int x,int y,int) { points.push_back({x,y}); }
  void drawLine(int,int,int x,int y,int) { points.push_back({x,y}); }
};
struct { LCD Display; } M5StamPLC;
int16_t d100 = 0;
'''
checks = r'''
int main() {
  showDisplay(true);
  assert(M5StamPLC.Display.points.empty());
  add(0); showDisplay(true);
  assert(M5StamPLC.Display.points.back().x == 238);
  assert(M5StamPLC.Display.points.back().y == 67);
  add(-32768); add(32767); showDisplay(true);
  assert(M5StamPLC.Display.points[1].y == 104);
  assert(M5StamPLC.Display.points[2].y == 29);
  for(int i=0;i<120;++i) add(42);
  assert(historyCount == 120);
  showDisplay(false);
  assert(M5StamPLC.Display.points.size() == 120);
  assert(M5StamPLC.Display.points.front().x == 1);
  for(auto p:M5StamPLC.Display.points) assert(p.y == 67 && p.x >= 1 && p.x <= 238);
  historyCount = historyHead = 0;
  showDisplay(true);
  assert(M5StamPLC.Display.points.empty());
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / "check.cpp"
    exe = Path(directory) / "check.exe"
    cpp.write_text(stub + history + display + "\nvoid add(int16_t value) { d100=value;\n" + append + "}\n" + checks, encoding="utf-8")
    subprocess.run([sys.argv[1] if len(sys.argv) > 1 else "g++", "-std=c++17", str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: empty, constant, signed extremes, wraparound, clear")
