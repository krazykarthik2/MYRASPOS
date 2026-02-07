# Shell Commands Documentation

## Overview

MYRASPOS includes 25 built-in shell commands that provide filesystem operations, process management, system inspection, and text processing capabilities. All commands are implemented as C functions and integrated into the shell.

**Location**: `kernel/commands/*.c`

## Command Categories

### File Operations
- `cat` - Display file contents
- `cp` - Copy files
- `mv` - Move/rename files
- `rm` - Remove files
- `touch` - Create empty files
- `view` - View files with paging

### Directory Operations
- `ls` - List directory contents
- `mkdir` - Create directories
- `rmdir` - Remove directories
- `tree` - Display directory tree

### Text Processing
- `echo` - Print text to output
- `grep` - Search for patterns in files
- `head` - Display first lines of file
- `tail` - Display last lines of file
- `more` - Page through file contents

### System Information
- `free` - Display memory usage
- `ps` - List running processes
- `systemctl` - Service management

### Process Control
- `kill` - Terminate processes
- `sleep` - Sleep for specified time
- `wait` - Wait for process completion

### Utility
- `clear` - Clear screen
- `edit` - Edit files
- `help` - Display help information
- `ramfs_tools` - RAM filesystem utilities

## Command List

### cat - Concatenate and Display Files
**File**: `kernel/commands/cat.c`

**Purpose**: Display contents of one or more files to stdout

**Usage**: 
```
cat <file1> [file2 ...]
```

**Implementation**:
```c
void cmd_cat(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            printf("cat: cannot open '%s'\n", argv[i]);
            continue;
        }
        
        char buf[512];
        int n;
        while ((n = read(fd, buf, sizeof(buf))) > 0) {
            write(1, buf, n);  // stdout
        }
        close(fd);
    }
}
```

