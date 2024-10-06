#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <unordered_map>
#include <cstdlib>
#include <vector>

enum PVRReg : uint32_t
{
  PVR_PARAM_BASE  = 0x005f'8020,
  PVR_REGION_BASE = 0x005f'802c,
  PVR_FB_R_CTRL   = 0x005f'8044,
  PVR_FB_R_SOF1   = 0x005f'8050,
};

union RegionArrayControlWord {
  struct {
    uint32_t _0 : 2;
    uint32_t tile_x : 6;
    uint32_t tile_y : 6;
    uint32_t _1 : 14;
    uint32_t flush : 1;
    uint32_t presort : 1;
    uint32_t zclear : 1;
    uint32_t last : 1;
  };
  uint32_t raw;
};

union RegionArrayListPtr {
  struct {
    uint32_t _0 : 2;
    uint32_t list_ptr : 22;
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

union OL_TriangleStrip {
  struct {
    uint32_t start_address : 21;
    uint32_t skip : 3;
    uint32_t shadow : 1;
    uint32_t mask : 6;
    uint32_t _0 : 1;
  };
  uint32_t raw;
};

union OL_TriangleArray {
  struct {
    uint32_t start_address : 21;
    uint32_t skip : 3;
    uint32_t shadow : 1;
    uint32_t count : 4;
    uint32_t _0 : 3;
  };
  uint32_t raw;
};

union OL_QuadArray {
  struct {
    uint32_t start_address : 21;
    uint32_t skip : 3;
    uint32_t shadow : 1;
    uint32_t count : 4;
    uint32_t _0 : 3;
  };
  uint32_t raw;
};

union OL_BlockLink {
  struct {
    uint32_t _0 : 2;
    uint32_t _addr : 22;
    uint32_t _1 : 4;
    uint32_t end_of_list : 1;
    uint32_t _2 : 3;
  };
  uint32_t raw;

