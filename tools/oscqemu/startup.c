// Self-contained Cortex-M3 startup + ARM semihosting for the QEMU differential
// harness. No rdimon/crt0 dependency: we own the vector table, .data/.bss init,
// and the syscall shims. The shared driver's stdout (the sample stream) is
// routed to a host file via semihosting, so capture is clean regardless of the
// console/TTY; scenario args arrive via the semihosting command line.
#include <stdint.h>
#include <stdio.h>

extern int main(int argc, char** argv);
extern void __libc_init_array(void);
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack, _end;

// ARM semihosting call: r0 = op, r1 = param, trap via BKPT 0xAB (the M-profile
// convention QEMU recognizes). Returns r0.
static int semihost(int op, void* arg) {
  register int r0 __asm__("r0") = op;
  register void* r1 __asm__("r1") = arg;
  __asm__ volatile("bkpt 0xAB" : "+r"(r0) : "r"(r1) : "memory");
  return r0;
}
#define SYS_OPEN        0x01
#define SYS_WRITE0      0x04
#define SYS_CLOSE       0x02
#define SYS_WRITE       0x05
#define SYS_GET_CMDLINE 0x15
#define SYS_EXIT        0x18

static int out_handle;  // semihosting handle for the sample-output file

// newlib retarget: printf -> _write -> semihosting SYS_WRITE to the host file.
int _write(int fd, const char* buf, int len) {
  (void)fd;
  uint32_t block[3] = { (uint32_t)out_handle, (uint32_t)buf, (uint32_t)len };
  int not_written = semihost(SYS_WRITE, block);
  return len - not_written;
}
// A run that dies says why, on the console, and QEMU exits non-zero.
static void Fail(const char* reason) {
  semihost(SYS_WRITE0, (void*)reason);
  semihost(SYS_EXIT, (void*)0x20023 /* ADP_Stopped_RunTimeErrorUnknown */);
  for (;;) {}
}

// The heap stops short of the stack by kStackGuardBytes.
static const uint32_t kStackGuardBytes = 1024;

void* _sbrk(int incr) {
  static char* heap;
  if (!heap) heap = (char*)&_end;
  char* sp;
  __asm__ volatile("mov %0, sp" : "=r"(sp));
  if (heap + incr > sp - kStackGuardBytes) Fail("heap exhausted: it would reach the stack\n");
  char* prev = heap;
  heap += incr;
  return prev;
}
int _read(int fd, char* buf, int len) { (void)fd; (void)buf; (void)len; return 0; }
int _close(int fd) { (void)fd; return -1; }
int _fstat(int fd, void* st) { (void)fd; (void)st; return 0; }
int _isatty(int fd) { (void)fd; return 1; }
int _lseek(int fd, int off, int whence) { (void)fd; (void)off; (void)whence; return 0; }
void _kill(int a, int b) { (void)a; (void)b; }
int _getpid(void) { return 1; }

// Bare-metal C++ runtime bits normally supplied by crt*.o (which -nostartfiles
// omits): __dso_handle backs static-destructor registration; _init/_fini are
// the (empty) hooks __libc_init_array brackets the constructor array with.
void* __dso_handle = 0;
void _init(void) {}
void _fini(void) {}

static char cmdline[256];
static char* argv_buf[16];

void Reset_Handler(void) {
  // Copy .data (flash -> SRAM), zero .bss.
  uint32_t* src = &_sidata;
  for (uint32_t* dst = &_sdata; dst < &_edata;) *dst++ = *src++;
  for (uint32_t* dst = &_sbss; dst < &_ebss;) *dst++ = 0;
  __libc_init_array();  // C++ static constructors

  // Route the sample stream to a host file in QEMU's working directory, so the
  // host can diff it.
  { static const char kOut[] = "qemu_out.txt";
    uint32_t open_block[3] = { (uint32_t)kOut, 4 /* "w" */, sizeof(kOut) - 1 };
    out_handle = semihost(SYS_OPEN, open_block); }

  // Pull the scenario args from QEMU's semihosting command line into argv
  // (argv[0] is a dummy program name).
  int argc = 0;
  uint32_t cl_block[2] = { (uint32_t)cmdline, sizeof(cmdline) - 1 };
  if (semihost(SYS_GET_CMDLINE, cl_block) == 0) {
    char* p = cmdline;
    while (*p && argc < (int)(sizeof(argv_buf) / sizeof(argv_buf[0])) - 1) {
      while (*p == ' ') ++p;
      if (!*p) break;
      argv_buf[argc++] = p;
      while (*p && *p != ' ') ++p;
      if (*p) *p++ = 0;
    }
  }
  argv_buf[argc] = 0;

  main(argc, argv_buf);
  fflush(NULL);  // push buffered output through _write to the host file
  { uint32_t h = (uint32_t)out_handle; semihost(SYS_CLOSE, &h); }

  // 32-bit SYS_EXIT takes the reason code directly in r1 (not a pointer).
  semihost(SYS_EXIT, (void*)0x20026 /* ADP_Stopped_ApplicationExit */);
  for (;;) {}
}

static void NmiHandler(void) { Fail("NMI\n"); }
static void HardFaultHandler(void) { Fail("hard fault\n"); }
static void MemManageHandler(void) { Fail("memory management fault\n"); }
static void BusFaultHandler(void) { Fail("bus fault\n"); }
static void UsageFaultHandler(void) { Fail("usage fault\n"); }

// Initial SP, reset, and the faults, so a crash ends the run instead of
// jumping through an empty vector.
__attribute__((section(".isr_vector"), used))
void (* const g_pfnVectors[])(void) = {
  (void (*)(void))&_estack,
  Reset_Handler,
  NmiHandler,
  HardFaultHandler,
  MemManageHandler,
  BusFaultHandler,
  UsageFaultHandler,
};
