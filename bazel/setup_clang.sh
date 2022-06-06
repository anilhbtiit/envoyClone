#!/bin/bash

BAZELRC_FILE="${BAZELRC_FILE:-$(bazel info workspace)/clang.bazelrc}"

LLVM_PREFIX=$1

if [[ ! -e "${LLVM_PREFIX}/bin/llvm-config" ]]; then
  echo "Error: cannot find llvm-config in ${LLVM_PREFIX}."
  exit 1
fi

PATH="$("${LLVM_PREFIX}"/bin/llvm-config --bindir):${PATH}"
export PATH

RT_LIBRARY_PATH="$(llvm-config --libdir)/clang/$(llvm-config --version)/lib/$(llvm-config --host-target)"

echo "# Generated file, do not edit. If you want to disable clang, just delete this file.
build:clang --action_env='PATH=${PATH}'
build:clang --action_env=CC=clang
build:clang --action_env=CXX=clang++
build:clang --action_env='LLVM_CONFIG=${LLVM_PREFIX}/bin/llvm-config'
build:clang --repo_env='LLVM_CONFIG=${LLVM_PREFIX}/bin/llvm-config'
build:clang --linkopt='-L$(llvm-config --libdir)'
build:clang --linkopt='-Wl,-rpath,$(llvm-config --libdir)'

build:clang-asan --action_env=ENVOY_UBSAN_VPTR=1
build:clang-asan --copt=-fsanitize=vptr,function
build:clang-asan --linkopt=-fsanitize=vptr,function
build:clang-asan --linkopt='-L${RT_LIBRARY_PATH}'
build:clang-asan --linkopt=-l:libclang_rt.ubsan_standalone.a
build:clang-asan --linkopt=-l:libclang_rt.ubsan_standalone_cxx.a
" >"${BAZELRC_FILE}"
