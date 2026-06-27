#pragma once

#include <Geode/platform/cplatform.h>

# if defined(GEODE_IS_MACOS)
#  undef GL_DO_NOT_WARN_IF_MULTI_GL_VERSION_HEADERS_INCLUDED
#  define GL_DO_NOT_WARN_IF_MULTI_GL_VERSION_HEADERS_INCLUDED
#  include <OpenGL/gl3.h>
#  include <OpenGL/gl3ext.h>
# elif defined (GEODE_IS_ANDROID)
#  include <Geode/cocos/platform/CCGL.h>
#  include <EGL/egl.h>
# elif defined (GEODE_IS_IOS)
#  include <OpenGLES/ES3/gl.h>
#  include <OpenGLES/ES3/glext.h>
# else
#  include <Geode/cocos/platform/CCGL.h>
# endif
