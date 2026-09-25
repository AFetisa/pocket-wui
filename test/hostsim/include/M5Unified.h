#pragma once
// Only what storage.cpp asks of M5Unified: the SD pin map and the board.
#include <Arduino.h>
namespace m5 {
enum class pin_name_t { sd_spi_sclk, sd_spi_mosi, sd_spi_miso, sd_spi_ss };
enum class board_t { board_unknown, board_M5CardputerADV };
}
struct SimM5 {
  int getPin(m5::pin_name_t p) { return 10 + (int)p; }
  m5::board_t getBoard() { return m5::board_t::board_unknown; }
};
extern SimM5 M5;
