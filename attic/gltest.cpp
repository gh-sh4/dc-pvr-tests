/* This program is a slight modification of the libdream/ta example program.
   The big difference in this program is that we add in a small bit of user
   input code, and that we support render-to-texture mode. */

#include <cstdlib>
#include <stdio.h>

#include <arch/types.h>
#include <arch/timer.h>

#include <dc/pvr.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>

/* A little test program -- creates six rainbow polygons and
   moves them around over a white background. */
typedef struct {
  float x, y, z;
} polyplace_t;

const uint32_t kNumPolys = 512;

polyplace_t polys[kNumPolys] = {
  { 16.0f, 0.0f, 10.0f }, { 0.0f, 48.0f, 12.0f },  { 24.0f, 300.0f, 13.0f },
  { 0.0f, 16.0f, 16.0f }, { 0.0f, 364.0f, 18.0f }, { 480.0f, 24.0f, 19.0f },
};

/* Sends one polygon's worth of data to the TA */
void
draw_one_poly(polyplace_t *p)
{
  /* Opaque Colored vertex */
  pvr_vertex_t vert;

  const uint32_t pairs = 16; // pairs*2 total verts

  float x = 0, y = 0;
  float dx = 8, dy = 32;

  uint32_t COLORS[] = {
    0xff0000ff, 0xff00ff00, 0xffff0000, 0xff00ffff, 0xffff00ff,
    0xff0000ff, 0xff00ff00, 0xffff0000, 0xff00ffff, 0xffff00ff,
  };

  for (unsigned i = 0; i < pairs * 2; i += 2) {
    if (i == 0) {
      vert.flags = PVR_CMD_VERTEX;
    }

    vert.x    = p->x + x;
    vert.y    = p->y + y;
    vert.z    = p->z;
    vert.argb = COLORS[i % 8];
    pvr_prim(&vert, sizeof(vert));

    y += dy;

    if (i == (pairs * 2 - 2)) {
      vert.flags = PVR_CMD_VERTEX_EOL;
    }
    vert.x    = p->x + x;
    vert.y    = p->y + y;
    vert.z    = p->z;
    vert.argb = COLORS[i % 8];
    pvr_prim(&vert, sizeof(vert));

    x += dx;
    y -= dy;
  }
}

/* Sends one polygon's worth of data to the TA */
void
draw_one_textured_poly(polyplace_t *p)
{
  /* Opaque Textured vertex */
  pvr_vertex_t vert;

  vert.flags = PVR_CMD_VERTEX;
  vert.x     = p->x + 0.0f;
  vert.y     = p->y + 240.0f;
  vert.z     = p->z;
  vert.u     = 0.0f;
  vert.v     = 480.0f / 512.0f;
  vert.argb  = 0xffffffff;
  vert.oargb = 0;
  pvr_prim(&vert, sizeof(vert));

  vert.y = p->y + 0.0f;
  vert.v = 0.0f;
  pvr_prim(&vert, sizeof(vert));

  vert.x = p->x + 320.0f;
  vert.y = p->y + 240.0f;
  vert.u = 640.0f / 1024.0f;
  vert.v = 480.0f / 512.0f;
  pvr_prim(&vert, sizeof(vert));

  vert.flags = PVR_CMD_VERTEX_EOL;
  vert.y     = p->y + 0.0f;
  vert.v     = 0.0f;
  pvr_prim(&vert, sizeof(vert));
}

int to_texture = 1;
pvr_ptr_t d_texture;
uint32 tx_x = 1024, tx_y = 512;

void
draw_frame(void)
{
  pvr_poly_cxt_t cxt;
  pvr_poly_hdr_t poly;
  int i;

  pvr_poly_cxt_col(&cxt, PVR_LIST_OP_POLY);
  cxt.gen.culling = PVR_CULLING_NONE;

  pvr_poly_compile(&poly, &cxt);

  /* Start opaque poly list */
  pvr_wait_ready();

  pvr_scene_begin();
  pvr_list_begin(PVR_LIST_OP_POLY);

  /* Send polygon header to the TA using store queues */
  pvr_prim(&poly, sizeof(poly));

  /* Draw all polygons */
  for (i = 0; i < kNumPolys; i++)
    draw_one_poly(polys + i);

  /* End of opaque list */
  pvr_list_finish();

  /* Finish the frame */
  pvr_scene_finish();
}

void
draw_textured(void)
{
  pvr_poly_cxt_t cxt;
  pvr_poly_hdr_t hdr;
  int i;

  draw_frame();

  pvr_poly_cxt_txr(&cxt,
                   PVR_LIST_OP_POLY,
                   PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED,
                   1024,
                   512,
                   d_texture,
                   PVR_FILTER_NONE);
  pvr_poly_compile(&hdr, &cxt);

  pvr_wait_ready();
  pvr_scene_begin();

  /* Start opaque poly list */
  pvr_list_begin(PVR_LIST_OP_POLY);

  /* Send polygon header to the TA using store queues */
  pvr_prim(&hdr, sizeof(hdr));

  /* Draw all polygons */
  for (i = 0; i < kNumPolys; i++)
    draw_one_textured_poly(polys + i);

  /* End of opaque list */
  pvr_list_finish();

  /* Finish the frame */
  pvr_scene_finish();
}

