/*
   MCUDoomWadutil by Nicola Wrachien.

   Based on doomhack's GBAWadutil
   Original source: https://github.com/doomhack/GbaWadUtil

   This command line utility allows to convert your wad file
   to a format convenient for MCU-based Doom ports.

   Usage:
   MCUDoomWadutil <inputwad> <outputwad>
*/
#include "wadfile.h"
#include "doomtypes.h"
#include <string.h>

#define WAD_ALIGN 4

/* Browser: read the WAD from memory and write it to memory (instead of files) */
bool loadWadMem(const uint8_t *tmpBuffer, size_t sz, wadfile_t *wadfile)
{
    bool res = true;
    memcpy(&wadfile->header, tmpBuffer, sizeof (wadinfo_t));
    if (memcmp(&wadfile->header.identification[1], "WAD", 3) != 0 || (wadfile->header.identification[0] != 'I' && wadfile->header.identification[0] != 'P'))
        return false;
    filelump_t *fileLumpTable =  (filelump_t *) &tmpBuffer[wadfile->header.infotableofs];
    wadfile->lumps = malloc(wadfile->header.numlumps * sizeof(lump_t));
    lump_t *lumpTable = wadfile->lumps;
    for (int i = 0; i < wadfile->header.numlumps; i++)
    {
        size_t lumpSize = (fileLumpTable[i].size + (WAD_ALIGN - 1)) & ~(WAD_ALIGN - 1);
        lumpTable[i].lump.size = fileLumpTable[i].size;
        memcpy(lumpTable[i].lump.name, fileLumpTable[i].name, sizeof(lumpTable[i].lump.name));
        lumpTable[i].data = malloc(lumpSize);
        memset(lumpTable[i].data, 0, lumpSize);
        size_t avail = fileLumpTable[i].filepos < sz ? sz - fileLumpTable[i].filepos : 0;
        memcpy(lumpTable[i].data, &tmpBuffer[fileLumpTable[i].filepos], lumpSize < avail ? lumpSize : avail);
    }
    return res;
}
uint8_t *saveWadMem(wadfile_t *wadfile, char wadType, size_t *outLen)
{
    size_t total = sizeof(wadfile->header) + sizeof(filelump_t) * wadfile->header.numlumps;
    for (int i = 0; i < wadfile->header.numlumps; i++) total += (wadfile->lumps[i].lump.size + (WAD_ALIGN - 1)) & ~(WAD_ALIGN - 1);
    uint8_t *out = malloc(total), *p = out;
    wadfile->header.identification[0] = wadType;
    wadfile->header.identification[1] = 'W';
    wadfile->header.identification[2] = 'A';
    wadfile->header.identification[3] = 'D';
    wadfile->header.infotableofs = sizeof(wadfile->header);
    memcpy(p, &wadfile->header, sizeof(wadfile->header)); p += sizeof(wadfile->header);
    int ptr = wadfile->header.infotableofs + sizeof (filelump_t) * wadfile->header.numlumps;
    for (int i = 0; i < wadfile->header.numlumps; i++)
    {
        size_t size = (wadfile->lumps[i].lump.size + (WAD_ALIGN - 1)) & ~(WAD_ALIGN - 1);
        filelump_t fl;
        fl.filepos = ptr;
        fl.size = wadfile->lumps[i].lump.size;
        memcpy(fl.name, wadfile->lumps[i].lump.name, sizeof (wadfile->lumps[i].lump.name));
        memcpy(p, &fl, sizeof(fl)); p += sizeof(fl);
        ptr += size;
    }
    for (int i = 0; i < wadfile->header.numlumps; i++)
    {
        size_t size = (wadfile->lumps[i].lump.size + (WAD_ALIGN - 1)) & ~(WAD_ALIGN - 1);
        memcpy(p, wadfile->lumps[i].data, wadfile->lumps[i].lump.size);
        memset(p + wadfile->lumps[i].lump.size, 0, size - wadfile->lumps[i].lump.size); p += size;
    }
    *outLen = total;
    return out;
}
int getLumpNumByName(wadfile_t *wadfile, const char *name)
{
    for (int i = 0; i < wadfile->header.numlumps; i++)
    {
        if (0 == strncmp(wadfile->lumps[i].lump.name, name, sizeof(wadfile->lumps[i].lump.name)))
            return i;
    }
    return -1;
}
lump_t * getLumpByNum(wadfile_t *wadfile, int lumpNum)
{
    if (lumpNum >= wadfile->header.numlumps)
    {
        printf("Error, cannot find lum %d, max %d", lumpNum, wadfile->header.numlumps);
        return NULL;
    }
    else
        return &wadfile->lumps[lumpNum];
}
void replaceLump(wadfile_t *wadfile, lump_t *newLump, int index)
{
    if (index >= wadfile->header.numlumps)
    {
        //printf("Lump %d >= numLumps %d\r\n", index, wadfile->header.numlumps);
        return;
    }
    // free old lump data, not needed anymore
    free(wadfile->lumps[index].data);
    memcpy(&wadfile->lumps[index], newLump, sizeof(lump_t));
    //printf("new lump %d size %d\r\n", index, newLump->lump.size);
}
void insertLump(wadfile_t *wadfile, lump_t *newLump, int index)
{
    // create new table
    lump_t *newTable = malloc((wadfile->header.numlumps + 1) * sizeof(lump_t));
    lump_t *oldTable = wadfile->lumps;
    // copy all lumps before index
    int i = 0;
    if (index > wadfile->header.numlumps)
        index = wadfile->header.numlumps;
    for (; i < index; i++)
    {
        memcpy(&newTable[i], &oldTable[i], sizeof(lump_t) );
    }
    // insert
    memcpy(&newTable[i], newLump, sizeof(lump_t));
    // copy remaining
    for (; i < wadfile->header.numlumps; i++)
    {
        memcpy(&newTable[i + 1], &oldTable[i], sizeof(lump_t) );
    }
    wadfile->lumps = newTable;
    free(oldTable);
    // update numlumps
    wadfile->header.numlumps++;
}
void removeLump(wadfile_t * wadfile, int index)
{
    if (index >= wadfile->header.numlumps)
        return;
    // create new table
    // create new table
    lump_t *newTable = malloc((wadfile->header.numlumps - 1) * sizeof(lump_t));
    lump_t *oldTable = wadfile->lumps;
    // copy all lumps before index
    int i = 0;
    for (; i < index; i++)
    {
        memcpy(&newTable[i], &oldTable[i], sizeof(lump_t) );
    }
    // remove
    free(oldTable[i].data);
    // copy remaining
    for (; i < wadfile->header.numlumps - 1; i++)
    {
        memcpy(&newTable[i], &oldTable[i + 1], sizeof(lump_t) );
    }
    wadfile->lumps = newTable;
    free(oldTable);
    // update numlumps
    wadfile->header.numlumps--;
}
void mergeWadFile(wadfile_t *wad1, wadfile_t *wad2)
{
    for (int i = 0; i < wad2->header.numlumps; i++)
    {
        insertLump(wad1, &wad2->lumps[i], 0xFFFF);
    }
}
