# Shell Service Documentation

## Overview

The shell service (`shell.c/h`) provides an interactive command-line interface for MYRASPOS. It implements a Unix-like shell with support for:
- Command parsing and execution
- Pipeline processing (command chaining with `|`)
- Output redirection (`>`, `>>`)
- Background job execution (`&`)
- Built-in commands (cd, pwd, exit)
- Glob pattern matching for file operations
- Path resolution and normalization
- Integration with the program registry and PTY subsystem

The shell serves as the primary user interface for system interaction and supports both console and PTY-based operation for multi-session support.

## Data Structures

### `struct pipeline_job`
Represents a parsed command pipeline ready for execution.

```c
struct pipeline_job {
    int ncmds;                      // Number of commands in pipeline
    char **argvs[MAX_CMDS];        // Array of argv arrays (one per command)
    int argcs[MAX_CMDS];           // Array of argc values
    char *out_file;                 // Output redirection target (NULL if none)
    int append;                     // 1 for >>, 0 for >
    int background;                 // 1 if & at end
    char **tokens;                  // Tokenized input (for memory management)
    int token_count;                // Number of tokens
    struct pty *pty;                // Associated PTY (NULL for console)
};
```

**Fields:**
- `ncmds`: Number of commands in the pipeline (max 8)
- `argvs`: Array of argument vectors, one for each command
- `argcs`: Argument counts for each command
- `out_file`: Resolved absolute path for output redirection
- `append`: Boolean flag for append mode redirection
- `background`: Boolean flag indicating background execution
- `tokens`: All parsed tokens (owned by job for cleanup)
- `token_count`: Total number of tokens allocated
- `pty`: Pseudo-terminal for output (NULL uses console)

### `struct cmd_entry`
Built-in command registration structure.

```c
struct cmd_entry {
    const char *name;    // Command name
    cmd_fn_t fn;        // Handler function
};
```

### Command Function Signature
```c
typedef int (*cmd_fn_t)(int argc, char **argv, 
                        const char *in, size_t in_len,
                        char *out, size_t out_cap);
```

**Parameters:**
- `argc`: Argument count
- `argv`: Argument vector
- `in`: Input buffer from previous pipeline stage (NULL if first)
- `in_len`: Length of input data
- `out`: Output buffer for command result
- `out_cap`: Capacity of output buffer

**Returns:** Number of bytes written to output buffer, or -1 on error.

## Key Functions

### Core Shell Functions

#### `void shell_main(void *arg)`
Main shell loop implementation.

**Parameters:**
- `arg`: Optional PTY structure pointer for terminal session

**Behavior:**
1. Displays shell banner
2. Enters command loop:
   - Prints prompt showing current directory (`myras::/path$ `)
   - Reads user input (with line editing support)
   - Parses command line into pipeline
   - Executes foreground or creates background task
   - Handles Ctrl+C interrupt signal
3. Exits when `shell_should_exit` flag is set

**Thread Safety:** Not thread-safe. Single shell instance per task.

#### `int shell_exec(const char *cmdline, char *out, size_t out_cap)`
Execute a command line and capture output (programmatic interface).

**Parameters:**
- `cmdline`: Command line string to execute
- `out`: Buffer to receive output
- `out_cap`: Capacity of output buffer

**Returns:** Number of bytes written to output, or -1 on error.

**Use Case:** Used by other kernel components to execute shell commands programmatically without user interaction.

### Pipeline Processing

#### `static struct pipeline_job *parse_pipeline(const char *line_in)`
Parse a command line into a pipeline job structure.

