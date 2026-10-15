/*
 * pebble_compat.h - ESP32-C6 Variante des Pebble-API-Shims.
 *
 * Stellt exakt die Symbole bereit die der T7Emu-Emulator-Kern erwartet,
 * damit t7.c / t7_cpu.c / t7_memory.c / t7_eeprom.c
 * unveraendert kompilieren:
 *
 *   - APP_LOG   -> tama_log() (Serial/USB-CDC)
 *   - ResHandle + resource_*  -> ROM aus LittleFS-Datei "/t7emu.bin"
 *   - persist_* -> NVS (Preferences), 16 Pages a 256 Byte
 *
 * Implementierung in pebble_compat.cpp (braucht C++ wegen LittleFS/Preferences).
 * Header ist sowohl von C (Kern) als auch C++ (compat/main) inkludierbar.
 */

#ifndef PEBBLE_COMPAT_H
#define PEBBLE_COMPAT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#ifdef T7_FREESTANDING
/* Paradise: no C library, only these from libc.c */
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
#else
#include <stdlib.h>   // malloc/free  (Core nutzt sie ueber diesen Header)
#include <string.h>   // memset/memcpy/strncpy
#include <stdio.h>    // snprintf
#include <time.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// --- Logging --------------------------------------------------------------
#define APP_LOG_LEVEL_ERROR    0
#define APP_LOG_LEVEL_WARNING  1
#define APP_LOG_LEVEL_INFO     2
#define APP_LOG_LEVEL_DEBUG    3

void tama_log(const char *fmt, ...);
#define APP_LOG(level, fmt, ...) tama_log("[tama] " fmt "\n", ##__VA_ARGS__)

// --- Resource-API (ROM) ---------------------------------------------------
typedef int ResHandle;

// Vor t7_init() aufrufen. Pfad der ROM-Datei im LittleFS.
void   t7_compat_set_rom_path(const char *path);
// Vor t7_init() aufrufen. NVS-Namespace fuer das Save-EEPROM.
void   t7_compat_set_eeprom_path(const char *ns);

ResHandle resource_get_handle(int id);
size_t    resource_size(ResHandle h);
size_t    resource_load_byte_range(ResHandle h, size_t offset,
                                   uint8_t *out, size_t len);

// Direktzeiger auf den memory-mapped ROM (XIP-Flash) oder NULL.
// Erlaubt zero-copy ROM-Banking (Page-Table zeigt direkt in den Flash).
const uint8_t *t7_compat_rom_mapped(void);

#define RESOURCE_ID_TAMA_ROM 1

// --- Persist-API (Save-EEPROM) --------------------------------------------
typedef int status_t;
#define S_SUCCESS 0

bool      persist_exists(uint32_t key);
int       persist_read_data(uint32_t key, void *buf, size_t size);
status_t  persist_write_data(uint32_t key, const void *buf, size_t size);
status_t  persist_delete(uint32_t key);

// Schreibt dirty Pages nach NVS. Periodisch + vor Sleep/Shutdown aufrufen.
void      persist_flush_to_disk(void);
int       persist_page_count(void);
int       persist_page_size(void);
int       persist_backup_page(int page, uint8_t *out);
void      persist_restore_page(int page, const uint8_t *in);

#ifdef __cplusplus
}
#endif

#endif // PEBBLE_COMPAT_H
