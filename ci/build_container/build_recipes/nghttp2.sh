#!/bin/bash

set -e

SCRIPT_DIR="$(dirname "${BASH_SOURCE[0]}")"
source "${SCRIPT_DIR}/versions.sh"

curl "$NGHTTP2_FILE_URL" -sLo nghttp2-"$NGHTTP2_VERSION".tar.gz \
  && echo "$NGHTTP2_FILE_SHA256" nghttp2-"$NGHTTP2_VERSION".tar.gz | sha256sum --check
tar xf nghttp2-"$NGHTTP2_VERSION".tar.gz
cd "$NGHTTP2_FILE_PREFIX"

# Allow nghttp2 to build as static lib on Windows
# TODO: remove once https://github.com/nghttp2/nghttp2/pull/1198 is merged
cat > nghttp2_cmakelists.diff << 'EOF'
diff --git a/lib/CMakeLists.txt b/lib/CMakeLists.txt
index 17e422b2..e58070f5 100644
--- a/lib/CMakeLists.txt
+++ b/lib/CMakeLists.txt
@@ -56,6 +56,7 @@ if(HAVE_CUNIT OR ENABLE_STATIC_LIB)
     COMPILE_FLAGS "${WARNCFLAGS}"
     VERSION ${LT_VERSION} SOVERSION ${LT_SOVERSION}
     ARCHIVE_OUTPUT_NAME nghttp2
+    ARCHIVE_OUTPUT_DIRECTORY static
   )
   target_compile_definitions(nghttp2_static PUBLIC "-DNGHTTP2_STATICLIB")
   if(ENABLE_STATIC_LIB)
EOF

if [[ "${OS}" == "Windows_NT" ]]; then
  git apply nghttp2_cmakelists.diff
fi

mkdir build
cd build

cmake -G "Ninja" -DCMAKE_INSTALL_PREFIX="$THIRDPARTY_BUILD" \
  -DCMAKE_INSTALL_LIBDIR="$THIRDPARTY_BUILD/lib" \
  -DENABLE_STATIC_LIB=on \
  -DENABLE_LIB_ONLY=on \
  ..
ninja
ninja install

if [[ "${OS}" == "Windows_NT" ]]; then
  cp "lib/CMakeFiles/nghttp2_static.dir/nghttp2_static.pdb" "$THIRDPARTY_BUILD/lib/nghttp2_static.pdb"
fi
