#pragma once
namespace ui {
void begin();
void draw();          // full repaint
void tick();          // buttons + periodic refresh
void toast(const char *line);

int  batteryLevel();  // 0-100, or -1 when unknown
int  batteryMv();
bool charging();
}  // namespace ui
