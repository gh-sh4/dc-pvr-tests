#pragma once

#include <cstdint>

#define PVR_REG(addr) (*(volatile uint32_t *)(0xa05f'0000 | addr))

#define SOFTRESET PVR_REG(0x8008)
#define STARTRENDER PVR_REG(0x8014)
#define PARAM_BASE PVR_REG(0x8020)
#define REGION_BASE PVR_REG(0x802c)
#define FB_R_CTRL PVR_REG(0x8044)
#define FB_W_CTRL PVR_REG(0x8048)
#define FB_W_LINE_STRIDE PVR_REG(0x804C)
#define FB_R_SOF1 PVR_REG(0x8050)
#define FB_R_SOF2 PVR_REG(0x8054)
#define FB_R_SIZE PVR_REG(0x805C)
#define FB_W_SOF1 PVR_REG(0x8060)
#define FB_W_SOF2 PVR_REG(0x8064)
#define FB_X_CLIP PVR_REG(0x8068)
#define FB_Y_CLIP PVR_REG(0x806C)
#define FPU_SHAD_SCALE PVR_REG(0x8074)
#define FPU_CULL_VAL PVR_REG(0x8078)
#define FPU_PARAM_CFG PVR_REG(0x807C)
#define HALF_OFFSET PVR_REG(0x8080)
#define FPU_PERP_VAL PVR_REG(0x8084)
#define ISP_BACKGND_D PVR_REG(0x8088)
#define ISP_BACKGND_T PVR_REG(0x808C)
#define ISP_FEED_CFG PVR_REG(0x8098)

#define FOG_CLAMP_MAX PVR_REG(0x80BC)
#define FOG_CLAMP_MIN PVR_REG(0x80C0)

#define SCALER_CTL PVR_REG(0x80F4)

#define FB_BURSTCTRL PVR_REG(0x8110)

#define SDRAM_REFRESH PVR_REG(0x80A0)
#define SDRAM_ARB_CFG PVR_REG(0x80A4)
#define SDRAM_CFG PVR_REG(0x80A8)

#define Y_COEFF PVR_REG(0x8118)

#define TA_OL_BASE PVR_REG(0x8124)
#define TA_ISP_BASE PVR_REG(0x8128)
#define TA_OL_LIMIT PVR_REG(0x812c)
#define TA_ISP_LIMIT PVR_REG(0x8130)
#define TA_ITP_CURRENT PVR_REG(0x8138)
#define TA_GLOB_TILE_CLIP PVR_REG(0x813c)
#define TA_ALLOC_CTRL PVR_REG(0x8140)
#define TA_LIST_INIT PVR_REG(0x8144)
#define TA_LIST_CONT PVR_REG(0x8160)
#define TA_NEXT_OPB_INIT PVR_REG(0x8164)

union TA_OL_POINTERS_t {
  struct {
    uint32_t skip : 2;
    uint32_t addr : 22;
    uint32_t shadow : 1;
    uint32_t number : 4;
    uint32_t triangle : 1;
    uint32_t sprite : 1;
    uint32_t entry : 1;
  };
  uint32_t raw;
};
static_assert(sizeof(TA_OL_POINTERS_t) == sizeof(uint32_t));

namespace map {
const uint32_t TA_POLYGON_FIFO = 0x1000'0000;
}

struct isp_backgnd_plane_t {
  uint32_t isp_tsp_control;
  uint32_t tsp_control;
  uint32_t texture_control;
  float x1, y1, z1;
  uint32_t color1;
  float x2, y2, z2;
  uint32_t color2;
  float x3, y3, z3;
  uint32_t color3;
};
static_assert(sizeof(isp_backgnd_plane_t) == 4 * (3 + 4 + 4 + 4));

TA_OL_POINTERS_t pvr_read_ta_ol_pointers(unsigned index);

void vram32_write32(uint32_t vram_addr, uint32_t data);
uint32_t vram32_read32(uint32_t vram_addr);
void vram32_memcpy(uint32_t vram_addr, const uint32_t *data, uint32_t num_bytes);
void vram32_memset(uint32_t vram_addr, uint32_t data, uint32_t num_bytes);

void pvr_dump_vram(FILE *dump_file);
void pvr_dump_regs(FILE *dump_file);
