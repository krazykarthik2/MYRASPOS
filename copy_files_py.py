import os

commands = [
    "echo.c", "help.c", "touch.c", "ls.c", "rm.c", "mkdir.c", "rmdir.c", "cp.c", "mv.c", "grep.c",
    "head.c", "tail.c", "more.c", "tree.c", "edit.c", "view.c", "clear.c", "ps.c", "free.c",
    "kill.c", "sleep.c", "wait.c", "systemctl.c", "ramfs_tools.c", "cat.c"
]

apps = [
    "calculator_app.c", "myra_app.c", "terminal_app.c", "files_app.c", "keyboard_tester_app.c"
]

def copy_and_del(src, dst):
    if not os.path.exists(src):
        print(f"Skipping {src} (not found)")
        return
    try:
        with open(src, 'rb') as f:
            data = f.read()
        with open(dst, 'wb') as f:
            f.write(data)
        os.remove(src)
        print(f"Moved {src} to {dst}")
    except Exception as e:
        print(f"Error copying {src}: {e}")

# v_edit.c to editor_app.c
copy_and_del("kernel/v_edit.c", "kernel/apps/editor_app.c")

for f in commands:
    copy_and_del(os.path.join("kernel", f), os.path.join("kernel", "commands", f))

for f in apps:
    copy_and_del(os.path.join("kernel", f), os.path.join("kernel", "apps", f))
