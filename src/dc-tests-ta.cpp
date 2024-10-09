#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

#include <dc/asic.h>
#include <dc/pvr.h>
#include <dc/sq.h>
#include <unistd.h>

#include <kos/thread.h>

#include "test.h"
#include "pvr.h"

using namespace test_flags;

union RegionArrayControlWord {
  struct {
    uint32_t _0 : 2;
    uint32_t tile_x : 6;
    uint32_t tile_y : 6;
    uint32_t _1 : 14;
    uint32_t dont_flush : 1;
    uint32_t presort : 1;
    uint32_t dont_zclear : 1;
    uint32_t last : 1;
  };
  uint32_t raw;
};

union RegionArrayListPtr {
  struct {
    uint32_t list_ptr : 24;
    uint32_t _1 : 7;
    uint32_t empty : 1;
  };
  uint32_t raw;
};

struct RegionArrayEntry {
  RegionArrayControlWord control;
  RegionArrayListPtr lists[5];
};
static_assert(sizeof(RegionArrayEntry) == 6 * sizeof(uint32_t));

void
ta_user_clip(unsigned tx_min, unsigned ty_min, unsigned tx_max, unsigned ty_max)
{
  uint32_t data[8];
  data[0] = 0x20000000;
  data[1] = 0x00000000;
  data[2] = 0x00000000;
  data[3] = 0x00000000;
  data[4] = tx_min;
  data[5] = ty_min;
  data[6] = tx_max;
  data[7] = ty_max;

  static_assert(sizeof(data) == 32);
  sq_cpy((void *)map::TA_POLYGON_FIFO, data, 32);
}

void
ta_global_poly0()
{
  const uint32_t para_type = 4;
  const uint32_t list_type = 0;
  const uint32_t group_en  = 1;
  const uint32_t strip_len = 0;
  const uint32_t user_clip = 0;

  uint32_t data[8];
  data[0] = (para_type << 29) | (list_type << 24);
  data[0] |= (group_en << 23) | (strip_len << 18) | (user_clip << 16) | (1 << 1);
  data[1] = (6 << 29) | (1 << 23); // ISP_TSP Instruction
  data[2] = (1 << 29) | (0 << 26); // TSP instruction
  data[3] = 0x00000000;            // texture control
  data[4] = 0;
  data[5] = 0;
  data[6] = 0;
  data[7] = 0;

  sq_cpy((void *)map::TA_POLYGON_FIFO, data, 32);
}

void
ta_poly0_vertex(float x, float y, float z, bool end_of_strip = false)
{
  const uint32_t para_type = 7;

  uint32_t data[8];
  memset(data, 0, sizeof(data));

  data[0] = (para_type << 29) | (end_of_strip << 28);
  memcpy(&data[1], &x, sizeof(float));
  memcpy(&data[2], &y, sizeof(float));
  memcpy(&data[3], &z, sizeof(float));
  data[6] = 0xff00ff00; // base color

  sq_cpy((void *)map::TA_POLYGON_FIFO, data, 32);
}

void
ta_end_of_list()
{
  sq_set32((void *)map::TA_POLYGON_FIFO, 0, 32);
}

void
print_ta_ol_pointers(FILE *logfile, unsigned count)
{
  for (unsigned i = 0; i < count; ++i) {
    const TA_OL_POINTERS_t ol = pvr_read_ta_ol_pointers(i);
    fprintf(logfile,
            "TA_OL_POINTERS %i raw 0x%08lx skip %d addr 0x%x shadow %d number %d "
            "triangle %d sprite %d entry %d\n",
            i,
            ol.raw,
            ol.skip,
            ol.addr << 2,
            ol.shadow,
            ol.number,
            ol.triangle,
            ol.sprite,
            ol.entry);
  }
}

/** Verify basic behavior about TA list initialization and TA_OL_POINTERS. This test
 * performs binning for a single triangle against a 2x2 arrangement of tiles. No rendering
 * is performed; this test is purely about binner behavior. */

