import os
import shutil

moves = [
    ("kernel/calculator_app.h", "kernel/apps/calculator_app.h"),
    ("kernel/myra_app.h", "kernel/apps/myra_app.h"),
    ("kernel/files_app.h", "kernel/apps/files_app.h"),
    ("kernel/terminal_app.h", "kernel/apps/terminal_app.h"),
    ("kernel/keyboard_tester_app.h", "kernel/apps/keyboard_tester_app.h"),
    ("kernel/editor_app.h", "kernel/apps/editor_app.h"),
    ("kernel/v_edit.h", "kernel/apps/editor_app.h"), # Check strictly if this exists
    ("kernel/image_viewer.h", "kernel/apps/image_viewer.h"),
    ("kernel/image_viewer.c", "kernel/apps/image_viewer.c")
]

for src, dst in moves:
    if os.path.exists(src):
        try:
            # Check if rename (editor_app.h might be target of v_edit.h or existing file)
            if src == "kernel/v_edit.h" and os.path.exists("kernel/apps/editor_app.h"):
                 print(f"Skipping {src} -> {dst} (target exists)")
                 continue
            
            # Copy and delete to be safe
            with open(src, 'rb') as f_src:
                data = f_src.read()
            with open(dst, 'wb') as f_dst:
                f_dst.write(data)
            os.remove(src)
            print(f"Moved {src} to {dst}")
        except Exception as e:
            print(f"Error moving {src}: {e}")
    else:
        print(f"Skipping {src} (not found)")
