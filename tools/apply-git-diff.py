#!/usr/bin/env python3
import sys
import os
import re
import tempfile
import shutil
import subprocess

class Hunk:
    def __init__(self):
        self.search_lines = []
        self.replace_lines = []

class FilePatch:
    def __init__(self):
        self.old_file = None
        self.new_file = None
        self.hunks = []

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

def get_clipboard_text():
    """Attempt to get text from the clipboard using standard library."""
    try:
        import tkinter as tk
        root = tk.Tk()
        root.withdraw()
        text = root.clipboard_get()
        root.destroy()
        return text
    except Exception:
        return None

def is_patch(text):
    """Simple heuristic to detect if a text string contains a diff/patch."""
    if not text:
        return False
    has_minus = re.search(r'^--- ', text, re.MULTILINE)
    has_plus = re.search(r'^\+\+\+ ', text, re.MULTILINE)
    has_hunk = re.search(r'^@@ .* @@', text, re.MULTILINE)
    return bool(has_minus and has_plus and has_hunk)

def get_patch_input():
    """Resolve input based on Priority: 1. Clipboard, 2. Stdin, 3. File Arg"""
    clip_text = get_clipboard_text()
    if is_patch(clip_text):
        print("Detected valid patch in Clipboard. Applying...")
        return clip_text

    if not sys.stdin.isatty():
        stdin_text = sys.stdin.read()
        if is_patch(stdin_text):
            print("Detected valid patch from Standard Input. Applying...")
            return stdin_text

    if len(sys.argv) > 1:
        filepath = sys.argv[1]
        if os.path.isfile(filepath):
            with open(filepath, 'r', encoding='utf-8') as f:
                file_text = f.read()
            if is_patch(file_text):
                print(f"Detected valid patch in file '{filepath}'. Applying...")
                return file_text

    print("Error: No valid patch found in Clipboard, Stdin, or File Argument.")
    sys.exit(1)

def clean_filepath(path_line):
    """Extract path and strip 'a/' or 'b/' prefixes, handle /dev/null."""
    path = path_line[4:].strip()
    if path == '/dev/null':
        return None
    if path.startswith('a/') or path.startswith('b/'):
        return path[2:]
    return path

def parse_patch(patch_text):
    """Parse text into FilePatch objects."""
    patches = []
    current_patch = None
    current_hunk = None

    for line in patch_text.splitlines():
        if line.startswith('--- '):
            if current_patch and current_patch.old_file != current_patch.new_file:
                pass
            current_patch = FilePatch()
            patches.append(current_patch)
            current_patch.old_file = clean_filepath(line)
        elif line.startswith('+++ '):
            if current_patch:
                current_patch.new_file = clean_filepath(line)
        elif line.startswith('@@ '):
            current_hunk = Hunk()
            if current_patch:
                current_patch.hunks.append(current_hunk)
        elif current_hunk is not None:
            if line.startswith('+'):
                current_hunk.replace_lines.append(line[1:])
            elif line.startswith('-'):
                current_hunk.search_lines.append(line[1:])
            elif line.startswith(' '):
                current_hunk.search_lines.append(line[1:])
                current_hunk.replace_lines.append(line[1:])
            elif line == '':
                current_hunk.search_lines.append('')
                current_hunk.replace_lines.append('')

    return patches

def normalize_line(line):
    """Normalize line to ignore whitespaces for flexible matching."""
    return re.sub(r'\s+', ' ', line.strip())

def apply_hunk(file_lines, hunk):
    """Find search_lines in file_lines (ignoring whitespace) and replace."""
    if not hunk.search_lines:
        return hunk.replace_lines

    hunk_len = len(hunk.search_lines)
    for i in range(len(file_lines) - hunk_len + 1):
        match = True
        for j, s_line in enumerate(hunk.search_lines):
            if normalize_line(file_lines[i + j]) != normalize_line(s_line):
                match = False
                break

        if match:
            return file_lines[:i] + hunk.replace_lines + file_lines[i + hunk_len:]

    return None

def apply_patches(patches):
    """Apply parsed patches to the filesystem with backup and rollback mechanisms."""
    git_root = get_git_root()
    print(f"Working relative to git root: {git_root}")

    temp_dir = tempfile.mkdtemp(prefix="patch_backup_")
    print(f"Using temporary directory for backups: {temp_dir}\n")

    for patch in patches:
        is_creation = patch.old_file is None
        is_deletion = patch.new_file is None
        target_rel_path = patch.new_file if patch.new_file else patch.old_file

        # Build absolute path by joining Git Root with the patch's relative path
        target_file = os.path.abspath(os.path.join(git_root, target_rel_path))

        print(f"Processing '{target_rel_path}'")
        print(f"  -> Path: {target_file}")

        # --- BACKUP ---
        # Replace os.sep to create a flat filename for backup
        safe_backup_name = target_rel_path.replace('/', '_').replace('\\', '_') + ".bak"
        backup_path = os.path.join(temp_dir, safe_backup_name)
        file_exists = os.path.exists(target_file)

        if file_exists:
            shutil.copy2(target_file, backup_path)
        elif not is_creation:
            print(f"  [!] Error: File does not exist and patch is not marked as creation.")
            print(f"  -> Skipped.\n")
            continue

        # --- DELETE ---
        if is_deletion:
            os.remove(target_file)
            print(f"  -> Deleted successfully.\n")
            continue

        # --- READ ---
        file_lines = []
        if file_exists:
            with open(target_file, 'r', encoding='utf-8') as f:
                file_lines = f.read().splitlines()

        # --- APPLY HUNKS ---
        success = True
        for idx, hunk in enumerate(patch.hunks):
            new_lines = apply_hunk(file_lines, hunk)
            if new_lines is None:
                print(f"  [!] Error: Failed to apply hunk #{idx + 1}. Signature did not match.")
                success = False
                break
            file_lines = new_lines

        # --- WRITE OR ROLLBACK ---
        if success:
            os.makedirs(os.path.dirname(target_file), exist_ok=True)
            with open(target_file, 'w', encoding='utf-8') as f:
                f.write('\n'.join(file_lines) + '\n')
            print("  -> Applied successfully.\n")
        else:
            print("  [!] Rolling back changes for this file...")
            if is_creation and os.path.exists(target_file):
                os.remove(target_file)
            elif file_exists:
                shutil.copy2(backup_path, target_file)
            print("  -> Rollback complete.\n")

    shutil.rmtree(temp_dir, ignore_errors=True)

if __name__ == "__main__":
    patch_text = get_patch_input()
    patches = parse_patch(patch_text)

    if not patches:
        print("No valid file patches could be parsed from the input.")
        sys.exit(1)

    apply_patches(patches)
