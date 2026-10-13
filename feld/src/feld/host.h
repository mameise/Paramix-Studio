/* Only for the PC test (host.c): flash as a memory array, input from a sequence. */
extern unsigned char *host_flash;
#define FLASH_BASE ((volatile u8*)host_flash)
int host_input(int *step);
void host_exit(const char *why);
