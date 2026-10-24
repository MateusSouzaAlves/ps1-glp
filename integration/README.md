# PS1 native integration sources

The C++ session and positional CHD adapters and their native headers are
provided as component source. They use the pinned core libretro API and
libchdr. Java player/lifecycle and remaining app integration are not included
and are not certified as a GPL-compatible combined distribution here.
The existing build-arm64.ps1 builds the core, not a standalone Android app.

The app-authored native adapter sources in this refresh and this recipe are
available under GPL-2.0-or-later; original upstream per-file notices prevail.
Configure `cmake -S integration -B <fresh-build> -G Ninja` with the Android
NDK toolchain, arm64-v8a/API26 and `-DEMUORBIT_BUILD_SEED=<own-build-seed>`.
Build targets `emuorbit_ps1_session` and `emuorbit_ps1_chd`. This builds
native components, not the omitted Java host or an Android application.
