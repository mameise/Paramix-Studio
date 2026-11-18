# Flash app

Our own program for reading and writing the Paradise flash over UART. The
Studio loads it into the device's RAM from the download screen (the device's
own app loading, no P-COM, nothing is written to flash for this). Switching the
device off and on returns to the normal firmware.

- `flashapp.bin`: the program (runs from PRAM 0, 64 KB window).
- `src/fw.c`: source. Talks the PAU1 protocol on UART1 at 460800 baud and shows
  its own screen with a progress bar.
- Device status labels are English: READY, READING, WRITING, and DONE.


## Safe reset

When HOLD C TO RESET appears, hold C for three RTC ticks (about three seconds),
then release after RELEASE C appears. A fresh release is required after entry
or any host activity. This uses WDT0 to restart through Boot ROM; SYSRESETREQ
would instead restart the current PRAM application. Reset itself writes no flash.

Every received UART byte disarms reset. New Studio clients explicitly lock it
across preparation, reading, writing and verification, and re-arm only after
success. WRITE_BEGIN/DATA/COMMIT latch an incomplete write until WRITE_FINISH;
HELLO and CANCEL cannot clear it. Failed or interrupted writes stay locked.
A structural SONIXDEV boot-header check and an idle flash controller are also
required; this is not a CRC validation of encrypted boot code. Older clients
remain usable but cannot re-arm reset after traffic.

Status artwork is packed into one-bit masks with its original foreground colors;
`test/flash_assets_test.py` checks that the original pixels remain unchanged.
The watchdog reset sequence still requires physical-device validation.

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
| 11 | WRITE_COMMIT | CRC-32 of the sector; erases, programs, compares; answer: CRC and error flag (FW_PRECHECK: the buffer is checked against the CRC first, error 2 = nothing written, send again) |
| 12 | WRITE_FINISH | end |
| 13 | WRITE_STATE | writer state |
| 16 | RESET_CONTROL | offset 0; LE u32 0 = lock, 1 = safe idle; status 0 accepted, 1 refused |

HELLO bytes 40–43 contain capability bits; bit 0 advertises RESET_CONTROL.
Clients must complete all write verification before WRITE_FINISH and keep reset
locked until the entire job succeeds.

The host side is `src/flash.js` in the Studio.

## Build

```
python3 src/gen_status.py  # regenerate status bitmaps after changing their labels
sh src/build.sh            # builds flashapp.bin (at most 0xE000 bytes)
```

License: GNU GPL version 3 or later.
