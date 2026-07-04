// Host-side tests for PaperType's BLE keyboard report decoder (BLEKB-CORE).
//
// The tested code is extracted VERBATIM from PaperType.ino - regenerate with:
//   sed -n '/BLEKB-TYPES BEGIN/,/BLEKB-TYPES END/p; /BLEKB-CORE BEGIN/,/BLEKB-CORE END/p' ../BYOBYOK/BYOBYOK.ino > blekb.inc
//
// Build & run:
//   g++ -std=c++17 -O1 -g -fsanitize=address,undefined -o blekb_test blekb_test.cpp
//   ./blekb_test

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "blekb.inc"

static int fails = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { \
  printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

// Build a standard 8-byte report ([mods, reserved, k1..k6]) and decode it.
static int report(BleKbState &st, uint8_t mods, std::initializer_list<uint8_t> keys,
                  KeyEvt *out, int maxOut = 8) {
  uint8_t rep[8] = { mods, 0, 0, 0, 0, 0, 0, 0 };
  int i = 2;
  for (uint8_t k : keys) if (i < 8) rep[i++] = k;
  return bleKbDecode(st, rep, 8, out, maxOut);
}

int main() {
  KeyEvt ev[8];

  // -- hidToAscii spot checks ------------------------------------------------
  CHECK(hidToAscii(0x04, false, false) == 'a');
  CHECK(hidToAscii(0x04, true,  false) == 'A');
  CHECK(hidToAscii(0x04, false, true)  == 'A');   // caps
  CHECK(hidToAscii(0x04, true,  true)  == 'a');   // shift undoes caps
  CHECK(hidToAscii(0x1D, false, false) == 'z');
  CHECK(hidToAscii(0x1E, false, false) == '1');
  CHECK(hidToAscii(0x1E, true,  false) == '!');
  CHECK(hidToAscii(0x27, false, false) == '0');
  CHECK(hidToAscii(0x27, true,  false) == ')');
  CHECK(hidToAscii(0x27, false, true)  == '0');   // caps must not shift digits
  CHECK(hidToAscii(0x28, false, false) == 13);    // Enter
  CHECK(hidToAscii(0x2A, false, false) == 8);     // Backspace
  CHECK(hidToAscii(0x2B, false, false) == 9);     // Tab
  CHECK(hidToAscii(0x2C, false, false) == ' ');
  CHECK(hidToAscii(0x2D, true,  false) == '_');
  CHECK(hidToAscii(0x34, false, false) == '\'');
  CHECK(hidToAscii(0x34, true,  false) == '"');
  CHECK(hidToAscii(0x38, true,  false) == '?');
  CHECK(hidToAscii(0x4F, false, false) == 0);     // Right arrow: keycode-only
  CHECK(hidToAscii(0x52, false, false) == 0);     // Up arrow

  // -- press / hold / release diffing ----------------------------------------
  BleKbState st;
  CHECK(report(st, 0, {0x04}, ev) == 1);          // press 'a'
  CHECK(ev[0].ascii == 'a' && ev[0].keycode == 0x04 && ev[0].modifiers == 0);
  CHECK(report(st, 0, {0x04}, ev) == 0);          // still held: no repeat event
  CHECK(report(st, 0, {0x04, 0x05}, ev) == 1);    // add 'b' while holding 'a'
  CHECK(ev[0].ascii == 'b');
  CHECK(report(st, 0, {0x05}, ev) == 0);          // release 'a': nothing new
  CHECK(report(st, 0, {}, ev) == 0);              // release all
  CHECK(report(st, 0, {0x04}, ev) == 1);          // 'a' again after release

  // -- modifiers ---------------------------------------------------------------
  st = BleKbState();
  CHECK(report(st, 0x02, {0x04}, ev) == 1);       // LShift+a
  CHECK(ev[0].ascii == 'A' && ev[0].modifiers == 0x02);
  st = BleKbState();
  CHECK(report(st, 0x20, {0x1E}, ev) == 1);       // RShift+1
  CHECK(ev[0].ascii == '!');
  st = BleKbState();
  CHECK(report(st, 0x01, {0x16}, ev) == 1);       // Ctrl+s (quick save path)
  CHECK(ev[0].keycode == 0x16 && (ev[0].modifiers & 0x11));

  // -- caps lock tracking ------------------------------------------------------
  st = BleKbState();
  CHECK(report(st, 0, {0x39}, ev) == 0);          // CapsLock: no event, toggles
  CHECK(report(st, 0, {}, ev) == 0);
  CHECK(report(st, 0, {0x04}, ev) == 1);
  CHECK(ev[0].ascii == 'A' && ev[0].caps);        // caps applies to letters
  CHECK(report(st, 0, {}, ev) == 0);
  CHECK(report(st, 0x02, {0x04}, ev) == 1);
  CHECK(ev[0].ascii == 'a');                      // shift+caps = lowercase
  CHECK(report(st, 0x02, {0x39, 0x04}, ev) == 0); // caps off again, 'a' still held
  CHECK(report(st, 0, {}, ev) == 0);
  CHECK(report(st, 0, {0x04}, ev) == 1);
  CHECK(ev[0].ascii == 'a' && !ev[0].caps);

  // -- multiple new keys in one report (fast rollover) -------------------------
  st = BleKbState();
  CHECK(report(st, 0, {0x04, 0x05, 0x06}, ev) == 3);
  CHECK(ev[0].ascii == 'a' && ev[1].ascii == 'b' && ev[2].ascii == 'c');

  // -- phantom / error rollover reports ignored, state preserved ---------------
  st = BleKbState();
  CHECK(report(st, 0, {0x04}, ev) == 1);
  CHECK(report(st, 0, {0x01, 0x01, 0x01, 0x01, 0x01, 0x01}, ev) == 0);
  CHECK(report(st, 0, {0x04}, ev) == 0);          // 'a' was never released

  // -- malformed input ----------------------------------------------------------
  st = BleKbState();
  uint8_t shortRep[4] = { 0, 0, 0x04, 0 };
  CHECK(bleKbDecode(st, shortRep, 4, ev, 8) == 0);   // too short: ignored
  CHECK(bleKbDecode(st, nullptr, 8, ev, 8) == 0);
  uint8_t longRep[10] = { 0, 0, 0x04, 0, 0, 0, 0, 0, 0x99, 0x99 };
  CHECK(bleKbDecode(st, longRep, 10, ev, 8) == 1);   // extra bytes tolerated
  CHECK(ev[0].ascii == 'a');

  // -- maxOut respected ---------------------------------------------------------
  st = BleKbState();
  CHECK(report(st, 0, {0x04, 0x05, 0x06, 0x07}, ev, 2) == 2);

  if (fails == 0) printf("ALL BLE DECODER TESTS PASSED (%d checks)\n", checks);
  return fails ? 1 : 0;
}
