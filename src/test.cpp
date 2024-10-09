#include <cstdio>
#include <cstdlib>
#include <cstdarg>

#include <sys/stat.h>

#include "./pvr.h"
#include "test.h"

std::vector<TestDefinition> _g_test_list;

std::vector<TestDefinition> &
get_test_list()
{
  return _g_test_list;
}

void
TestContext::assert(bool condition, const char *msg)
{
  if (!condition) {
    host_printf("TEST ASSERT FAILED: %s\n", msg);
    exit(-1);
  }
}

void
TestContext::create_test_folder()
{
  char folder_name[256];
  snprintf(folder_name, sizeof(folder_name), "/pc/%s", _name);

  struct stat st = { 0 };
  if (stat(folder_name, &st) == -1) {
    if (mkdir(folder_name, 0700) < 0) {
      host_printf("Failed to create test folder '%s'\n", folder_name);
      exit(-1);
    }
  }
}

void
TestContext::host_printf(const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
}

void
TestContext::log(const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  vfprintf(_log_file, fmt, args);
  va_end(args);
}

void
TestContext::pvr_reg_vram_dump(const char *dump_name)
{
  char file_name[256];

  // create_test_folder();

  snprintf(file_name, sizeof(file_name), "/pc/%s__%s.vram", _name, dump_name);
  host_printf(" - Dumping VRAM to %s\n", file_name);
  FILE *dump_file = fopen(file_name, "wb");
  pvr_dump_vram(dump_file);
  fclose(dump_file);

  snprintf(file_name, sizeof(file_name), "/pc/%s__%s.pvr_regs", _name, dump_name);
  host_printf(" - Dumping PVR regs to %s\n", file_name);
  dump_file = fopen(file_name, "wb");
  pvr_dump_regs(dump_file);
  fclose(dump_file);
}

void
TestContext::write_framebuffer_ppm(const char *fb_name, FrameBufferDef fb_dev)
{
  char file_name[256];
  snprintf(file_name, sizeof(file_name), "/pc/%s__%s.ppm", _name, fb_name);
  host_printf(" - Dumping framebuffer PPM to '%s'\n", file_name);

  FILE *file = fopen(file_name, "wb");
  
  if (fb_dev.pixel_format == framebuffer_pixel_format_t::ARGB_8888) {
    fprintf(file, "P6\n%lu %lu\n255\n", fb_dev.width, fb_dev.height);
    for (uint32_t y = 0; y < fb_dev.height; ++y) {
      for (uint32_t x = 0; x < fb_dev.width; ++x) {
        const uint32_t fb_offset = (y * fb_dev.width + x) * 4;
        const uint32_t word = vram32_read32(fb_dev.vram32_addr + fb_offset);
        const uint8_t r = (word >> 16) & 0xff;
        const uint8_t g = (word >> 8) & 0xff;
        const uint8_t b = (word >> 0) & 0xff;
        fwrite(&r, 1, 1, file);
        fwrite(&g, 1, 1, file);
        fwrite(&b, 1, 1, file);
      }
    }

  } else {
    host_printf("ERROR Unsupported framebuffer pixel format for write_framebuffer_ppm %u\n", (uint32_t)fb_dev.pixel_format);
  }

  fclose(file);
}
