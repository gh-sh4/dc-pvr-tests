#include <cassert>
#include <cstdio>

#include <arch/timer.h>

#include "pvr.h"

TA_OL_POINTERS_t
pvr_read_ta_ol_pointers(unsigned index)
{
  volatile uint32_t *base = (volatile uint32_t *)0xa05f'8600;
  assert(index < 600 && "Invalid TA OL index");

  TA_OL_POINTERS_t result;
  result.raw = base[index];
  return result;
}

void
vram32_write32(uint32_t vram_addr, uint32_t data)
{
  volatile uint32_t *vram = (volatile uint32_t *)0xa500'0000;
  vram[vram_addr / 4]     = data;
}

uint32_t
vram32_read32(uint32_t vram_addr)
{
#define VRAM32(_offset) ((volatile uint32_t *)(0xA500'0000 | (_offset)))
  return *VRAM32(vram_addr);
#undef VRAM32
}

const uint32_t kVRAMDumpBufferBytes = 256 * 1024;
uint32_t g_vram_dump_buffer[kVRAMDumpBufferBytes / sizeof(uint32_t)];

void
pvr_dump_vram(FILE *dump_file)
{
  for (uint32_t vram_addr = 0; vram_addr < 8 * 1024 * 1024;
       vram_addr += kVRAMDumpBufferBytes) {

    // Read kVRAMDumpBufferBytes bytes from VRAM to the global buffer
    for (uint32_t i = 0; i < kVRAMDumpBufferBytes / 4; ++i) {
      g_vram_dump_buffer[i] = vram32_read32(vram_addr + i * sizeof(uint32_t));
    }
    fwrite(g_vram_dump_buffer, 1, kVRAMDumpBufferBytes, dump_file);
  }
}

void
pvr_dump_regs(FILE *dump_file)
{
  struct RegisterRange {
    uint32_t start; // inclusive, OR'd with 0xA05F'0000
    uint32_t end;   // inclusive, OR'd with 0xA05F'0000
  };

  // Page 108 of the bible
  static RegisterRange pvr_regs[] = {
    { 0x8000, 0x8008 }, // ID/REVISION/SOFTRESET
    { 0x8014, 0x8018 }, // STARTRENDER/TEST_SELECT
    { 0x8020, 0x8020 }, // PARAM_BASE
    { 0x802C, 0x8030 }, // REGION_BASE/SPAN_SORT_CFG
    { 0x8040, 0x8054 }, // VO_BORDER_COL/FB_R_*
    { 0x805C, 0x806C }, // (continued)
    { 0x8074, 0x808C }, // FPU config, ISP_BACKGND_*
    { 0x8098, 0x8098 }, // ISP_FEED_CFG
    { 0x80A0, 0x80A8 }, // Texture SDRAM Config
    { 0x80B0, 0x80C0 }, // Fog Config
    { 0x80C4, 0x80F4 }, // SPG, VO, SCALER
    { 0x8108, 0x811C }, // PAL_RAM, Misc
    { 0x8124, 0x8150 }, // TA registers
    { 0x8160, 0x8164 }, // List continue, next_opb_init

    { 0x8200, 0x83FC }, // FOG_TABLE
    { 0x8600, 0x8F5C }, // TA_OL_POINTERS
    { 0x9000, 0x9FFC }, // PALETTE_RAM
  };

  uint32_t count = 0;
  for (const auto &range : pvr_regs) {
    for (uint32_t offset = range.start; offset <= range.end; offset += sizeof(uint32_t)) {
      const uint32_t address = 0xA05F'0000 | offset;
      const uint32_t reg     = *((volatile uint32_t *)address);
      fwrite(&address, 1, sizeof(uint32_t), dump_file);
      fwrite(&reg, 1, sizeof(uint32_t), dump_file);
      count++;
    }
  }
}
