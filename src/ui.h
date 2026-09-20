#pragma once
namespace ui {
void begin();
void draw();          // full repaint
void tick();          // buttons + periodic refresh
void toast(const char *line);
}  // namespace ui
