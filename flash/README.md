# Flash app

Our own program for reading and writing the Paradise flash over UART. The
Studio loads it into the device's RAM from the download screen (the device's
own app loading, no P-COM, nothing is written to flash for this). Switching the
device off and on returns to the normal firmware.

- `flashapp.bin`: the program (runs from PRAM 0, 64 KB window).
- `src/fw.c`: source. Talks the PAU1 protocol on UART1 at 460800 baud and shows
  its own screen with a progress bar.

## PAU1

Frames are COBS encoded and end with a zero byte. Inside: "PAU1", version,
flags, type, session, sequence, offset, payload length, status, payload,
CRC-32.

| Type | Command | Payload |
| --- | --- | --- |
| 1 | HELLO | answer: 44 bytes (edition, block size, JEDEC ID, capacity, flash error) |
| 5 | READ | length; answer: data |
| 6 | CRC | length; answer: CRC-32 of the range |
| 8 | WRITE_TEST | self test |
| 9 | WRITE_BEGIN | SHA-256 and CRC-32 of the image; answer: start sector |
| 10 | WRITE_DATA | data for the sector buffer |
| 11 | WRITE_COMMIT | CRC-32 of the sector; erases, programs, compares; answer: CRC and error flag |
| 12 | WRITE_FINISH | end |
| 13 | WRITE_STATE | writer state |

The host side is `src/flash.js` in the Studio.

## Build

```
sh src/build.sh     # clang, ld.lld, llvm-objcopy -> flashapp.bin (byte-identical)
```

License: GNU GPL version 3 or later.