**Design Decisions**:
- Uses 512-byte buffer for reading (matches disk sector size)
- Continues on error (doesn't stop at first failure)
- Writes directly to stdout without buffering

**Constraints**:
- No binary file detection (displays binary as-is)
- No line wrapping or formatting
- Memory limited by buffer size

---

### clear - Clear Terminal Screen
**File**: `kernel/commands/clear.c`

**Purpose**: Clear the terminal screen

**Usage**:
```
clear
```

**Implementation**:
```c
void cmd_clear(int argc, char **argv) {
    // Send ANSI escape sequence or call framebuffer clear
    printf("\033[2J\033[H");  // Clear screen, move cursor to home
}
```

**Design Decisions**:
- Uses ANSI escape sequences for compatibility
- Alternative: directly clear framebuffer if in GUI mode

---

### cp - Copy Files
**File**: `kernel/commands/cp.c`

**Purpose**: Copy files from source to destination

**Usage**:
```
cp <source> <destination>
```

**Implementation**:
```c
void cmd_cp(int argc, char **argv) {
    if (argc != 3) {
        printf("Usage: cp <source> <dest>\n");
        return;
    }
    
    int src_fd = open(argv[1], O_RDONLY);
    if (src_fd < 0) {
        printf("cp: cannot open '%s'\n", argv[1]);
        return;
    }
    
    int dst_fd = open(argv[2], O_WRONLY | O_CREAT);
    if (dst_fd < 0) {
        printf("cp: cannot create '%s'\n", argv[2]);
        close(src_fd);
        return;
    }
    
    char buf[4096];
    int n;
    while ((n = read(src_fd, buf, sizeof(buf))) > 0) {
        write(dst_fd, buf, n);
    }
    
    close(src_fd);
    close(dst_fd);
}
```

**Design Decisions**:
- Uses 4KB buffer for better performance
- Overwrites existing destination files
- No directory copying (files only)

**Constraints**:
- No progress indication for large files
- No preserve permissions/timestamps
- Single file only (no wildcards)

---

### echo - Print Text
**File**: `kernel/commands/echo.c`

**Purpose**: Print arguments to stdout

**Usage**:
```
echo <text...>
```

**Implementation**:
```c
void cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        printf("%s", argv[i]);
        if (i < argc - 1) printf(" ");
    }
    printf("\n");
}
```

**Design Decisions**:
- Spaces preserved between arguments
- Always adds newline at end
- No escape sequence processing

---

### edit - Text Editor
**File**: `kernel/commands/edit.c`

**Purpose**: Launch text editor for file

**Usage**:
```
edit <filename>
```

**Implementation**:
Launches the editor application window with specified file.

**Design Decisions**:
- Integrated with window manager
- Uses editor_app for actual editing
- Returns to shell after editor closes

---

### free - Memory Usage
**File**: `kernel/commands/free.c`

**Purpose**: Display memory allocation statistics

**Usage**:
```
free
```

**Implementation**:
```c
void cmd_free(int argc, char **argv) {
    size_t total, used, free_mem;
    palloc_stats(&total, &used, &free_mem);
    
    printf("Memory Statistics:\n");
    printf("  Total:     %zu KB\n", total / 1024);
    printf("  Used:      %zu KB\n", used / 1024);
    printf("  Free:      %zu KB\n", free_mem / 1024);
    printf("  Used %%:    %zu%%\n", (used * 100) / total);
}
```

**Design Decisions**:
- Queries palloc for page-level stats
- Displays in KB for readability
- Shows percentage utilization

---

### grep - Pattern Search
**File**: `kernel/commands/grep.c`

**Purpose**: Search for patterns in files

**Usage**:
```
grep <pattern> <file>
```

**Implementation**:
Simple string matching (not regex).

**Design Decisions**:
- Case-sensitive search only
- Line-by-line matching
- No regular expression support (simple substring match)

**Constraints**:
- No regex (too complex for kernel)
- Single file at a time
- No recursive directory search

---

### head - Display File Beginning
**File**: `kernel/commands/head.c`

**Purpose**: Display first N lines of file

**Usage**:
```
head [-n <count>] <file>
```

**Implementation**:
```c
void cmd_head(int argc, char **argv) {
    int lines = 10;  // default
    char *filename;
    
    // Parse arguments for -n option
    if (argc > 2 && strcmp(argv[1], "-n") == 0) {
        lines = atoi(argv[2]);
        filename = argv[3];
    } else {
        filename = argv[1];
    }
    
    // Read and display lines
    int fd = open(filename, O_RDONLY);
    // ... read line by line up to 'lines' count
}
```

**Design Decisions**:
- Default 10 lines (UNIX standard)
- Optional -n flag for custom count
- Stops at EOF if file shorter than requested

---

### help - Display Help
**File**: `kernel/commands/help.c`

**Purpose**: Display available commands

**Usage**:
```
help
```

**Implementation**:
Lists all registered commands with brief descriptions.

---

### kill - Terminate Process
**File**: `kernel/commands/kill.c`

**Purpose**: Send termination signal to process

**Usage**:
```
kill <pid>
```

**Implementation**:
```c
void cmd_kill(int argc, char **argv) {
    if (argc != 2) {
        printf("Usage: kill <pid>\n");
        return;
    }
    
    int pid = atoi(argv[1]);
    if (task_kill(pid) == 0) {
        printf("Killed task %d\n", pid);
    } else {
        printf("kill: no such task %d\n", pid);
    }
}
```

**Design Decisions**:
- Immediate termination (no graceful shutdown)
- No signal support (just kill)
- Cannot kill kernel tasks

**Constraints**:
- No signal numbers (always SIGKILL equivalent)
- Cannot kill task 0 (boot) or task 1 (init)

---

### ls - List Directory
**File**: `kernel/commands/ls.c`

**Purpose**: List directory contents

**Usage**:
```
ls [directory]
```

**Implementation**:
```c
void cmd_ls(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : ".";
    
    // Open directory
    DIR *dir = opendir(path);
    if (!dir) {
        printf("ls: cannot access '%s'\n", path);
        return;
    }
    
    // Read entries
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        printf("%s\n", entry->d_name);
    }
    
    closedir(dir);
}
```

**Design Decisions**:
- Simple listing (no -l long format)
- Current directory default
- Alphabetical order (if filesystem provides it)

**Constraints**:
- No color coding
- No file size/permissions display
- No hidden file filtering

---

### mkdir - Make Directory
**File**: `kernel/commands/mkdir.c`

**Purpose**: Create new directory

**Usage**:
```
mkdir <directory>
```

**Implementation**:
```c
void cmd_mkdir(int argc, char **argv) {
    if (argc != 2) {
        printf("Usage: mkdir <directory>\n");
        return;
    }
    
    if (mkdir(argv[1]) == 0) {
        printf("Created directory '%s'\n", argv[1]);
    } else {
        printf("mkdir: cannot create '%s'\n", argv[1]);
    }
}
```

**Design Decisions**:
- Single directory at a time
- No -p flag (no parent creation)
- Fails if directory exists

---

### more - Page File Contents
**File**: `kernel/commands/more.c`

**Purpose**: Display file contents with paging

**Usage**:
```
more <file>
```

**Implementation**:
Similar to `cat` but pauses after each screenful.

**Design Decisions**:
- Interactive paging (wait for key press)
- Shows "-- More --" prompt
- Space = next page, Q = quit

---

### mv - Move/Rename Files
**File**: `kernel/commands/mv.c`

**Purpose**: Move or rename files

**Usage**:
```
mv <source> <destination>
```

**Implementation**:
```c
void cmd_mv(int argc, char **argv) {
    if (argc != 3) {
        printf("Usage: mv <source> <dest>\n");
        return;
    }
    
    if (rename(argv[1], argv[2]) == 0) {
        printf("Moved '%s' to '%s'\n", argv[1], argv[2]);
    } else {
        printf("mv: cannot move '%s'\n", argv[1]);
    }
}
```

**Design Decisions**:
- Uses rename() system call
- Works for both move and rename
- Atomic operation if on same filesystem

**Constraints**:
- May fail across different filesystems (ramfs vs diskfs)
- No directory merging
- Overwrites destination if exists

---

### ps - Process Status
**File**: `kernel/commands/ps.c`

**Purpose**: List all running tasks

**Usage**:
```
ps
```

**Implementation**:
```c
void cmd_ps(int argc, char **argv) {
    printf("PID  NAME             STATE  RUNS   UPTIME\n");
    
    int ids[64];
    int run_counts[64];
    int start_ticks[64];
    int runnable[64];
    char names[64 * 16];
    
    int count = task_stats(ids, run_counts, start_ticks, 
                          runnable, names, 64, NULL);
    
    for (int i = 0; i < count; i++) {
        printf("%-4d %-16s %-6s %-6d %d ms\n",
               ids[i],
               &names[i * 16],
               runnable[i] ? "RUN" : "BLOCK",
               run_counts[i],
               scheduler_get_tick() - start_ticks[i]);
    }
}
```

**Design Decisions**:
- Shows PID, name, state, run count, uptime
- Simple columnar format
- Updates in real-time (not snapshot)

---

### ramfs_tools - RAM Filesystem Utilities
**File**: `kernel/commands/ramfs_tools.c`

**Purpose**: Debugging and management tools for ramfs

**Usage**:
```
ramfs_stats
ramfs_dump
```

**Implementation**:
Provides low-level ramfs introspection.

---

### rm - Remove Files
**File**: `kernel/commands/rm.c`

**Purpose**: Delete files

**Usage**:
```
rm <file>
```

**Implementation**:
```c
void cmd_rm(int argc, char **argv) {
    if (argc != 2) {
        printf("Usage: rm <file>\n");
        return;
    }
    
    if (unlink(argv[1]) == 0) {
        printf("Removed '%s'\n", argv[1]);
    } else {
        printf("rm: cannot remove '%s'\n", argv[1]);
    }
}
```

**Design Decisions**:
- Immediate deletion (no trash/recycle bin)
- No confirmation prompt
- Files only (not directories)

**Constraints**:
- No -r flag (use rmdir for directories)
- No -f flag (always fails on error)
- Cannot delete open files

---

### rmdir - Remove Directory
**File**: `kernel/commands/rmdir.c`

**Purpose**: Delete empty directories

**Usage**:
```
rmdir <directory>
```

**Implementation**:
```c
void cmd_rmdir(int argc, char **argv) {
    if (argc != 2) {
        printf("Usage: rmdir <directory>\n");
        return;
    }
    
    if (rmdir(argv[1]) == 0) {
        printf("Removed directory '%s'\n", argv[1]);
    } else {
        printf("rmdir: cannot remove '%s'\n", argv[1]);
    }
}
```

**Design Decisions**:
- Only removes empty directories
- Fails if directory contains files
- No recursive removal

**Safety**: Prevents accidental data loss

---

### sleep - Delay Execution
**File**: `kernel/commands/sleep.c`

**Purpose**: Sleep for specified seconds

**Usage**:
```
sleep <seconds>
```

**Implementation**:
```c
void cmd_sleep(int argc, char **argv) {
    if (argc != 2) {
        printf("Usage: sleep <seconds>\n");
        return;
    }
    
    int seconds = atoi(argv[1]);
    timer_sleep_ms(seconds * 1000);
}
```

**Design Decisions**:
- Blocks current task
- Resolution in seconds (no fractional)
- Uses timer-based blocking

---

### systemctl - Service Control
**File**: `kernel/commands/systemctl.c`

**Purpose**: Manage system services

**Usage**:
```
systemctl status
systemctl start <service>
systemctl stop <service>
```

**Implementation**:
Interface to service manager.

---

### tail - Display File End
**File**: `kernel/commands/tail.c`

**Purpose**: Display last N lines of file

**Usage**:
```
tail [-n <count>] <file>
```

**Implementation**:
Similar to head but from end of file.

**Design Decisions**:
- Default 10 lines
- Must read entire file to find end
- Buffers lines in memory

---

### touch - Create Empty File
**File**: `kernel/commands/touch.c`

**Purpose**: Create empty file or update timestamp

**Usage**:
```
touch <file>
```

**Implementation**:
```c
void cmd_touch(int argc, char **argv) {
    if (argc != 2) {
        printf("Usage: touch <file>\n");
        return;
    }
    
    int fd = open(argv[1], O_WRONLY | O_CREAT);
    if (fd >= 0) {
        close(fd);
        printf("Touched '%s'\n", argv[1]);
    } else {
        printf("touch: cannot create '%s'\n", argv[1]);
    }
}
```

**Design Decisions**:
- Creates empty file if not exists
- No timestamp update (not implemented)
- Simple open/close pattern

---

### tree - Directory Tree
**File**: `kernel/commands/tree.c`

**Purpose**: Display directory hierarchy

**Usage**:
```
tree [directory]
```

**Implementation**:
Recursive directory traversal with indentation.

**Design Decisions**:
- ASCII art tree structure
- Indentation shows depth
- Recursive implementation

---

### view - File Viewer
**File**: `kernel/commands/view.c`

**Purpose**: View file with GUI viewer

**Usage**:
```
view <file>
```

**Implementation**:
Launches appropriate viewer based on file type (image viewer for images, text viewer for text).

---

### wait - Wait for Event
**File**: `kernel/commands/wait.c`

**Purpose**: Wait for process or event

**Usage**:
```
wait [pid]
```

**Implementation**:
```c
void cmd_wait(int argc, char **argv) {
    if (argc == 1) {
        // Wait for any child
        task_wait_event(WM_EVENT_ID);
    } else {
        // Wait for specific PID
        int pid = atoi(argv[1]);
        while (task_exists(pid)) {
            yield();
        }
    }
}
```

**Design Decisions**:
- Can wait for specific PID or any event
- Uses yield() to avoid busy waiting
- Blocks until condition met

## Command Registration

Commands are registered in `shell.c` using a dispatch table:

```c
struct shell_command {
    const char *name;
    void (*handler)(int argc, char **argv);
    const char *help;
};

static struct shell_command commands[] = {
    {"cat", cmd_cat, "Display file contents"},
    {"ls", cmd_ls, "List directory"},
    // ... all other commands ...
    {NULL, NULL, NULL}  // Terminator
};
```

## Design Philosophy

### Simplicity
- Each command is self-contained
- Minimal dependencies
- Clear, readable implementation

### UNIX-like
- Familiar command names and behavior
- Standard arguments and flags
- Text-based I/O

### Kernel Integration
- Direct access to kernel APIs
- No shell script interpreter needed
- Efficient execution (no fork/exec overhead)

## Constraints

### Global Constraints
1. **No pipes**: Cannot chain commands with `|`
2. **No redirection**: No `>` or `<` operators
3. **No wildcards**: No `*` or `?` expansion
4. **No job control**: No `&` background jobs
5. **No environment variables**: No `$VAR` expansion
6. **Single file operations**: Most commands handle one file at a time

### Memory Constraints
- Fixed buffer sizes (typically 512-4096 bytes)
- No dynamic buffer growth
- Limited by kernel heap

### Filesystem Constraints
- Path length limited to 256 characters
- Filename length limited to 64 characters
- Case-sensitive names

## Future Enhancements

### Planned
- Pipe support (command chaining)
- I/O redirection
- Wildcard expansion
- Command history
- Tab completion

### Not Planned
- Shell scripting language (out of scope)
- Complex regex (too large for kernel)
- Job control (requires signal support)

## See Also
- [Shell Implementation](../services/shell.md)
- [Filesystem Documentation](../fs/README.md)
- [Process Management](../03-TASK-SCHEDULING.md)
