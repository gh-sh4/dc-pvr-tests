#include <cassert>
#include <cstdio>
#include <utility>

#include <dc/asic.h>
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

void
vram32_memcpy(uint32_t vram_addr, const uint32_t *data, uint32_t num_bytes)
{
  volatile uint32_t *vram = (volatile uint32_t *)0xa500'0000;
  for (uint32_t i = 0; i < num_bytes / 4; ++i) {
    vram[vram_addr / 4 + i] = data[i];
  }
}

uint32_t
vram32_read32(uint32_t vram_addr)
{
#define VRAM32(_offset) ((volatile uint32_t *)(0xA500'0000 | (_offset)))
  return *VRAM32(vram_addr);
#undef VRAM32
}

void
vram32_memset(uint32_t vram_addr, uint32_t data, uint32_t num_bytes)
{
  volatile uint32_t *vram = (volatile uint32_t *)0xa500'0000;
  for (uint32_t i = 0; i < num_bytes / 4; ++i) {
    vram[vram_addr / 4 + i] = data;
  }
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

const std::initializer_list<unsigned long> MyPVR_Events = {
  // Unknown what this interrupt does. Asked in Discord. For now, assume TSP includes VRAM
  // flush.
  // https://discord.com/channels/488332893324836864/614912396460556298/1293576526335443025
  // ASIC_EVT_PVR_RENDERDONE_VIDEO,

  ASIC_EVT_PVR_OPAQUEDONE,
  ASIC_EVT_PVR_TRANSDONE,
  ASIC_EVT_PVR_RENDERDONE_TSP,
  ASIC_EVT_PVR_RENDERDONE_ISP,
};

void
MyPVR::_static_interrupt_handler(unsigned long evt, void *data)
{
  MyPVR *pvr = (MyPVR *)data;
  if (data) {
    pvr->_handle_interrupt(evt);
  }
}

void
MyPVR::_handle_interrupt(unsigned long evt)
{
  _evt_signals = _evt_signals | evt;
}

MyPVR::MyPVR() : _evt_signals(0), _evt_registered(0)
{
  /* Register interrupt handlers */
  for (auto evt : MyPVR_Events) {
    asic_evt_set_handler(evt, MyPVR::_static_interrupt_handler, this);
    asic_evt_enable(evt, ASIC_IRQ_DEFAULT);
    _evt_registered |= evt;
  }
}

MyPVR::~MyPVR()
{
  /* De-register interrupt handlers */
  for (auto evt : MyPVR_Events) {
    asic_evt_disable(evt, ASIC_IRQ_DEFAULT);
  }
}

void
MyPVR::wait_for_events(unsigned long mask)
{
  if ((_evt_registered & mask) != mask) {
    printf("error MyPVR: Trying to wait on unregistered events!\n");
    return;
  }

  while ((_evt_signals & mask) != mask)
    ;
}

void
MyPVR::set_fb_regs(FrameBufferDef fb_def)
{
  FB_W_CTRL        = 0x0000'0006;
  FB_W_LINE_STRIDE = fb_def.line_stride_bytes / 8; // in 64-bit units
  FB_W_SOF1        = fb_def.vram32_addr;
  FB_W_SOF2        = fb_def.vram32_addr + fb_def.line_stride_bytes;
  FB_X_CLIP        = (63 << 16) | (0 << 0); // Render x in [0, 64)
  FB_Y_CLIP        = (63 << 16) | (0 << 0); // Render y in [0, 64)

  FB_R_SOF1 = fb_def.vram32_addr;
  FB_R_SOF2 = fb_def.vram32_addr + fb_def.line_stride_bytes;

  uint32_t fb_r_ctrl_fb_depth = 0;
  switch (fb_def.pixel_format) {
    case framebuffer_pixel_format_t::KRGB_0555:
      FB_R_CTRL = 0 << 2;
      break;
    case framebuffer_pixel_format_t::RGB_565:
      FB_R_CTRL = 1 << 2;
      break;
    case framebuffer_pixel_format_t::RGB_888:
      FB_R_CTRL = 2 << 2;
      break;
    case framebuffer_pixel_format_t::ARGB_8888:
      FB_R_CTRL = 3 << 2;
      break;
  }

  FB_R_SIZE = (fb_def.height << 10) | (fb_def.width << 0); // Assume 0 FB_R_SIZE.modulus

  FOG_CLAMP_MIN = 0x0000'0000;
  FOG_CLAMP_MAX = 0xffff'ffff;

  SCALER_CTL    = 0x0000'0400;
  FPU_PARAM_CFG = 0x0027df77;
  Y_COEFF       = 0x0000'8040;

  FPU_PARAM_CFG = (1 << 21) | (0x1f << 14) | (0x1f << 8) | (7 << 4) |
                  (3 << 0); // Type 2 Region Array Entries
}

void
MyPVR::set_background(uint32_t vram_addr,
                      float width,
                      float height,
                      uint32_t color,
                      float depth)
{
  // Background depth and plane
  union {
    uint32_t u;
    float f;
  } u32f;
  u32f.f = depth;

  isp_backgnd_plane_t backgnd;
  backgnd.isp_tsp_control = 0x90800000; // DepthTest:GreaterOrEqual | GouraudShading
  backgnd.tsp_control     = 0x20800440; // Src:ONE Dst:ZERO
  backgnd.texture_control = 0x00000000; //
  backgnd.x1              = 0;
  backgnd.y1              = height;
  backgnd.z1              = u32f.f;
  backgnd.color1          = 0xff00'0000;
  backgnd.x2              = 0;
  backgnd.y2              = 0;
  backgnd.z2              = u32f.f;
  backgnd.color2          = 0xff00'0000;
  backgnd.x3              = width;
  backgnd.y3              = height;
  backgnd.z3              = u32f.f;
  backgnd.color3          = 0xff00'0000;

  vram32_memcpy(vram_addr, (const uint32_t *)&backgnd, sizeof(backgnd));
  ISP_BACKGND_T = (0b001 << 24) | ((vram_addr >> 2) << 3);
  ISP_BACKGND_D = u32f.u;
}

void
MyPVR::setup_region_array(RegionArrayDef def)
{
  uint32_t opb_ptr = def.opb_start_offset;
  uint32_t region_array_offset = def.region_array_offset;

  RegionArrayEntry entry    = {};
  entry.control.raw         = 0x0000'0000;
  entry.control.dont_flush  = 0;
  entry.control.dont_zclear = 0;
  entry.control.last        = 0;

  for (uint32_t y = 0; y < def.height / 32; ++y) {
    for (uint32_t x = 0; x < def.width / 32; ++x) {
      entry.control.tile_x = x / 32;
      entry.control.tile_y = y / 32;

      for (uint32_t i = 0; i < 5; ++i) {
        entry.lists[i].empty    = def.list_opb_sizes[i] == 0;
        entry.lists[i].list_ptr = opb_ptr;
        opb_ptr += def.list_opb_sizes[i];
      }

      vram32_memcpy(region_array_offset, (const uint32_t *)&entry, sizeof(entry));
      region_array_offset += sizeof(entry);
    }
  }

  // Set last bit in last entry
  region_array_offset -= sizeof(entry);
  entry.control.last = 1;
  vram32_write32(region_array_offset, entry.control.raw);
}
