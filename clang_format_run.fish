#!/usr/bin/env fish

# clang-format runner for vkBasalt
# Usage: ./run_format.fish [--check]
# Run from project root (same level as src/)
# Configuration is read from .clang-format automatically.

set -l dry_run false

for arg in $argv
    switch $arg
        case --check --dry-run -n
            set dry_run true
    end
end

if not command -q clang-format
    echo "Error: clang-format not found in PATH" >&2
    exit 1
end

if not test -f .clang-format
    echo "Error: .clang-format not found in project root." >&2
    echo "Refusing to run; without it clang-format would fall back to default LLVM style." >&2
    exit 1
end

set -l files (find src -maxdepth 1 -type f \( -name "*.cpp" -o -name "*.hpp" \) | sort)

if test (count $files) -eq 0
    echo "No .cpp or .hpp files found in ./src" >&2
    exit 1
end

if test "$dry_run" = true
    echo "Checking "(count $files)" files (dry run, no changes)..."
    clang-format --dry-run --Werror $files
    set -l rc $status
    if test $rc -eq 0
        echo "All files are correctly formatted."
    else
        echo "" >&2
        echo "Files above need formatting. Run without --check to apply." >&2
    end
    exit $rc
end

echo "Formatting "(count $files)" files in place..."
clang-format -i $files
echo "Done."