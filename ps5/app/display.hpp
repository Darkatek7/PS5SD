// Adapted from PS5LM (github.com/cobanov/PS5LM, GPL-3.0)
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace hui::gfx {
class Renderer;
}

namespace ps5sd {

// The TV, through ps5-opengl: one OpenGL 4.6 context, presented at vsync.
class Display {
  public:
    bool open(int width, int height);
    void clear(float r, float g, float b);
    // Draws the renderer's frame into the TV's framebuffer.
    void present(hui::gfx::Renderer & renderer);
    bool swap();
    int  width() const { return width_; }
    int  height() const { return height_; }

  private:
    int width_  = 0;
    int height_ = 0;
};

}  // namespace ps5sd
