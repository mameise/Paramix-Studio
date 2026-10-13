/* Entry point for the browser: like main.c of MCUDoomWadUtil, but in memory */
#include "wadfile.h"
#include "wadprocessor.h"
extern void heap_reset(void);
static size_t g_out_len;
__attribute__((export_name("out_len"))) size_t out_len(void){ return g_out_len; }
__attribute__((export_name("alloc"))) void *alloc(size_t n){ return malloc(n); }
__attribute__((export_name("reset"))) void reset(void){ heap_reset(); }
__attribute__((export_name("convert"))) uint8_t *convert(const uint8_t *wad, size_t wadLen, const uint8_t *gba, size_t gbaLen)
{
    wadfile_t wadfile, gbawadfile;
    g_out_len = 0;
    if (!loadWadMem(gba, gbaLen, &gbawadfile)) return 0;
    if (!loadWadMem(wad, wadLen, &wadfile)) return 0;
    mergeWadFile(&wadfile, &gbawadfile);
    if (!processWad(&wadfile, false, true)) return 0;
    return saveWadMem(&wadfile, 'I', &g_out_len);
}
