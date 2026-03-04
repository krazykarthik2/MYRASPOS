import subprocess

def run_make_disk():
    try:
        result = subprocess.run(['python', 'make_disk.py'], capture_output=True, text=True)
        with open('make_disk_result.txt', 'w') as f:
            f.write("STDOUT:\n")
            f.write(result.stdout)
            f.write("\nSTDERR:\n")
            f.write(result.stderr)
    except Exception as e:
        with open('make_disk_result.txt', 'w') as f:
            f.write(f"Error running subprocess: {e}")

if __name__ == "__main__":
    run_make_disk()