**Parsing Features:**
- Tokenization with quote support (`"..."`, `'...'`)
- Escape sequence handling (`\`)
- Pipeline operator (`|`)
- Redirection operators (`>`, `>>`)
- Background operator (`&`)

**Returns:** Allocated `pipeline_job` structure or NULL on error.

**Memory:** Caller must free returned structure and all contained allocations.

#### `static void run_pipeline_internal(struct pipeline_job *job)`
Execute a pipeline job synchronously.

**Execution Flow:**
1. For each command in pipeline:
   - Allocate output buffer (2048 bytes)
   - Execute command with input from previous stage
   - Check for interrupt signal (`shell_sigint`)
   - Pass output to next stage
2. Handle final output:
   - Write to file if redirection specified
   - Write to PTY if available
   - Write to console otherwise

**Interrupt Handling:** Checks `shell_sigint` between commands for Ctrl+C support.

#### `static void pipeline_runner(void *arg)`
Task wrapper for pipeline execution (background jobs).

**Behavior:**
1. Executes pipeline
2. Frees all job memory
3. Disables task for future scheduling via `task_set_fn_null()`

### Command Execution

#### `static int exec_command_argv(char **argv, int argc, const char *in, size_t in_len, char *out, size_t out_cap)`
Execute a single command by name lookup.

**Resolution Order:**
1. Built-in commands (cd, pwd, exit)
2. Registered programs (from program registry)
3. Error: "unknown command"

**Returns:** Number of bytes written to output buffer.

### Path Resolution

#### `char *init_resolve_path(const char *p)`
Resolve a relative or absolute path to an absolute path.

**Exported API:** Available to other kernel components via `init.h`.

**Behavior:**
- Absolute paths (starting with `/`): Normalized
- Relative paths: Resolved against `shell_cwd`
- Normalization: Handles `.`, `..`, and multiple slashes

**Returns:** Kmalloc'd string (caller must free), or NULL on error.

#### `static char *normalize_abs_path_alloc(const char *path)`
Normalize an absolute path by resolving `.` and `..` components.

**Algorithm:**
1. Split path into segments
2. Skip empty segments (consecutive slashes)
3. Skip `.` segments (current directory)
4. For `..` segments: Remove last segment from output
5. Reconstruct normalized path

**Returns:** Kmalloc'd normalized path.

### Built-in Commands

#### `static int cmd_cd(int argc, char **argv, ...)`
Change current working directory.

**Features:**
- No args or `.` → stay in current directory
- `..` → parent directory
- Glob patterns → change to matched directory (must match exactly one)
- Path verification → checks directory exists via ramfs

**Updates:** Global `shell_cwd` variable.

#### `static int cmd_pwd(int argc, char **argv, ...)`
Print current working directory.

**Output:** Current value of `shell_cwd` followed by newline.

#### `static int cmd_exit(int argc, char **argv, ...)`
Exit the shell.

**Behavior:** Sets `shell_should_exit` flag to terminate shell loop.

### Input Handling

#### `static int shell_read_line(char *buf, size_t max)`
Read a line from console with basic line editing.

**Editing Features:**
- Character echo
- Backspace/Delete support (ASCII 8 and 127)
- Enter key handling (`\r`, `\n`)

**Returns:** Number of characters read (excluding newline).

**Blocking:** Uses `yield()` when no input available.

## Implementation Details

### Command Parsing

The parser implements a state machine that:
1. **Tokenizes** input respecting quotes and escapes
2. **Groups** tokens into commands separated by `|`
3. **Detects** redirection operators and background marker
4. **Resolves** redirection targets to absolute paths

Quote handling preserves backslashes for escape interpretation by programs like `echo -e`.

### Pipeline Execution Model

Pipelines execute **synchronously** within the shell task:
- Each command's output becomes the next command's input
- Buffers are chained: `cmd1 → buf1 → cmd2 → buf2 → ...`
- Only the last buffer is written to final destination
- Memory is managed carefully to avoid leaks and double-frees

### Buffer Management

The shell uses a careful buffer management strategy:
- Each pipeline stage allocates a 2KB buffer
- Previous stage buffer is freed after consumption
- Only the last stage buffer persists until final output
- Interrupt handling ensures cleanup even on abort

### Current Working Directory

The global `shell_cwd` variable (256 bytes) tracks the current directory:
- Always stored as absolute path
- Root directory: `"/"`
- Other directories: No trailing slash (e.g., `"/home/user"`)
- Thread-unsafe: Shell expects single-threaded execution

### PTY Integration

When a PTY is attached:
- Input is read via `pty_getline()` (blocking, canonical mode)
- Output is written character-by-character via `pty_write_out()`
- Console I/O functions are bypassed entirely

### Background Job Execution

Background jobs (`command &`):
1. Create new task with `task_create()`
2. Task runs `background_wrapper()` → `pipeline_runner()`
3. After execution, task disables itself via `task_set_fn_null()`
4. Shell prints "started pid N" and continues

### Interrupt Handling

The global `volatile int shell_sigint` flag:
- Set by external interrupt handler (typically Ctrl+C)
- Checked between pipeline stages
- Stops execution but completes cleanup
- Reset at start of each command execution

## Design Decisions

### Why Single Global CWD?
**Decision:** Use a single global `shell_cwd` variable.

**Rationale:** 
- MYRASPOS shell is designed for single-user, single-session use in the base configuration
- Simplifies path resolution without per-task state
- PTY-based shells can maintain separate state if needed in future

**Trade-off:** Multiple concurrent shells share CWD (acceptable for intended use case).

### Why Synchronous Pipeline Execution?
**Decision:** Execute pipelines synchronously within the shell task.

**Rationale:**
- Simpler implementation without inter-task communication
- Predictable memory usage and cleanup
- Easier debugging and error handling
- Sufficient for interactive command execution

**Alternative:** Async execution with IPC would enable true parallelism but adds complexity.

### Why In-Memory Token Storage?
**Decision:** Store all tokens in job structure for batch cleanup.

**Rationale:**
- Prevents memory leaks in error paths
- Simplifies error handling (single free point)
- Enables efficient cleanup after execution

**Trade-off:** Higher memory usage during parsing (acceptable for typical command lines).

### Why 2KB Output Buffers?
**Decision:** Use 2048-byte buffers for each pipeline stage.

**Rationale:**
- Sufficient for most command output
- Prevents unbounded memory usage
- Commands must be designed to work within buffer limits

**Limitation:** Very large outputs may be truncated.

### Why Preserve Backslashes in Quotes?
**Decision:** Keep backslashes in quoted strings during tokenization.

**Rationale:**
- Allows programs to interpret escape sequences (e.g., `echo -e "\n"`)
- Provides flexibility for different interpretation strategies
- Maintains Unix-like behavior

## Usage Examples

### Basic Command Execution
```c
// From user input
myras::/$ ls /home
file1.txt
file2.txt
```

### Pipeline Example
```c
// Chain commands
myras::/$ cat file.txt | grep "pattern" | head -n 5

// Implementation creates:
// pipeline_job with ncmds=3
// cmd1: cat file.txt → buf1
// cmd2: grep "pattern" (in=buf1) → buf2  
// cmd3: head -n 5 (in=buf2) → output
```

### Output Redirection
```c
// Overwrite file
myras::/$ echo "hello world" > /tmp/output.txt

// Append to file
myras::/$ echo "more text" >> /tmp/output.txt
```

### Background Jobs
```c
myras::/$ sleep 1000 &
started pid 5
myras::/$ ps
PID  NAME
1    init
2    shell  
5    background
```

### Path Resolution
```c
// Relative path resolution
myras::/home$ cd user
myras::/home/user$

// Parent directory
myras::/home/user$ cd ..
myras::/home$

// Absolute path
myras::/home$ cd /etc/config
myras::/etc/config$
```

### Glob Matching in cd
```c
myras::/home$ cd us*
myras::/home/user$    // if exactly one match

myras::/home$ cd [a-z]*
cd: too many matches   // if multiple matches
```

### Programmatic Execution
```c
// From kernel code
char output[1024];
int len = shell_exec("ls /home", output, sizeof(output));
if (len > 0) {
    output[len] = '\0';
    uart_puts(output);
}
```

## Cross-References

### Related Services
- **[programs.md](programs.md)** - Program registry for command lookup
- **[pty.md](pty.md)** - Pseudo-terminal for multi-session support
- **[service.md](service.md)** - Service manager for background daemons
- **[syscall.md](syscall.md)** - System call interface for file operations

### Related Kernel Components
- **sched.h/c** - Task scheduler for background job execution
- **ramfs.h/c** - Filesystem for directory operations
- **init.h/c** - Initialization and global utilities
- **glob.h/c** - Pattern matching for file globbing
- **kmalloc.h/c** - Dynamic memory allocation

### Related Headers
- **shell.h** - Public API: `shell_exec()`, exported to init
- **lib.h** - String utilities: `strcmp()`, `strlen()`, `memcpy()`
- **uart.h** - Console I/O for direct output

### Key Constants
```c
#define MAX_ARGS 8       // Maximum arguments per command
#define MAX_CMDS 8       // Maximum commands in pipeline
#define BUF_SIZE 2048    // Buffer size for command output
#define LINE_BUF_SIZE 2048  // Input line buffer size
```

### Global Variables
```c
static char shell_cwd[256];         // Current working directory
volatile int shell_sigint;          // Interrupt flag (Ctrl+C)
static int shell_should_exit;       // Exit flag
static struct cmd_entry commands[]; // Built-in command table
```

## Thread Safety

**Not Thread-Safe:** The shell is designed for single-threaded execution:
- Global `shell_cwd` is shared
- No locking on command execution
- Each shell instance should run in its own task
- Multiple shells (via PTY) can coexist but should not share state

## Error Handling

The shell implements graceful error handling:
- Parse errors: Report "error parsing\n" and continue
- Unknown commands: Report "unknown command\n" in output buffer
- Path resolution failures: Return NULL, commands handle gracefully
- Memory allocation failures: Abort operation, cleanup allocated resources
- Interrupt signals: Stop execution but complete cleanup

## Performance Considerations

### Memory Usage
- Per-command overhead: ~2KB buffer per pipeline stage
- Token storage: ~64 tokens × average token size
- Job structure: ~200 bytes + pointers
- **Total per command:** ~2-3KB for simple commands, more for long pipelines

### Execution Speed
- Built-in commands: Near-instant (hash table lookup)
- Program commands: Fast (function pointer call)
- Pipeline overhead: Minimal (sequential buffer copying)
- I/O bound: Most time spent in actual command execution

### Optimization Opportunities
- Token reuse: Could avoid string copying for simple cases
- Buffer pooling: Reuse buffers across commands
- Lazy normalization: Only normalize paths when necessary
- Command caching: Cache resolved paths for repeated operations