/* Main program: init and loop drawing polygons */
pvr_init_params_t pvr_params = {
  { PVR_BINSIZE_8, PVR_BINSIZE_0, PVR_BINSIZE_0, PVR_BINSIZE_0, PVR_BINSIZE_0 },
  128 * kNumPolys,
  .opb_overflow_count = 16,
};

const uint32_t kVRAMDumpBufferBytes = 256 * 1024;
uint32_t g_vram_dump_buffer[kVRAMDumpBufferBytes / sizeof(uint32_t)];

void
dump_vram(const char *const name)
{
#define VRAM32(_offset) ((volatile uint32_t *)(0xA5000000 + (_offset)))

  char filename[256];
  sprintf(filename, "/pc/vram_%s.bin", name);
  FILE *dump_file = fopen(filename, "wb");

  if (!dump_file) {
    printf("Failed to open file %s\n", filename);
    return;
  }

  for (uint32_t vram_addr = 0; vram_addr < 8 * 1024 * 1024;
       vram_addr += kVRAMDumpBufferBytes) {

    // Read kVRAMDumpBufferBytes bytes from VRAM to the global buffer
    const uint64_t start = timer_ms_gettime64();
    for (uint32_t i = 0; i < kVRAMDumpBufferBytes / sizeof(uint32_t); ++i) {
      g_vram_dump_buffer[i] = *VRAM32(vram_addr + i * sizeof(uint32_t));
    }
    fwrite(g_vram_dump_buffer, 1, kVRAMDumpBufferBytes, dump_file);
    const uint64_t end = timer_ms_gettime64();

    printf("VRAM Dump (\"%s\") addr 0x%08X (+%d KiB): %lu ms\n",
           name,
           vram_addr,
           kVRAMDumpBufferBytes / 1024,
           end - start);
  }

  fclose(dump_file);
}

void
dump_vram_registers(const char *const name)
{
  char filename[256];
  sprintf(filename, "/pc/pvr_regs_%s.bin", name);
  FILE *dump_file = fopen(filename, "wb");

  if (!dump_file) {
    printf("Failed to open file %s\n", filename);
    return;
  }

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

  const uint64_t start = timer_ms_gettime64();

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

  const uint64_t end = timer_ms_gettime64();

  printf("PVR Reg Dump (\"%s\") %u 32b Words in %lu ms\n", name, count, end - start);
  fclose(dump_file);
}

void
init_polys()
{
  srand(0);
  for (int i = 0; i < kNumPolys; i++) {
    polys[i].x = (float)(rand() % 640);
    polys[i].y = (float)(rand() % 480);
    polys[i].z = 10 + (float)(rand() % 20);
  }
}

int
main(int argc, char **argv)
{
  maple_device_t *cont;
  cont_state_t *state;
  int finished   = 0;
  uint64 timer   = timer_ms_gettime64(), start, end;
  uint32 counter = 0;
  pvr_stats_t stats;

  init_polys();

  pvr_init(&pvr_params);

  /* Allocate our texture to be rendered to */
  d_texture = pvr_mem_malloc(1024 * 512 * 2);

  pvr_set_bg_color(0.1f, 0.1f, 0.2f);

  start = timer_ms_gettime64();

  int frame = 0;
  while (!finished) {

    cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);

    if (cont != NULL) {
      state = (cont_state_t *)maple_dev_status(cont);

      if (state != NULL && timer < timer_ms_gettime64()) {
        if (state->buttons & CONT_START)
          finished = 1;
        else if (state->buttons & CONT_A && (to_texture % 2) != 1) {
          timer = timer_ms_gettime64() + 200;
        } else if (state->buttons & CONT_B && to_texture) {
          timer = timer_ms_gettime64() + 200;
        }
      }
    }

    draw_frame();
    counter++;

    dump_vram("simple");
    dump_vram_registers("simple");
    timer_spin_sleep(500);
    break;
  }

  end = timer_ms_gettime64();

  printf("%lu frames in %llu ms = %f FPS\n",
         counter,
         end - start,
         (double)(counter / ((float)end - start) * 1000.0f));

  pvr_get_stats(&stats);
  printf("From pvr_get_stats:\n\tVBlank Count: %u\n\tFrame Count: %u\n",
         stats.vbl_count,
         stats.frame_count);

  pvr_mem_free(d_texture);

  /* Shutdown isn't technically necessary, but is possible */
  pvr_shutdown();

  return 0;
}
