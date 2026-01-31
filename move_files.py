import shutil
import os

commands = [
    "echo.c", "help.c", "touch.c", "ls.c", "rm.c", "mkdir.c", "rmdir.c", "cp.c", "mv.c", "grep.c",
    "head.c", "tail.c", "more.c", "tree.c", "edit.c", "view.c", "clear.c", "ps.c", "free.c",
    "kill.c", "sleep.c", "wait.c", "systemctl.c", "ramfs_tools.c", "cat.c"
]

apps = [
    "calculator_app.c", "myra_app.c", "terminal_app.c", "files_app.c", "keyboard_tester_app.c"
]

# v_edit.c to editor_app.c
if os.path.exists("kernel/v_edit.c"):
    try:
        shutil.move("kernel/v_edit.c", "kernel/apps/editor_app.c")
        print("Moved v_edit.c to apps/editor_app.c")
    except Exception as e:
        print(f"Error moving v_edit.c: {e}")

for f in commands:
    src = os.path.join("kernel", f)
    dst = os.path.join("kernel", "commands", f)
    if os.path.exists(src):
        try:
            shutil.move(src, dst)
            print(f"Moved {f}")
        except Exception as e:
            print(f"Error moving {f}: {e}")
    else:
        print(f"Skipping {f} (not found)")

for f in apps:
    src = os.path.join("kernel", f)
    dst = os.path.join("kernel", "apps", f)
    if os.path.exists(src):
        try:
            shutil.move(src, dst)
            print(f"Moved {f}")
        except Exception as e:
            print(f"Error moving {f}: {e}")
    else:
        print(f"Skipping {f} (not found)")