  uint32_t addr() const
  {
    return _addr << 2;
  }
};

void
die(const char *msg)
{
  printf("die/error : %s\n", msg);
  exit(1);
}

void
check_die(bool condition, const char *msg)
{
  if (condition) {
    die(msg);
  }
}

using BinaryString = char[7];
std::array<BinaryString, 64> g_binary_strings;
void
init_binary_strings()
{
  for (uint32_t i = 0; i < 64; ++i) {
    for (uint32_t j = 0; j < 6; ++j) {
      g_binary_strings[i][j] = (i & (1 << (5 - j))) ? '1' : '0';
    }
    g_binary_strings[i][6] = '\0';
  }
}

class Dump {
public:
  Dump(const char *name)
  {
    // Read VRAM
    char filename[256];
    sprintf(filename, "vram_%s.bin", name);
    FILE *vram_file = fopen(filename, "rb");
    if (!vram_file) {
      printf("Failed to open file %s\n", filename);
      return;
    }
    _vram.reset(new uint8_t[8 * 1024 * 1024]);
    fread(_vram.get(), 1, 8 * 1024 * 1024, vram_file);

    // Read PVR MMIO Registers
    sprintf(filename, "pvr_regs_%s.bin", name);
    FILE *regs_file = fopen(filename, "rb");
    if (!regs_file) {
      printf("Failed to open file %s\n", filename);
      return;
    }
    while (!feof(regs_file)) {
      uint32_t address;
      uint32_t reg;
      fread(&address, 1, sizeof(uint32_t), regs_file);
      fread(&reg, 1, sizeof(uint32_t), regs_file);
      printf("PVR reg 0x%08x = 0x%08x\n", address, reg);
      _pvr_regs[address & 0x005f'ffff] = reg;
    }
  }

  uint32_t read_reg(uint32_t addr) const
  {
    auto it = _pvr_regs.find(addr);
    check_die(it == _pvr_regs.end(), "Invalid PVR register read");
    return it->second;
  }

  uint32_t read_vram32(uint32_t addr) const
  {
    check_die(addr >= 8 * 1024 * 1024, "Invalid VRAM read");
    return *(uint32_t *)(_vram.get() + addr);
  }

  void memcpy(void *dest, uint32_t source_offset, uint32_t num_bytes) const
  {
    check_die(source_offset + num_bytes >= 8 * 1024 * 1024, "Invalid VRAM read");
    std::memcpy(dest, _vram.get() + source_offset, num_bytes);
  }

private:
  std::unique_ptr<uint8_t> _vram;
  std::unordered_map<uint32_t, uint32_t> _pvr_regs;
};

void
process_object_list(const Dump &dump, uint32_t start_address)
{
  union {
    OL_TriangleStrip tri_strip;
    OL_TriangleArray tri_array;
    OL_QuadArray quad_array;
    OL_BlockLink block_link;
    uint32_t raw;
  } entry;
  static_assert(sizeof(entry) == sizeof(uint32_t));
  entry.raw = 0;

  uint32_t count = 0;

  while (!entry.block_link.end_of_list) {
    dump.memcpy(&entry, start_address, sizeof(entry));

    printf("    * OL Entry[%04u] @ 0x%08x : ", count++, start_address);
    if ((entry.raw >> 31) == 0) {
      printf("Triangle Strip @ 0x%06x, Skip %u, Shadow %u, Mask %s\n",
             entry.tri_strip.start_address,
             entry.tri_strip.skip,
             entry.tri_strip.shadow,
             g_binary_strings[entry.tri_strip.mask]);

      start_address += sizeof(entry);
    } else if ((entry.raw >> 29) == 0b100) {
      printf("Triangle Array @ 0x%06x, Skip %u, Shadow %u, Count %u\n",
             entry.tri_array.start_address,
             entry.tri_array.skip,
             entry.tri_array.shadow,
             entry.tri_array.count + 1);

      start_address += sizeof(entry);
    } else if ((entry.raw >> 29) == 0b101) {
      printf("Quad Array @ 0x%06x, Skip %u, Shadow %u, Count %u\n",
             entry.quad_array.start_address,
             entry.quad_array.skip,
             entry.quad_array.shadow,
             entry.quad_array.count + 1);

      start_address += sizeof(entry);
    } else if ((entry.raw >> 29) == 0b111) {
      if (entry.block_link.end_of_list) {
        printf("End of List\n");
      } else {
        printf("Block Link, Next OPB @ 0x%06x\n", entry.block_link.addr());
      }

      start_address = entry.block_link.addr();
    } else {
      die("Invalid OL Entry");
    }
  }
}

void
process_region_array(const Dump &dump)
{
  uint32_t region_array_entry_ptr = dump.read_reg(PVR_REGION_BASE);
  printf("Region Array @ 0x%08x\n", region_array_entry_ptr);

  uint32_t counter       = 0;
  RegionArrayEntry entry = {};

  while (!entry.control.last) {
    dump.memcpy(&entry, region_array_entry_ptr, sizeof(entry));
    region_array_entry_ptr += sizeof(entry);

    printf("Entry[%u] @ 0x%08x @ (%3u,%3u)  - ",
           counter++,
           region_array_entry_ptr,
           32 * entry.control.tile_x,
           32 * entry.control.tile_y);

    // clang-format off
    printf(" Control 0x%08x : ", entry.control.raw);
    if (!entry.control.zclear)  { printf("Z-Clear ");    }
    if (!entry.control.presort) { printf("Auto-Sort ");  }
    if (!entry.control.flush)   { printf("Flush ");      }
    if ( entry.control.last)    { printf("Last-Entry "); }
    printf("\n");
    // clang-format on

    static const char *const kListName[] = {
      "Opaque", "Opaque Mod Vol", "Trans", "Trans Mod Vol", "Punchthrough",
    };

    for (unsigned i = 0; i < 5; ++i) {
      if (entry.lists[i].empty) {
        continue;
      }
      const uint32_t list_ptr = entry.lists[i].raw & 0x7fff'fffb;
      printf(" - %s List : OPB @ 0x%08x\n", kListName[i], list_ptr);
      process_object_list(dump, list_ptr);
    }

    printf("\n");
  }
}

void
dump_framebuffer(const Dump &dump, const char *name)
{
  const uint32_t fb_start = dump.read_reg(PVR_FB_R_SOF1);
  printf("Framebuffer Start @ 0x%08x\n", fb_start);

  const uint32_t FB_R_CTRL = dump.read_reg(PVR_FB_R_CTRL);
  const uint32_t fb_depth  = (FB_R_CTRL >> 2) & 0b11;
  const uint32_t fb_concat = (FB_R_CTRL >> 4) & 0b111;

  const uint32_t width = 640, height = 480;
  const uint32_t stride            = width * 2;
  const uint32_t pixel_sizes[]     = { 2, 2, 3, 4 };
  const uint32_t pixel_bytes       = pixel_sizes[fb_depth];
  const uint32_t framebuffer_bytes = stride * height * pixel_bytes;

  if (fb_start + framebuffer_bytes > 8 * 1024 * 1024) {
    die("Invalid framebuffer");
  }

  if (pixel_bytes != 2) {
    die("Unsupported pixel format");
  }

  std::vector<uint8_t> fb_data(width * height * pixel_bytes);
  dump.memcpy(fb_data.data(), fb_start, width * height * pixel_bytes);

  FILE *fb_file = fopen("fb.ppm", "wb");
  fprintf(fb_file, "P6\n%u %u\n255\n", width, height);
  // Iterate each pixel converting native format to RGB888 for PPM
  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      const uint32_t fb_offset = (y * width + x) * pixel_bytes;
      const uint16_t argb1555  = *(uint16_t *)(fb_data.data() + fb_offset);
      uint8_t r, g, b;

      // Apply fb_concat (page 363)
      switch (fb_depth) {
        case 0b00: // 0555 RGB 16b
          r = ((argb1555 >> 10) & 0x1f);
          g = ((argb1555 >> 5) & 0x1f);
          b = ((argb1555 >> 0) & 0x1f);

          r = (r << 3) | fb_concat;
          g = (g << 3) | fb_concat;
          b = (b << 3) | fb_concat;
          break;
        case 0b01: // 565 RGB 16b
          r = ((argb1555 >> 11) & 0x1f);
          g = ((argb1555 >> 5) & 0x3f);
          b = ((argb1555 >> 0) & 0x1f);

          r = (r << 3) | fb_concat;
          g = (g << 2) | fb_concat;
          b = (b << 3) | fb_concat;
          break;
        case 0b10: // 888 RGB 32b
          break;
        case 0b11: // 0888 RGB 32b
          break;
      }

      fputc(r, fb_file);
      fputc(g, fb_file);
      fputc(b, fb_file);
    }
  }
  fclose(fb_file);
}

int
main(int argc, char **argv)
{
  init_binary_strings();

  printf("PVR Dump Interpreter\n");
  Dump dump("ta_basic_single_poly_post");

  process_region_array(dump);

  dump_framebuffer(dump, "simple");

  return 0;
}