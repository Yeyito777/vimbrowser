// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_GL_TEST_GL_TEST_HELPER_H_
#define UI_GL_TEST_GL_TEST_HELPER_H_

#include <stdint.h>

#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gl/gl_bindings.h"
#include "ui/gl/gl_context.h"
#include "ui/gl/gl_surface.h"


namespace gl {

class GLTestHelper {
 public:
  // Creates a texture object.
  // Does not check for errors, always returns texture.
  static GLuint CreateTexture(GLenum target);

  // Creates a framebuffer, attaches a color buffer, and checks for errors.
  // Returns framebuffer, 0 on failure.
  static GLuint SetupFramebuffer(int width, int height);

  static std::pair<scoped_refptr<GLSurface>, scoped_refptr<GLContext>>
  CreateOffscreenGLSurfaceAndContext();

};

}  // namespace gl

#endif  // UI_GL_TEST_GL_TEST_HELPER_H_
