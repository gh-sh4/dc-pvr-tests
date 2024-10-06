#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <dc/pvr.h>
#include <dc/sq.h>
#include <unistd.h>

#include "test.h"
#include "pvr.h"

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
  data[0] |= (group_en << 23) | (strip_len << 18) | (user_clip << 16);
  data[1] = 0x00000000; // ISP_TSP Instruction
  data[2] = 0x00000000; // TSP instruction
  data[3] = 0x00000000; // texture control
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
  data[6] = 0x01234567; // base color

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
void
test_ta_basic_single_poly(TestContext *context)
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

  pvr_dump_vram("ta_basic_single_poly_ta_init");
  pvr_dump_regs("ta_basic_single_poly_ta_init");

  // ASSERTION: TA_LIST_INIT will not cause any tiles to have an initial OPB until
  // some sort of flush
  test_assert(vram32_read32(0x0010'0000) == 0x0000'0000,
              "OL entry should be uninitialized after list init");

  FILE *logfile = fopen("/pc/ta_basic_single_poly.log", "w");

  fprintf(logfile, "@ After TA INIT but before global polygon params...\n");
  print_ta_ol_pointers(logfile, 4);

  // Setup a single polygon
  ta_user_clip(0, 0, 1, 1);
  ta_global_poly0();

  fprintf(logfile, "@ After global polygon params...\n");
  test_assert(vram32_read32(0x0010'0000) == 0x0000'0000,
              "OL entry should be uninitialized after global polygon params");
  print_ta_ol_pointers(logfile, 4);

  ta_poly0_vertex(0.0f, 0.0f, 1.0f);
  ta_poly0_vertex(0.0f, 48.0f, 1.0f);
  ta_poly0_vertex(48.0f, 48.0f, 1.0f, true);

  sleep(1);

  fprintf(logfile, "@ After vertex params...\n");
  test_assert(vram32_read32(0x0010'0000) == 0x0000'0000,
              "OL entry should be uninitialized after vertex params");
  print_ta_ol_pointers(logfile, 4);

  // At this point, TA_OL_POINTERS should have a valid entry pointing at the first OL/OPB
  // entry
  TA_OL_POINTERS_t ol = pvr_read_ta_ol_pointers(0);
  test_assert(ol.entry, "TA_OL_POINTERS entry bit should be set");
  test_assert((ol.addr << 2) == 0x0010'0000,
              "TA_OL_POINTERS addr should point at the first OL entry");

  ta_end_of_list();

  sleep(1);

  // At this point, TA_OL_POINTERS entry is no longer valid and OL points to ISP
  // parameters at the start of VRAM

  fprintf(logfile, "@ After end of opaque list...\n");

  ol = pvr_read_ta_ol_pointers(0);
  test_assert(!ol.entry, "TA_OL_POINTERS entry bit should not be set");
  test_assert(vram32_read32(0x0010'0000) == 0x8020'0000,
              "OL entry should be initialized after end of opaque list");
  print_ta_ol_pointers(logfile, 4);

  pvr_dump_vram("ta_basic_single_poly_post");
  pvr_dump_regs("ta_basic_single_poly_post");

  fclose(logfile);
}