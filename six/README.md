# More fields on Tropical, Glacier and Jade Forest

`six.bin` is one page of helper code that the Studio writes to flash 0x10F000
(XIP 0x100FE000) for "All 6 fields" (Tropical, Glacier) or "Sky as 4th field"
(Jade Forest). Source: `src/six.s`.

Build:

```
sh src/build.sh      # clang and ld.lld (LLVM), writes six.bin
```

The firmware keeps the active field slot as 0, 1 or 2 in the settings block
(byte 499 on Tropical/Glacier, 443 on Jade Forest). The Studio adds a set byte
(offset 0xF04): 0 means the original fields, 1 means the alternative ones
(Land, Water, Sky in the three slots on Tropical/Glacier; Sky in slot 1 on
Jade Forest). The firmware is changed in these places (offsets in the
firmware window, flash = offset + 0x11000):

- the function that returns the background composites of a field
  (Tropical 0xAEAC, Jade 0x966C): jumps to `six_bg`,
- the child list in the code (Tropical 0xFD62, Jade 0xE1DA): `six_child`,
- every movw/movt pair that loads a per field table (teens, adults, travel
  backgrounds, field eggs, scenes, wagon): `six_tab_r0` / `six_tab_r1`. The
  site list at 0x380 (return address, original table, alternative table)
  picks the table by the set byte.

All addresses and the alternative tables are filled in by the Studio
(`src/six.js`). On Tropical/Glacier the Land, Water and Sky field pictures
and scenes are copied from the user's own Land, Water or Sky ROM; the cells
of the rain pictures that the clear ones do not share lie from 0xF00000
(header "SIXB"). Jade Forest already holds Sky and gets only the tables.
Nothing of any ROM is part of this repository.
