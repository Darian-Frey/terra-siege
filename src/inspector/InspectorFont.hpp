#pragma once

#include "raylib.h"

// ====================================================================
// InspectorFont — TTF text rendering for the inspector.
//
// raylib's bundled DrawText() uses a tiny 10-px pixel font that's
// hard to read at the sizes the inspector needs (14-20 px). Same
// pattern as the game's HUD font (GameState::initHudFont): try a few
// well-known DejaVuSansMono paths, fall back to the raylib default
// if none are present.
//
// `drawText` and `measureText` are drop-in replacements for raylib's
// DrawText / MeasureText — they route through DrawTextEx when a
// TTF is loaded, and call raylib's defaults otherwise. Every
// inspector file that wants the nicer font swaps its DrawText calls
// for `drawText`; raw raylib DrawText still works if you really want
// the pixel font.
// ====================================================================
namespace tsmesh {

// Load on inspector startup (after InitWindow so the GL context is
// up). Idempotent.
void loadInspectorFont();

// Unload on shutdown (before CloseWindow).
void unloadInspectorFont();

// Replacements for raylib's DrawText / MeasureText. Match raylib's
// signature shape so call sites mostly just need a rename.
void drawText(const char *text, int x, int y, int size, Color color);
int measureText(const char *text, int size);

} // namespace tsmesh
