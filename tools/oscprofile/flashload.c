// QEMU TCG plugin: for every instruction that reads memory, how many of its
// reads went to flash and how many to RAM, by the address each one read.
//
//   qemu-system-arm ... -plugin flashload.so,out=<file>
//
// writes `<pc hex> <flash reads> <RAM reads>` a line for each such
// instruction, at exit. Flash is everything below the SRAM base, which on
// both the profile image's machine and the F103 is 0x20000000.
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

#define SRAM_BASE 0x20000000u
// Thumb instructions sit at even addresses below this, in the image's flash.
#define CODE_TOP 0x40000u

static uint64_t flash_reads[CODE_TOP / 2];
static uint64_t ram_reads[CODE_TOP / 2];
static char out_path[4096];

static void on_read(unsigned int vcpu, qemu_plugin_meminfo_t info,
                    uint64_t address, void *pc) {
  const uintptr_t index = (uintptr_t)pc / 2;
  (void)vcpu;
  if (qemu_plugin_mem_is_store(info)) return;
  if (address < SRAM_BASE) {
    flash_reads[index]++;
  } else {
    ram_reads[index]++;
  }
}

static void on_translate(qemu_plugin_id_t id, struct qemu_plugin_tb *tb) {
  const size_t count = qemu_plugin_tb_n_insns(tb);
  (void)id;
  for (size_t i = 0; i < count; ++i) {
    struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
    const uint64_t pc = qemu_plugin_insn_vaddr(insn);
    if (pc >= CODE_TOP) continue;
    // Every access, sorted by qemu_plugin_mem_is_store: asked for reads alone,
    // Ubuntu's QEMU 6.2 delivers the writes.
    qemu_plugin_register_vcpu_mem_cb(insn, on_read, QEMU_PLUGIN_CB_NO_REGS,
                                     QEMU_PLUGIN_MEM_RW, (void *)(uintptr_t)pc);
  }
}

static void write_counts(qemu_plugin_id_t id, void *unused) {
  FILE *out = fopen(out_path, "w");
  (void)id;
  (void)unused;
  if (!out) return;
  for (size_t index = 0; index < CODE_TOP / 2; ++index) {
    if (flash_reads[index] || ram_reads[index]) {
      fprintf(out, "%zx %" PRIu64 " %" PRIu64 "\n", index * 2,
              flash_reads[index], ram_reads[index]);
    }
  }
  fclose(out);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv) {
  (void)info;
  for (int i = 0; i < argc; ++i) {
    if (!strncmp(argv[i], "out=", 4)) {
      strncpy(out_path, argv[i] + 4, sizeof(out_path) - 1);
    }
  }
  if (!out_path[0]) return -1;
  qemu_plugin_register_vcpu_tb_trans_cb(id, on_translate);
  qemu_plugin_register_atexit_cb(id, write_counts, NULL);
  return 0;
}
