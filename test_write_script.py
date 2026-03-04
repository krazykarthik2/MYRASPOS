import os
print(f"Current PID: {os.getpid()}")
print(f"CWD: {os.getcwd()}")
with open(r"c:\Users\karthikkrazy\MYRASPOS\test_write_abs.txt", "w") as f:
    f.write("hello world absolute\n")
print("File written.")
