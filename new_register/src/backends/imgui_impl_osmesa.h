#pragma once
#include <string>

// Initialize an OSMesa offscreen OpenGL context with a pixel buffer of the
// given dimensions.  Returns false on failure (treat as hard error).
bool osmesa_init(int width = 1280, int height = 720);

// Destroy the OSMesa context and free the pixel buffer.
void osmesa_shutdown();

// Encode the current pixel buffer as PNG and write it to 'path'.
void osmesa_save_png(const std::string& path);
