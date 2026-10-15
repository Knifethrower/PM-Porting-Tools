#!/bin/bash
# MESA_EXTENSION_OVERRIDE for recording a game on Mesa (as GL 2.1) the way it would run on gl4es: turns off
# extensions whose functions or behaviour gl4es does not have, so the recorded calls replay on gl4es.
# (Turning off everything gl4es does not list stops Unity 4: "GLSL support is required".)
# Add to this list when a replay on gl4es stops at "unavailable function".
# Unity 4 on Mesa refuses to start (GLSL "not supported") without PBO, VAO, debug output or program binary: kept.
echo "-GL_ARB_sync -GL_ARB_texture_filter_anisotropic -GL_EXT_texture_filter_anisotropic"      "-GL_EXT_framebuffer_multisample -GL_EXT_framebuffer_multisample_blit_scaled -GL_ARB_framebuffer_sRGB"      "-GL_EXT_framebuffer_sRGB -GL_ARB_texture_float -GL_ARB_texture_rg -GL_ARB_timer_query -GL_EXT_timer_query"      "-GL_ARB_occlusion_query -GL_ARB_map_buffer_range"
