/* Figur-ROMs (Figuren).
   Werden zur Laufzeit von der SD-Karte in die figcache-Flash-Partition kopiert
   und per esp_partition_mmap eingeblendet -- die Logik dazu steht in main.cpp.
   Hier nur der aktive Zeiger + Laenge, die der Memory-Layer (t7_memory.c)
   ueber figure_rom_ptr()/figure_rom_length() abfragt.
   (Frueher lagen hier die 3 ~1.7MB grossen eingebetteten Arrays.) */

static const unsigned char* g_fig_ptr = 0;
static unsigned int         g_fig_len = 0;

/* Von main.cpp aufgerufen, nachdem eine Figur in figcache gemappt wurde.
   p=0, n=0  =>  keine Figur aktiv (Lite / Aus). */
void figure_rom_set_active(const unsigned char* p, unsigned int n){ g_fig_ptr = p; g_fig_len = n; }

const unsigned char* figure_rom_ptr(void){ return g_fig_ptr; }
unsigned int         figure_rom_length(void){ return g_fig_len; }
