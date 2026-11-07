cmake_minimum_required(VERSION 3.24)
project(nfsu2_ps5 C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_C_COMPILER_TARGET STREQUAL "x86_64-sie-ps5")
 message(FATAL_ERROR "Use cmake/ps5.cmake")
endif()
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(PS5_DEPS "${ROOT}/build/deps" CACHE PATH "PS5 dependencies")
set(PS5_SDL_BUILD "${ROOT}/build/sdl3" CACHE PATH "SDL3 build")
set(PS5_SHADER_BUILD "${ROOT}/build/shader-compiler" CACHE PATH "glslang build")
set(RT "${ROOT}/upstream/xboxrecomp/src")
set(GEN "${ROOT}/build/gen")
file(GLOB generated CONFIGURE_DEPENDS "${GEN}/*.c")
if(NOT generated)
 message(FATAL_ERROR "Run tools/prepare.py first")
endif()
set(runtime platform/win32_compat.c kernel/kernel_path.c kernel/kernel_pool.c kernel/kernel_rtl.c
 kernel/kernel_memory.c kernel/kernel_file.c kernel/kernel_thread.c kernel/kernel_sync.c kernel/kernel_hal.c
 kernel/kernel_xbox.c kernel/kernel_ob.c kernel/kernel_io.c kernel/kernel_crypto.c kernel/kernel_thunks.c
 kernel/kernel_bridge.c kernel/guest_vmem.c kernel/xbox_memory_layout.c kernel/icall_feedback.c
 kernel/recomp_trace.c kernel/nv2a_pb_scan.c kernel/nv2a_pb_exec.c kernel/xbox_devbus.c
 audio/dsound_device.c audio/wma_decoder.c apu/apu_core.c apu/apu_vp.c apu/apu_dsp.c apu/apu_mmio_hook.c
 usb/ohci.c usb/usb_gamepad.c video/video_player.c video/video_pump.c video/fb_present.c d3d/d3d8_null.c
 nv2a/nv2a_core.c nv2a/nv2a_pgraph_d3d11.c nv2a/nv2a_pb_replay.c nv2a/nv2a_mmio_hook.c nv2a/nv2a_pb_test.c
 nv2a_vk/nv2a_vk.c nv2a_gl/gl_vsh.c nv2a_gl/gl_psh.c)
list(TRANSFORM runtime PREPEND "${RT}/")
add_library(nfsu2_runtime STATIC ${runtime})
add_library(nfsu2_game STATIC "${ROOT}/upstream/src/main.c" "${ROOT}/upstream/src/recomp_manual.c"
 "${ROOT}/upstream/src/text_patch.c" "${ROOT}/upstream/src/movie_crop.c" "${ROOT}/upstream/src/nic.c" ${generated})
add_library(nfsu2_host STATIC src/memory_ps5.c src/input_ps5.c src/pad_map.c src/audio_ps5.c src/sha256.c src/xbe_check.c src/hd_textures.c src/fmv_replace.c src/fmv_video.c src/disc_image.c src/screen_ps5.c)
add_library(nfsu2_start OBJECT src/native_start.c src/main_ps5.c "${ROOT}/build/vulkan_shim.c")
foreach(t nfsu2_runtime nfsu2_game nfsu2_host nfsu2_start)
 target_compile_definitions(${t} PRIVATE NFSU2_PS5=1 NFSU2_PS5_DIRECT_MEMORY=1 NFSU2_VULKAN=1)
 target_compile_options(${t} PRIVATE -O2 -fwrapv -fsigned-char -ffp-contract=off -fno-strict-aliasing -ffunction-sections -fdata-sections -Wno-format -Wno-pointer-sign -Wno-parentheses-equality -Wno-unused-value)
 target_include_directories(${t} PRIVATE compat src "${ROOT}/upstream/src" "${GEN}"
 "${ROOT}/upstream/xboxrecomp/include" "${RT}" "${RT}/platform" "${RT}/kernel" "${RT}/apu"
 "${RT}/nv2a" "${RT}/nv2a_vk" "${RT}/nv2a_gl" "${RT}/input" "${RT}/video" "${RT}/audio" "${RT}/d3d" "${RT}/usb"
 "${PS5_DEPS}/PS5_Mesa/include" "${PS5_DEPS}/sdl3/include" "${PS5_DEPS}/glslang"
 "${PS5_DEPS}/PS5_Vulkan/.deps/native/ps5-payload-sdk/target/include")
endforeach()
target_include_directories(nfsu2_host PRIVATE "${PS5_DEPS}/dav1d/include" "${ROOT}/build/dav1d/obj/include")
set_source_files_properties(${generated} PROPERTIES COMPILE_OPTIONS "-O1;-w")
