#!/usr/bin/env python3

"""
This script automatically applies code modifications in a simplyfied patch format.
To ensure the script works correctly, changes should be in the following strict format:

1. Specify the file being modified using a header line starting with `#`s (e.g., `### `) followed by the file path.
   (An optional index like `[7] ` before the file path is also supported).
2. Provide the code changes inside a markdown code block (e.g., ```cpp ... ```).
3. Use `<<<<` to mark the beginning of the existing code to find.
4. Use `====` to mark the separator between the existing code and the new code.
5. Use `>>>>` to mark the end of the new code.

Example:

### [1] path/to/file.cpp
```cpp
<<<<
    void OldFunction() {
        doSomething();
    }
    void ReferenceLineFunction() {
====
    void NewFunction() {
        doSomethingBetter();
    }
    void ReferenceLineFunction() {
>>>>
```

HOW TO RUN:
python3 tools/simple-patch.py patch.txt
or
Get-Clipboard | python3 tools/simple-patch.py
"""

import sys
import os
import re
import subprocess

def get_git_root():
    """Find the git root directory by calling git or traversing parent directories."""
    # 1. Try using git command
    try:
        result = subprocess.run(
            ['git', 'rev-parse', '--show-toplevel'],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=True
        )
        return result.stdout.strip()
    except (subprocess.SubprocessError, FileNotFoundError):
        pass

    # 2. Fallback: Manual traversal looking for .git
    current_dir = os.path.abspath(os.getcwd())
    while True:
        if os.path.isdir(os.path.join(current_dir, '.git')):
            return current_dir
        parent_dir = os.path.dirname(current_dir)
        if parent_dir == current_dir:
            break # Reached filesystem root
        current_dir = parent_dir

    # 3. Default to current working directory if not in a git repo
    print("Warning: Could not detect a git root. Defaulting to current working directory.")
    return os.getcwd()

def normalize_newlines(text):
    return text.replace('\r\n', '\n')

def shorten_text(text, max_lines=6):
    lines = text.strip().split('\n')
    if len(lines) <= max_lines:
        return text.strip()
    return '\n'.join(lines[:3] + ['... [snip] ...'] + lines[-3:])

def apply_patches(patch_content):
    # Apply patches relative to the repository root
    git_root = get_git_root()
    os.chdir(git_root)
    print(f"Applying patches from repo root: {git_root}")

    # Find all file blocks: ### filepath OR #### [index] filepath
    # The regex ignores an optional `[digits] ` block before capturing the filepath
    file_pattern = re.compile(r'^#{3,}\s*(?:\[\d+\]\s*)?(.+)$', re.MULTILINE)

    file_matches = list(file_pattern.finditer(patch_content))
    if not file_matches:
        print("No file headers found in the provided patch content.")
        return

    errors = []

    for i, match in enumerate(file_matches):
        filepath = match.group(1).strip()
        start_idx = match.end()
        end_idx = file_matches[i+1].start() if i + 1 < len(file_matches) else len(patch_content)

        block_content = patch_content[start_idx:end_idx]

        # Extract all <<<< ==== >>>> blocks
        hunk_pattern = re.compile(r'<<<<\n(.*?)\n====\n(.*?)\n>>>>', re.DOTALL)
        hunks = hunk_pattern.findall(block_content)

        if not hunks:
            continue

        if not os.path.exists(filepath):
            errors.append(f"ERROR: File not found: {filepath}")
            continue

        with open(filepath, 'r', encoding='utf-8') as target_file:
            original_text = normalize_newlines(target_file.read())

        modified_text = original_text
        file_modified = False

        for find_block, replace_block in hunks:
            find_block_norm = normalize_newlines(find_block)
            replace_block_norm = normalize_newlines(replace_block)

            # Try exact match first
            if find_block_norm in modified_text:
                modified_text = modified_text.replace(find_block_norm, replace_block_norm, 1)
                file_modified = True
            else:
                # If exact match fails, log an error
                err_msg = (
                    f"ERROR: Replace failed in {filepath}\n"
                    f"--- COULD NOT FIND ---\n"
                    f"{shorten_text(find_block_norm)}\n"
                    f"--- SUGGESTED REPLACEMENT ---\n"
                    f"{shorten_text(replace_block_norm)}\n"
                    f"----------------------\n"
                )
                errors.append(err_msg)

        if file_modified:
            with open(filepath, 'w', encoding='utf-8') as target_file:
                target_file.write(modified_text)

    if errors:
        print("\n".join(errors))
    else:
        print("All patches applied successfully!")

if __name__ == "__main__":
    patch_content = ""

    # Check if there is data being piped into standard input
    if not sys.stdin.isatty():
        patch_content = sys.stdin.read()

    # Otherwise, check if a file argument was provided
    elif len(sys.argv) >= 2:
        patch_file = sys.argv[1]
        if not os.path.exists(patch_file):
            print(f"File not found: {patch_file}")
            sys.exit(1)
        with open(patch_file, 'r', encoding='utf-8') as f:
            patch_content = f.read()

    # If neither piped data nor file was provided, display usage
    else:
        print("Usage: python3 tools/simple-patch.py <patch_file.txt>")
        print("   or: Get-Clipboard | python3 tools/simple-patch.py")
        sys.exit(1)

    if not patch_content.strip():
        print("Error: No patch content provided.")
        sys.exit(1)

    apply_patches(patch_content)
