# Static libraries on the dynamic MSVC runtime, release only.
# Same as upstream's x64-windows-v3-static-md-release without /arch:AVX2, so
# the portable build also runs on CPUs older than x86-64-v3.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)
