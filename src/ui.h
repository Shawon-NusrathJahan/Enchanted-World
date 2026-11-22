// ui.h - mouse state, built-in pixel font and the two corner buttons
//
// All coordinates are WORLD coordinates (x -1.78..1.78, y -1..1, y up).
// UI colours are never tinted by the time-of-day system.

#ifndef UI_H
#define UI_H

#include "gfx.h"

namespace ui
{

struct Mouse
{
    float x = 0.0f, y = 0.0f;       // world coordinates
    bool  valid    = false;         // cursor is inside the picture
    bool  down     = false;         // left button held
    bool  pressed  = false;         // went down this frame
    bool  released = false;         // went up this frame
};

inline Mouse mouse;

void endFrame();                                    // call once per frame, after drawing

// text: x,y = bottom-left of the first letter (or bottom-centre if centred); px = size of one font pixel
float textWidth(const char* s, float px);
void  drawText(float x, float y, float px, const gfx::Color& c, const char* s, bool centered = false);

bool hover(float cx, float cy, float r);            // cursor inside a circle?
bool backButton();                                  // top-left arrow, true when clicked
bool mapButton();                                   // top-right map icon, true when clicked

}   // namespace ui

#endif
