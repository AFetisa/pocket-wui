#pragma once
struct SPIClass { void begin(int = -1, int = -1, int = -1, int = -1) {} void end() {} };
extern SPIClass SPI;
#define SS 5
