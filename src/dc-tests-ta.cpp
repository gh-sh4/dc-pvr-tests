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
ta_poly0_vertex(float x,
                float y,
                float z,
                uint32_t packed_color,
                bool end_of_strip = false)
{
  const uint32_t para_type = 7;

  uint32_t data[8];
  memset(data, 0, sizeof(data));

  data[0] = (para_type << 29) | (end_of_strip << 28);
  memcpy(&data[1], &x, sizeof(float));
  memcpy(&data[2], &y, sizeof(float));
  memcpy(&data[3], &z, sizeof(float));
  data[6] = packed_color; // base color

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

  ta_poly0_vertex(0.0f, 0.0f, 1.0f, 0xffff'ffff);
  ta_poly0_vertex(0.0f, 48.0f, 1.0f, 0xffff'ffff);
  ta_poly0_vertex(48.0f, 48.0f, 1.0f, 0xffff'ffff, true);

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
  ta_poly0_vertex(-NaN, -NaN, NaN, 0xffff'ffff);
  ta_poly0_vertex(-NaN, 30, NaN, 0xffff'ffff);
  ta_poly0_vertex(30, 30, NaN, 0xffff'ffff, true);
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
  MyPVR pvr;

  memset((void *)0xa500'0000, 0, 4 * 1024 * 1024);

  SOFTRESET = 0b11; // Assert then de-assert TA/ISP reset
  SOFTRESET = 0b00;

  TA_ISP_BASE       = 0x0000'0000; // First MiB for ISP data
  TA_ISP_LIMIT      = 0x0010'0000;
  TA_OL_BASE        = 0x0010'0000; // Second MiB for Object List data
  TA_OL_LIMIT       = 0x0020'0000;
  TA_NEXT_OPB_INIT  = 0x0018'0000; // This is wasteful, but doesn't matter for this test
  TA_GLOB_TILE_CLIP = 0x0001'0001; // 2x2 tile arrangement (4 total tiles)
  TA_ALLOC_CTRL     = 0x0000'0001; // 8 element OPBs for opaque lists only

  TA_LIST_INIT = 0x8000'0000; // Trigger initializing the TA
  (void)TA_LIST_INIT;         // dummy read to ensure write completes

  // ctx.pvr_reg_vram_dump("ta_init");

  // Setup a single polygon
  ta_global_poly0();
  ta_poly0_vertex(32.0f, 0.0f, 0.5f, 0xffff'0000);
  ta_poly0_vertex(0.0f, 64.0f, 0.5f, 0xff00'ff00);
  ta_poly0_vertex(64.0f, 64.0f, 0.5f, 0xff00'00ff, true);
  ta_end_of_list();

  // Wait for TA to finish binning
  pvr.wait_for_events(ASIC_EVT_PVR_OPAQUEDONE);

  // Setup the background plane and depth
  const uint32_t isp_backgnd_vram_offset = 0x0038'0000;
  pvr.set_background(isp_backgnd_vram_offset, 64, 64, 0xff00'0000, 1.0f / 1024);

  /* Setup region array */
  REGION_BASE  = 0x0030'0000; // Region array @ 3MiB
  PARAM_BASE   = 0x0000'0000; // Read all parameter data from our single buffer
  ISP_FEED_CFG = 0x00800408;

  const uint32_t framebuffer_offset         = 0x0020'0000;
  const uint32_t framebuffer_bytes_per_line = 4 * 64;

  pvr.setup_region_array(RegionArrayDef {
    .region_array_offset = 0x0030'0000,
    .opb_start_offset    = 0x0010'0000,
    .list_opb_sizes      = { 0x20, 0, 0, 0, 0 },
    .width               = 64,
    .height              = 64,
  });

  pvr.set_fb_regs(FrameBufferDef {
    .vram32_addr       = framebuffer_offset,
    .width             = 64,
    .height            = 64,
    .line_stride_bytes = framebuffer_bytes_per_line,
    .pixel_format      = framebuffer_pixel_format_t::ARGB_8888,
  });

  vram32_memset(framebuffer_offset, 0xcafebeef, 512 * 1024);

  // Trigger ISP rendering, wait for shading (and FB writeback) to complete
  STARTRENDER = 0xffff'ffff;
  pvr.wait_for_events(ASIC_EVT_PVR_RENDERDONE_TSP);

  ctx.pvr_reg_vram_dump("end_of_test");
  ctx.write_framebuffer_ppm("end_of_test",
                            FrameBufferDef {
                              .vram32_addr  = framebuffer_offset,
                              .width        = 64,
                              .height       = 64,
                              .pixel_format = framebuffer_pixel_format_t::ARGB_8888,
                            });
}