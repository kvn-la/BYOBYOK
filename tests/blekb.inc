// ==== BLEKB-TYPES BEGIN ==== (extracted verbatim into tests/ - keep in sync)
struct KeyEvt { uint8_t ascii, keycode, modifiers; bool caps; };
struct BleKbState { uint8_t mods = 0; uint8_t keys[6] = {0, 0, 0, 0, 0, 0}; bool caps = false; };
// ==== BLEKB-TYPES END ====
// ==== BLEKB-CORE BEGIN ==== (extracted verbatim into tests/ - keep in sync)
// BLE HID keyboards hand us raw input reports; unlike EspUsbHost there's no
// library doing the keycode->character work, so we decode them ourselves.
// (KeyEvt / BleKbState are defined near the top, in the BLEKB-TYPES block.)

// HID usage -> ASCII, US layout. Letters honor shift^caps; symbols shift only.
char hidToAscii(uint8_t kc, bool shift, bool caps) {
  if (kc >= 0x04 && kc <= 0x1D) {                 // a..z
    char c = (char)('a' + (kc - 0x04));
    return (shift != caps) ? (char)(c - 32) : c;
  }
  if (kc >= 0x1E && kc <= 0x27) {                 // 1..9,0
    static const char base[] = "1234567890";
    static const char shf[]  = "!@#$%^&*()";
    return shift ? shf[kc - 0x1E] : base[kc - 0x1E];
  }
  switch (kc) {
    case 0x28: return 13;                         // Enter
    case 0x2A: return 8;                          // Backspace
    case 0x2B: return 9;                          // Tab
    case 0x2C: return ' ';
    case 0x2D: return shift ? '_'  : '-';
    case 0x2E: return shift ? '+'  : '=';
    case 0x2F: return shift ? '{'  : '[';
    case 0x30: return shift ? '}'  : ']';
    case 0x31: return shift ? '|'  : '\\';
    case 0x33: return shift ? ':'  : ';';
    case 0x34: return shift ? '"'  : '\'';
    case 0x35: return shift ? '~'  : '`';
    case 0x36: return shift ? '<'  : ',';
    case 0x37: return shift ? '>'  : '.';
    case 0x38: return shift ? '?'  : '/';
  }
  return 0;                                       // arrows/F-keys...: keycode only
}

// A keyboard input report lists the keys *currently held* (standard 8-byte
// format: [modifiers, reserved, k1..k6]). Diffing against the previous report
// yields the newly pressed keys. Returns the number of KeyEvt produced.
int bleKbDecode(BleKbState &st, const uint8_t *rep, int len, KeyEvt *out, int maxOut) {
  if (!rep || len < 8) return 0;
  const uint8_t *keys = rep + 2;
  for (int i = 0; i < 6; i++)
    if (keys[i] == 0x01) return 0;                // phantom/rollover: ignore whole report
  uint8_t mods = rep[0];
  int nOut = 0;
  for (int i = 0; i < 6; i++) {
    uint8_t kc = keys[i];
    if (kc < 0x04) continue;                      // 0 = no key
    bool isNew = true;
    for (int j = 0; j < 6; j++) if (st.keys[j] == kc) { isNew = false; break; }
    if (!isNew) continue;
    if (kc == 0x39) { st.caps = !st.caps; continue; }   // CapsLock toggles, no event
    if (nOut < maxOut) {
      bool shift = (mods & 0x22) != 0;            // left/right shift
      KeyEvt e;
      e.ascii = (uint8_t)hidToAscii(kc, shift, st.caps);
      e.keycode = kc;
      e.modifiers = mods;
      e.caps = st.caps;
      out[nOut++] = e;
    }
  }
  st.mods = mods;
  memcpy(st.keys, keys, 6);
  return nOut;
}
// ==== BLEKB-CORE END ====
