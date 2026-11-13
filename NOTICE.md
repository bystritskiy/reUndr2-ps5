# PS5 build: licences and source

The NFSU2 PS5 host and its SDL3 display bridge are MIT.
SDL3 3.4.18 is altered by `ps5/sdl/SDL_ps5vulkan.c`,
`tools/rebuild-deps.py`.

The PS5 executable additionally links PS5 Platform from the pinned
PS5_PayloadSDK fork, and uses PS5_Vulkan's native link recipe, clean-room libc
module and C++ allocation runtime. These components are GPL-3.0-or-later;
the combined PS5 executable is subject to those terms. Its third-party notices
and the upstream MIT/LGPL, SDL zlib, glslang and SPIR-V notices accompany the
local build. Mesa contains several licences; its complete `licenses/` folder
and `docs/license.rst` accompany the build.

dav1d (BSD-2-Clause) decodes upscaled movie packs when a local build has
them; it is cross-compiled unmodified by `tools/build-dav1d.py`, and its
licence accompanies the build.

The port menu uses Dear ImGui 1.92.9 (MIT, `ps5/third_party/imgui`) and the
Roboto Medium font (Apache-2.0, `ps5/third_party/fonts`), both unmodified and
built into the program; their licences accompany the build.

`recomp/` holds the recompilation this port grew from: the Switch port
antoxa2584x/nfsu2-sw (from commit d3bbc30, used with its author's
permission) with the changes made for the PS5, and the xboxrecomp toolkit it
includes (MIT, `recomp/xboxrecomp/LICENSE` and `NOTICE`; the xemu-derived
parts are LGPL-2.1-or-later, `recomp/xboxrecomp/LICENSES`).

Source revisions and repositories are listed in `ps5/dependencies.json`.
Downloaded source stays in `build/deps/`. NFSU2 modifications stay in the
published host/runtime files and scripts; generated game C is unchanged.
The installed public PayloadSDK is not modified. No Sony SDK or system module
is distributed. `libc.prx` is the reproducibly generated clean-room runtime
from PS5_Vulkan, with its expected release SHA-256 recorded by the packager.
AGC link-stub bodies are host tooling and are not packaged or executed.

The original game assets and Xbox executables remain the user's local input;
these source licences do not grant rights to those files. The package is a
local build from that input.