REGISTER_TEST(ta_basic_single_poly, TEST_TA, "Basic TA single polygon test")
{
  memset((void *)0xa500'0000, 0, 3 * 1024 * 1024);

  SOFTRESET = 0b011; // Assert then de-assert TA/ISP reset
  SOFTRESET = 0b000;

  TA_ISP_BASE       = 0x0000'0000; // First MiB for ISP data
  TA_ISP_LIMIT      = 0x0010'0000;
  TA_OL_BASE        = 0x0010'0000; // Second MiB for Object List data
  TA_OL_LIMIT       = 0x0020'0000;
  TA_NEXT_OPB_INIT  = 0x0018'0000; // This is wasteful, but doesn't matter for this test
  TA_GLOB_TILE_CLIP = 0x0001'0001; // 2x2 tile arrangement (4 total tiles)
  TA_ALLOC_CTRL     = 0x0000'0001; // 8 element OPBs for opaque lists only

  TA_LIST_INIT = 0x8000'0000; // Trigger initializing the TA
  (void)TA_LIST_INIT;         // dummy read to ensure write completes

  ctx.pvr_reg_vram_dump("ta_init");

  // ASSERTION: TA_LIST_INIT will not cause any tiles to have an initial OPB until
  // some sort of flush
  ctx.assert(vram32_read32(0x0010'0000) == 0x0000'0000,
             "OL entry should be uninitialized after list init");

  ctx.log("@ After TA INIT but before global polygon params...\n");
  print_ta_ol_pointers(ctx.log_file(), 4);

  // Setup a single polygon
  ta_user_clip(0, 0, 1, 1);
  ta_global_poly0();

  ctx.log("@ After global polygon params...\n");
  ctx.assert(vram32_read32(0x0010'0000) == 0x0000'0000,
             "OL entry should be uninitialized after global polygon params");
  print_ta_ol_pointers(ctx.log_file(), 4);

  ta_poly0_vertex(0.0f, 0.0f, 1.0f);
  ta_poly0_vertex(0.0f, 48.0f, 1.0f);
  ta_poly0_vertex(48.0f, 48.0f, 1.0f, true);

  thd_sleep(10);

  ctx.log("@ After vertex params...\n");
  ctx.assert(vram32_read32(0x0010'0000) == 0x0000'0000,
             "OL entry should be uninitialized after vertex params");
  print_ta_ol_pointers(ctx.log_file(), 4);

  // At this point, TA_OL_POINTERS should have a valid entry pointing at the first OL/OPB
  // entry
  TA_OL_POINTERS_t ol = pvr_read_ta_ol_pointers(0);
  ctx.assert(ol.entry, "TA_OL_POINTERS entry bit should be set");
  ctx.assert((ol.addr << 2) == 0x0010'0000,
             "TA_OL_POINTERS addr should point at the first OL entry");

  ta_end_of_list();

  thd_sleep(10);

  // At this point, TA_OL_POINTERS entry is no longer valid and OL points to ISP
  // parameters at the start of VRAM

  ctx.log("@ After end of opaque list...\n");

  ol = pvr_read_ta_ol_pointers(0);
  ctx.assert(!ol.entry, "TA_OL_POINTERS entry bit should not be set");
  ctx.assert(vram32_read32(0x0010'0000) == 0x8020'0000,
             "OL entry should be initialized after end of opaque list");
  print_ta_ol_pointers(ctx.log_file(), 4);

  ctx.pvr_reg_vram_dump("end_of_test");
}

REGISTER_TEST(ta_nan_depth, TEST_TA, "Demonstrate NaN handling by the TA")
{
  memset((void *)0xa500'0000, 0, 3 * 1024 * 1024);

  SOFTRESET = 0b011; // Assert then de-assert TA/ISP reset
  SOFTRESET = 0b000;

  TA_ISP_BASE       = 0x0000'0000; // First MiB for ISP data
  TA_ISP_LIMIT      = 0x0010'0000;
  TA_OL_BASE        = 0x0010'0000; // Second MiB for Object List data
  TA_OL_LIMIT       = 0x0020'0000;
  TA_NEXT_OPB_INIT  = 0x0018'0000; // This is wasteful, but doesn't matter for this test
  TA_GLOB_TILE_CLIP = 0x0001'0001; // 2x2 tile arrangement (4 total tiles)
  TA_ALLOC_CTRL     = 0x0000'0001; // 8 element OPBs for opaque lists only

  TA_LIST_INIT = 0x8000'0000; // Trigger initializing the TA
  (void)TA_LIST_INIT;         // dummy read to ensure write completes

  ctx.pvr_reg_vram_dump("ta_init");

  const float NaN = std::numeric_limits<float>::quiet_NaN();

  // Setup a single polygon
  ta_global_poly0();
  ta_poly0_vertex(-NaN, -NaN, NaN);
  ta_poly0_vertex(-NaN, 30, NaN);
  ta_poly0_vertex(30, 30, NaN, true);
  ta_end_of_list();

  thd_sleep(10);

  // NaN is treated the same as infinite by the TA, so the polygon should be binned
  // only to the first tile
  ctx.assert(vram32_read32(0x0010'0000) == 0x8020'0000, "Tile 0 receives polygon");
  ctx.assert(vram32_read32(0x0010'0020) == 0xf000'0000, "Tile 1 receives no polygon");
  ctx.assert(vram32_read32(0x0010'0040) == 0xf000'0000, "Tile 2 receives no polygon");
  ctx.assert(vram32_read32(0x0010'0060) == 0xf000'0000, "Tile 3 receives no polygon");

  ctx.pvr_reg_vram_dump("end_of_test");
}

void
interrupt_handler(unsigned long evt, void *data)
{
  if (evt & ASIC_EVT_PVR_RENDERDONE_VIDEO)
    printf("PVR Flush-to-VRAM Complete\n");
  if (evt & ASIC_EVT_PVR_RENDERDONE_TSP)
    printf("PVR TSP Complete\n");
  if (evt & ASIC_EVT_PVR_RENDERDONE_ISP)
    printf("PVR ISP Complete\n");
}

REGISTER_TEST(isp_simple_render, TEST_TA | TEST_ISP, "Simple ISP render test")
{
  asic_evt_set_handler(ASIC_EVT_PVR_RENDERDONE_ISP, interrupt_handler, nullptr);
  asic_evt_enable(ASIC_EVT_PVR_RENDERDONE_ISP, ASIC_IRQ_DEFAULT);
  asic_evt_set_handler(ASIC_EVT_PVR_RENDERDONE_TSP, interrupt_handler, nullptr);
  asic_evt_enable(ASIC_EVT_PVR_RENDERDONE_TSP, ASIC_IRQ_DEFAULT);
  asic_evt_set_handler(ASIC_EVT_PVR_RENDERDONE_VIDEO, interrupt_handler, nullptr);
  asic_evt_enable(ASIC_EVT_PVR_RENDERDONE_VIDEO, ASIC_IRQ_DEFAULT);

  memset((void *)0xa500'0000, 0, 4 * 1024 * 1024);

  SOFTRESET = 0b011; // Assert then de-assert TA/ISP reset
  SOFTRESET = 0b000;

  TA_ISP_BASE       = 0x0000'0000; // First MiB for ISP data
  TA_ISP_LIMIT      = 0x0010'0000;
  TA_OL_BASE        = 0x0010'0000; // Second MiB for Object List data
  TA_OL_LIMIT       = 0x0020'0000;
  TA_NEXT_OPB_INIT  = 0x0018'0000; // This is wasteful, but doesn't matter for this test
  TA_GLOB_TILE_CLIP = 0x0001'0001; // 2x2 tile arrangement (4 total tiles)
  TA_ALLOC_CTRL     = 0x0000'0001; // 8 element OPBs for opaque lists only

  TA_LIST_INIT = 0x8000'0000; // Trigger initializing the TA
  (void)TA_LIST_INIT;         // dummy read to ensure write completes

  ctx.pvr_reg_vram_dump("ta_init");

  // Setup a single polygon
  ta_global_poly0();
  ta_poly0_vertex(0.0f, 0.0f, 0.5f);
  ta_poly0_vertex(10.0f, 50.0f, 0.5f);
  ta_poly0_vertex(48.0f, 40.0f, 0.5f, true);
  ta_poly0_vertex(32.0f, 0.0f, 0.2f);
  ta_poly0_vertex(40.0f, 20.0f, 0.2f);
  ta_poly0_vertex(32.0f, 10.0f, 0.2f, true);
  ta_end_of_list();

  thd_sleep(10);

  // ISP setup

  // Background depth and plane
  union {
    uint32_t u;
    float f;
  } u32f;
  u32f.f = 0.125f;

  isp_backgnd_plane_t backgnd;
  backgnd.isp_tsp_control = 0x90800000; // DepthTest:GreaterOrEqual | GouraudShading
  backgnd.tsp_control     = 0x20800440; // Src:ONE Dst:ZERO
  backgnd.texture_control = 0x00000000; //
  backgnd.x1              = 0;
  backgnd.y1              = 64;
  backgnd.z1              = u32f.f;
  backgnd.color1          = 0xff00'0000;
  backgnd.x2              = 0;
  backgnd.y2              = 0;
  backgnd.z2              = u32f.f;
  backgnd.color2          = 0xff00'0000;
  backgnd.x3              = 64;
  backgnd.y3              = 64;
  backgnd.z3              = u32f.f;
  backgnd.color3          = 0xff00'0000;

  const uint32_t isp_backgnd_vram_offset = 0x0038'0000;
  vram32_memcpy(isp_backgnd_vram_offset, (const uint32_t *)&backgnd, sizeof(backgnd));
  ISP_BACKGND_T = (0b001 << 24) | ((isp_backgnd_vram_offset >> 2) << 3);
  ISP_BACKGND_D = u32f.u;

  /* Setup region array */
  FPU_PARAM_CFG = (1 << 21) | (0x1f << 14) | (0x1f << 8) | (7 << 4) |
                  (3 << 0);  // Type 2 Region Array Entries
  REGION_BASE = 0x0030'0000; // Region array @ 3MiB

  RegionArrayEntry entry    = {};
  entry.control.raw         = 0x0000'0000;
  entry.control.tile_x      = 0;
  entry.control.tile_y      = 0;
  entry.control.dont_flush  = 0;
  entry.control.dont_zclear = 0;
  entry.control.last        = 0;
  entry.lists[0].raw        = 0x0010'0000;
  entry.lists[1].raw        = 0x8000'0000;
  entry.lists[2].raw        = 0x8000'0000;
  entry.lists[3].raw        = 0x8000'0000;
  entry.lists[4].raw        = 0x8000'0000;

  static_assert(sizeof(entry) == 24);
  uint32_t region_array_offset = 0x0030'0000;
  vram32_memcpy(region_array_offset, (const uint32_t *)&entry, sizeof(entry));
  region_array_offset += sizeof(entry);

  entry.control.tile_x = 1;
  entry.control.tile_y = 0;
  entry.lists[0].raw   = 0x0010'0020;
  vram32_memcpy(region_array_offset, (const uint32_t *)&entry, sizeof(entry));
  region_array_offset += sizeof(entry);

  entry.control.tile_x = 0;
  entry.control.tile_y = 1;
  entry.lists[0].raw   = 0x0010'0040;
  vram32_memcpy(region_array_offset, (const uint32_t *)&entry, sizeof(entry));
  region_array_offset += sizeof(entry);

  entry.control.tile_x = 1;
  entry.control.tile_y = 1;
  entry.lists[0].raw   = 0x0010'0060;
  entry.control.last   = 1;
  vram32_memcpy(region_array_offset, (const uint32_t *)&entry, sizeof(entry));

  PARAM_BASE   = 0x0000'0000; // Read all parameter data from our single buffer
  ISP_FEED_CFG = 0x00800408;  // FROM EXAMPLE XXX

  const uint32_t framebuffer_offset         = 0x0020'0000;
  const uint32_t framebuffer_bytes_per_line = 4 * 64;

  FB_W_CTRL        = 0x0000'0006;
  FB_W_LINE_STRIDE = framebuffer_bytes_per_line / 8; // in 64-bit units
  FB_W_SOF1        = framebuffer_offset;
  FB_W_SOF2        = framebuffer_offset + framebuffer_bytes_per_line;
  FB_X_CLIP        = (63 << 16) | (0 << 0); // Render x in [0, 64)
  FB_Y_CLIP        = (63 << 16) | (0 << 0); // Render y in [0, 64)

  FOG_CLAMP_MIN = 0x0000'0000;
  FOG_CLAMP_MAX = 0xffff'ffff;

  vram32_memset(framebuffer_offset, 0xcafebeef, 512 * 1024);

  SCALER_CTL = 0x0000'0400;
  // FB_BURSTCTRL  = 0x00093f39;
  // SDRAM_REFRESH = 0x0000'0020;
  // SDRAM_CFG     = 0x15d1c951;
  FPU_PARAM_CFG = 0x0027df77;
  Y_COEFF       = 0x0000'8040;

  SOFTRESET = 0b011; // Assert then de-assert TA/ISP reset
  SOFTRESET = 0b000;

  // Trigger ISP rendering
  STARTRENDER = 0xffff'ffff;

  thd_sleep(10);

  FB_R_SOF1 = framebuffer_offset;
  FB_R_SOF2 = framebuffer_offset + framebuffer_bytes_per_line;
  FB_R_CTRL = 3 << 2;
  FB_R_SIZE = (64 << 10) | (64 << 0); // Assume 0 FB_R_SIZE.modulus

  ctx.pvr_reg_vram_dump("end_of_test");

  asic_evt_disable(ASIC_EVT_PVR_RENDERDONE_VIDEO, ASIC_IRQ_DEFAULT);
  asic_evt_disable(ASIC_EVT_PVR_RENDERDONE_TSP, ASIC_IRQ_DEFAULT);
  asic_evt_disable(ASIC_EVT_PVR_RENDERDONE_ISP, ASIC_IRQ_DEFAULT);
}