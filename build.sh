#!/usr/bin/env bash
set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: ./build.sh [options] [-- extra-cmake-args...]

Options:
  -t, --target NAME        CMake target to build. Default: mecraft
  -b, --build-dir PATH     Build directory. Default: cmake-build-linux-manifest
  -c, --config NAME        CMake build type. Default: Release
  -j, --jobs COUNT         Parallel build jobs. Default: nproc
      --vcpkg-root PATH    vcpkg root. Default: ./vcpkg
      --configure-only     Configure the build directory without building a target.
      --skip-configure     Build an existing configured directory.
  -h, --help               Show this help text.
EOF
}

die() {
  printf 'build.sh: %s\n' "$1" >&2
  exit 1
}

require_value() {
  local option_name="$1"
  local value="${2-}"
  [[ -n "$value" ]] || die "missing value for ${option_name}"
}

require_command() {
  local command_name="$1"
  command -v "$command_name" >/dev/null 2>&1 || die "missing command: ${command_name}"
}

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build_dir="cmake-build-linux-manifest"
build_type="Release"
target_name="mecraft"
jobs="$(nproc)"
vcpkg_root="$script_dir/vcpkg"
configure_only=0
skip_configure=0
extra_cmake_args=()

while [[ $# -gt 0 ]]; do
  case "$1" in
  -t | --target)
    require_value "$1" "${2-}"
    target_name="$2"
    shift 2
    ;;
  -b | --build-dir)
    require_value "$1" "${2-}"
    build_dir="$2"
    shift 2
    ;;
  -c | --config)
    require_value "$1" "${2-}"
    build_type="$2"
    shift 2
    ;;
  -j | --jobs)
    require_value "$1" "${2-}"
    jobs="$2"
    shift 2
    ;;
  --vcpkg-root)
    require_value "$1" "${2-}"
    vcpkg_root="$2"
    shift 2
    ;;
  --configure-only)
    configure_only=1
    shift
    ;;
  --skip-configure)
    skip_configure=1
    shift
    ;;
  -h | --help)
    show_help
    exit 0
    ;;
  --)
    shift
    extra_cmake_args+=("$@")
    break
    ;;
  *)
    die "unknown option: $1"
    ;;
  esac
done

[[ "$configure_only" -eq 0 || "$skip_configure" -eq 0 ]] ||
  die "--configure-only and --skip-configure cannot be used together"

[[ "$build_dir" = /* ]] || build_dir="$script_dir/$build_dir"
[[ "$vcpkg_root" = /* ]] || vcpkg_root="$script_dir/$vcpkg_root"

toolchain_file="$vcpkg_root/scripts/buildsystems/vcpkg.cmake"

require_command cmake
require_command ninja
[[ -f "$script_dir/vcpkg.json" ]] || die "missing manifest: $script_dir/vcpkg.json"
[[ -f "$toolchain_file" ]] || die "missing vcpkg toolchain: $toolchain_file"
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || die "jobs must be a positive integer"

if [[ "$skip_configure" -eq 0 ]]; then
  printf 'Configuring %s\n' "$build_dir"
  cmake -S "$script_dir" -B "$build_dir" -G Ninja \
    -DMECRAFT_BUILD_TESTS=OFF \
    -DCMAKE_BUILD_TYPE="$build_type" \
    -DMECRAFT_VCPKG_ROOT="$vcpkg_root" \
    "${extra_cmake_args[@]}"
fi

if [[ "$configure_only" -eq 0 ]]; then
  printf 'Building target %s with %s job(s)\n' "$target_name" "$jobs"
  cmake --build "$build_dir" --target "$target_name" -j "$jobs"

  if [[ "$target_name" == "mecraft" && -x "$build_dir/mecraft" ]]; then
    printf 'Built executable: %s\n' "$build_dir/mecraft"
  fi
fi
