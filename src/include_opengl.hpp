#pragma once

#include <config.hpp>
//
#if   LUX_GL_VARIANT == LUX_GL_VARIANT_2_1
#   include <glad/glad-2.1.h>
#elif LUX_GL_VARIANT == LUX_GL_VARIANT_ES_2_0
#   include <glad/glad-es-2.0.h>
#endif
