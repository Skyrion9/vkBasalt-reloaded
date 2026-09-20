#!/usr/bin/env fish

set -l fix_flag
set -l build_dir "build"

for arg in $argv
    if test "$arg" = "--fix"
        set fix_flag "--fix"
    else
        set build_dir "$arg"
    end
end

if not test -f "$build_dir/compile_commands.json"
    echo "Error: $build_dir/compile_commands.json not found" >&2
    exit 1
end

set -l files src/*.cpp

if test (count $files) -eq 0
    echo "No .cpp files found in ./src" >&2
    exit 1
end

echo "Checking "(count $files)" files..."

clang-tidy \
    -p "$build_dir" \
    --checks="-*,bugprone-*,clang-analyzer-*,performance-*,modernize-*,cppcoreguidelines-*,concurrency-*,misc-redundant-expression,misc-unused-parameters,-bugprone-easily-swappable-parameters,-bugprone-narrowing-conversions,-cppcoreguidelines-pro-type-reinterpret-cast,-cppcoreguidelines-pro-bounds-pointer-arithmetic,-cppcoreguidelines-pro-bounds-array-to-pointer-decay,-cppcoreguidelines-pro-type-vararg,-cppcoreguidelines-avoid-magic-numbers,-cppcoreguidelines-non-private-member-variables-in-classes,-cppcoreguidelines-avoid-c-arrays,-cppcoreguidelines-macro-usage,-modernize-use-trailing-return-type,-modernize-avoid-c-arrays" \
    --header-filter="src/.*" \
    $fix_flag \
    $files
