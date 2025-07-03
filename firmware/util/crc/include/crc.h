#pragma once

#include <cstdint>
#include <hal.h>

uint8_t crc8(const uint8_t * buf, uint8_t len);
uint32_t crc32(const void *buf, uint32_t size);
uint32_t crc32inc(const void *buf, uint32_t crc, uint32_t size);

static const CRCConfig crc32_cfg = {
  .poly_size       = 32,
  .poly            = 0x04C11DB7,
  .initial_val     = 0xFFFFFFFF,
  .final_val       = 0xFFFFFFFF,
  .reflect_data    = true,
  .reflect_remainder= true,
  .end_cb			= nullptr,
};

static const CRCConfig crc8_cfg = {
  .poly_size        = 8,
  .poly             = 0x1D,
  .initial_val      = 0xFF,
  .final_val        = 0xFF,
  .reflect_data     = false,
  .reflect_remainder= false,
  .end_cb		    = nullptr,
};