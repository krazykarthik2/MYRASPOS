# Program Registry Documentation

## Overview

The program registry (`programs.c/h`) provides a centralized command lookup system for MYRASPOS. It implements:
- Static registration of executable programs/commands
- Function pointer-based command dispatch
- Uniform command interface across all programs
- Integration with shell and service manager
- Support for command aliases (e.g., `vim` → `edit`)

The registry acts as the "executable database" for the system, enabling the shell to resolve command names to executable code without requiring a traditional filesystem-based executable loader.

## Data Structures

### `typedef int (*prog_fn_t)(int argc, char **argv, const char *in, size_t in_len, char *out, size_t out_cap)`
Function pointer type for all programs.

**Parameters:**
- `argc`: Argument count (including program name as argv[0])
- `argv`: Null-terminated array of argument strings
- `in`: Input data from pipeline or stdin (NULL if none)
- `in_len`: Length of input data in bytes
- `out`: Output buffer for program results
- `out_cap`: Capacity of output buffer in bytes

**Returns:** 
- Number of bytes written to `out` (>= 0)
- -1 on error (rare, most errors written to output)

**Contract:**
- Programs must not write more than `out_cap` bytes
- Return value should reflect actual bytes written
- Programs should handle NULL `in` gracefully
- Programs should null-terminate output when possible

### `struct prog_entry`
Internal registration structure.

```c
struct prog_entry {
    const char *name;   // Command name (e.g., "ls", "cat")
    prog_fn_t fn;       // Function pointer to implementation
};
```

**Purpose:** Maps command names to implementation functions.

**Storage:** Static array `prog_table[]` in `programs.c`.

## Key Functions

### Program Lookup

#### `int program_lookup(const char *name, prog_fn_t *out)`
Look up a program by name and retrieve its function pointer.

**Parameters:**
- `name`: Command name to find (e.g., "ls", "cat", "grep")
- `out`: Pointer to store the found function pointer

**Returns:**
- 0 on success (`*out` contains function pointer)
- -1 if program not found

**Algorithm:** Linear search through `prog_table[]`.

**Thread Safety:** Read-only access, safe for concurrent calls.

**Example:**
```c
prog_fn_t fn;
if (program_lookup("ls", &fn) == 0) {
    char output[1024];
    int ret = fn(2, (char*[]){"ls", "/home", NULL}, 
                 NULL, 0, output, sizeof(output));
    if (ret > 0) {
        output[ret] = '\0';
        uart_puts(output);
    }
}
```

### Program Enumeration

#### `const char **program_list(size_t *count)`
Get a list of all registered program names.

**Parameters:**
- `count`: Optional pointer to store the number of programs (can be NULL)

**Returns:** Pointer to null-terminated array of program name strings.

**Lifetime:** Returned array is static, valid until next call.

**Limitation:** Maximum 16 programs returned (arbitrary limit in implementation).

**Example:**
```c
size_t count;
const char **names = program_list(&count);
for (size_t i = 0; i < count; ++i) {
    uart_puts(names[i]);
    uart_puts("\n");
}
```

## Registered Programs

### Filesystem Commands
- **`ls`** - List directory contents (supports glob patterns)
- **`cat`** - Concatenate and display files
- **`touch`** - Create empty file
- **`write`** - Write text to file
- **`rm`** - Remove files
- **`mkdir`** - Create directory
- **`rmdir`** - Remove directory
- **`cp`** - Copy files
- **`mv`** - Move/rename files
- **`tree`** - Display directory tree

### Text Processing
- **`echo`** - Print arguments (supports `-e` for escapes, `-n` for no newline)
- **`grep`** - Search for patterns in text
- **`head`** - Display first lines of input
- **`tail`** - Display last lines of input
- **`more`** - Paginated text viewer

### Editing and Viewing
- **`edit`** - Text editor
- **`vim`** - Alias for `edit`
- **`view`** - View file (read-only)

### System Commands
- **`help`** - Display available commands
- **`clear`** - Clear screen
- **`ps`** - List running processes
- **`sleep`** - Sleep for specified milliseconds
- **`wait`** - Wait for process to complete
- **`kill`** - Terminate process by PID

### Service Management
- **`systemctl`** - Control system services
  - `systemctl start <name>`
  - `systemctl stop <name>`
  - `systemctl restart <name>`
  - `systemctl status <name>`
  - `systemctl enable <name>`
  - `systemctl disable <name>`

### Filesystem Export/Import
- **`ramfs-export`** - Export ramfs to disk image
- **`ramfs-import`** - Import disk image to ramfs

### Memory Management
- **`free`** - Display memory usage statistics

## Implementation Details

### Static Registration Table

Programs are registered at compile time in a static array:

```c
static struct prog_entry prog_table[] = {
    {"echo", prog_echo},
    {"help", prog_help},
    {"touch", prog_touch},
    // ... more entries ...
    {"free", prog_free},
    {NULL, NULL}    // Sentinel
};
```

**Termination:** Array ends with `{NULL, NULL}` sentinel.

**Ordering:** Arbitrary (lookup is linear, order doesn't matter).

### Lookup Algorithm

Linear search implementation:

```c
int program_lookup(const char *name, prog_fn_t *out) {
    for (int i = 0; prog_table[i].name; ++i) {
        if (strcmp(name, prog_table[i].name) == 0) {
            *out = prog_table[i].fn;
            return 0;
        }
    }
    return -1;
}
```

**Complexity:** O(n) where n is number of programs.

**Performance:** ~36 strcmp calls worst case (small overhead).

### Alias Support

Aliases are implemented by registering the same function under multiple names:

```c
{"edit", prog_edit},
{"vim", prog_edit},    // Alias: vim → edit
```

**Benefit:** Familiar command names for users from different backgrounds.

**Cost:** Minimal (one extra table entry per alias).

### Program Implementation Pattern

Programs follow a standard pattern:

```c
int prog_example(int argc, char **argv, 
                 const char *in, size_t in_len,
                 char *out, size_t out_cap) {
    // 1. Parse arguments
    if (argc < 2) {
        const char *msg = "usage: example <arg>\n";
        size_t len = strlen(msg);
        if (len > out_cap) len = out_cap;
        memcpy(out, msg, len);
        return len;
    }
    
    // 2. Process input if provided
    if (in && in_len > 0) {
        // Process pipeline input
    }
    
    // 3. Execute main logic
    // ...
    
    // 4. Write output
    size_t written = 0;
    // ... format output into 'out' buffer ...
    
    return written;
}
```

### Integration with Shell

The shell invokes programs via the registry:

```c
// In shell.c: exec_command_argv()
prog_fn_t pfn;
if (program_lookup(argv[0], &pfn) == 0) {
    return pfn(argc, argv, in, in_len, out, out_cap);
}
```

**Flow:**
1. Shell parses command line into argv
2. Shell looks up argv[0] in program registry
3. If found, shell calls function pointer with arguments
4. Function executes and writes to output buffer
5. Shell chains output to next pipeline stage or displays it

### Integration with Service Manager

Services execute programs the same way:

```c
// In service.c: service_task_fn()
prog_fn_t pfn;
if (program_lookup(argv[0], &pfn) == 0) {
    char out[256];
    int wrote = pfn(argc, argv, NULL, 0, out, sizeof(out));
    // Handle output (redirect to file or console)
}
```

## Design Decisions

### Why Static Registration?
**Decision:** Use compile-time static array instead of dynamic registration.

**Rationale:**
- Simpler implementation (no dynamic memory, no init function)
- All programs known at compile time
- No registration order dependencies
- Impossible to forget registration
- Slightly smaller binary (no registration code)

**Trade-off:** Can't add programs at runtime (acceptable for kernel use).

### Why Function Pointers Instead of Strings?
**Decision:** Store function pointers directly, not command paths.

**Rationale:**
- No need for executable loader or ELF parser
- Direct function call (no overhead)
- Type safety at compile time
- Programs can access kernel APIs directly
- Simplifies debugging (symbolic function names)

**Context:** MYRASPOS runs all code in kernel space (no userspace separation yet).

### Why Uniform Interface?
**Decision:** All programs use the same function signature.

**Rationale:**
- Simplifies shell implementation (one call site)
- Enables pipeline composition (output → input chaining)
- Consistent behavior across all programs
- Easy to add new programs (follow the pattern)

**Limitation:** All programs must fit the model (not suitable for interactive programs).

### Why Linear Search?
**Decision:** Use O(n) linear search instead of hash table.

**Rationale:**
- Number of programs: ~36 (small)
- Search cost: ~36 string comparisons (~5-10μs)
- Hash table overhead: More complex, more memory
- Simplicity: Easy to understand and debug

**When to Change:** If program count exceeds ~100, consider hash table.

### Why Limited program_list()?
**Decision:** Limit `program_list()` to 16 programs.

**Rationale:**
- Current implementation: static array for simplicity
- Sufficient for current use (mainly for `help` command)
- Easy to increase if needed

**Better Implementation:** Could return full count or use dynamic allocation.

### Why Include Aliases?
**Decision:** Register common aliases (vim → edit).

**Rationale:**
- User convenience (familiar command names)
- Minimal cost (one table entry)
- Reduces user confusion
- Matches common Unix aliases

## Usage Examples

### Basic Program Execution
```c
prog_fn_t fn;
if (program_lookup("echo", &fn) == 0) {
    char output[256];
    char *args[] = {"echo", "Hello, World!", NULL};
    int len = fn(2, args, NULL, 0, output, sizeof(output));
    output[len] = '\0';
    uart_puts(output);  // Prints: Hello, World!
}
```

### Pipeline Usage
```c
// Simulate: cat file.txt | grep "pattern"
char input[1024];
char output[1024];

// Step 1: cat file.txt
prog_fn_t cat_fn;
program_lookup("cat", &cat_fn);
char *cat_args[] = {"cat", "file.txt", NULL};
int in_len = cat_fn(2, cat_args, NULL, 0, input, sizeof(input));

// Step 2: grep "pattern" with input from cat
prog_fn_t grep_fn;
program_lookup("grep", &grep_fn);
char *grep_args[] = {"grep", "pattern", NULL};
int out_len = grep_fn(2, grep_args, input, in_len, output, sizeof(output));

output[out_len] = '\0';
uart_puts(output);
```

### Enumerating Programs
```c
void list_all_programs(void) {
    size_t count;
    const char **names = program_list(&count);
    
    uart_puts("Available programs:\n");
    for (size_t i = 0; i < count; ++i) {
        uart_puts("  ");
        uart_puts(names[i]);
        uart_puts("\n");
    }
}
```

### Dynamic Program Dispatch
```c
int execute_command(const char *cmd, char *output, size_t out_size) {
    // Parse command into argv
    char *argv[8];
    int argc = parse_command(cmd, argv, 8);
    
    // Look up program
    prog_fn_t fn;
    if (program_lookup(argv[0], &fn) != 0) {
        return -1;  // Not found
    }
    
    // Execute
    return fn(argc, argv, NULL, 0, output, out_size);
}
```

### Implementing a New Program

**Step 1:** Define function (e.g., in `kernel/progs/mycommand.c`):
```c
int prog_mycommand(int argc, char **argv,
                   const char *in, size_t in_len,
                   char *out, size_t out_cap) {
    if (argc < 2) {
        const char *usage = "usage: mycommand <arg>\n";
        size_t len = strlen(usage);
        if (len > out_cap) len = out_cap;
        memcpy(out, usage, len);
        return len;
    }
    
    // Do something with argv[1]...
    const char *result = "Success!\n";
    size_t len = strlen(result);
    if (len > out_cap) len = out_cap;
    memcpy(out, result, len);
    return len;
}
```

**Step 2:** Add forward declaration in `programs.h`:
```c
int prog_mycommand(int argc, char **argv, 
                   const char *in, size_t in_len,
                   char *out, size_t out_cap);
```

**Step 3:** Register in `programs.c`:
```c
static struct prog_entry prog_table[] = {
    // ... existing entries ...
    {"mycommand", prog_mycommand},
    {NULL, NULL}
};
```

**Step 4:** Rebuild and use:
```bash
$ mycommand test
Success!
```

## Cross-References

### Related Services
- **[shell.md](shell.md)** - Primary consumer of program registry
- **[service.md](service.md)** - Uses registry for service execution
- **[syscall.md](syscall.md)** - Programs may invoke syscalls

### Related Components
- **kernel/progs/** - Directory containing program implementations
  - `prog_filesystem.c` - ls, cat, touch, mkdir, etc.
  - `prog_textproc.c` - echo, grep, head, tail
  - `prog_system.c` - ps, sleep, kill, free
  - `prog_editor.c` - edit, view
  - `prog_systemctl.c` - systemctl implementation

### Header Files
- **programs.h** - Public API (program_lookup, program_list)
- **lib.h** - String utilities used by programs
- **ramfs.h** - Filesystem operations for file programs
- **sched.h** - Process operations for system programs

### Integration Points

**Shell Integration:**
```c
// shell.c: exec_command_argv()
prog_fn_t pfn;
if (program_lookup(argv[0], &pfn) == 0) {
    return pfn(argc, argv, in, in_len, out, out_cap);
}
```

**Service Integration:**
```c
// service.c: service_task_fn()
prog_fn_t pfn = NULL;
if (program_lookup(argv[0], &pfn) == 0 && pfn) {
    char out[256];
    pfn(argc, argv, NULL, 0, out, sizeof(out));
}
```

## Thread Safety

**Read-Only Data:** The program table is initialized at compile time and never modified.

**Thread-Safe Operations:**
- `program_lookup()` - Read-only access to static data
- `program_list()` - Read-only access (but static buffer reuse)

**Not Thread-Safe:**
- `program_list()` - Reuses static buffer (subsequent calls overwrite)

**Program Execution:** Individual programs may have thread-safety requirements depending on their implementation.

## Performance Considerations

### Lookup Performance
- **Time Complexity:** O(n) linear search
- **Typical Case:** ~18 comparisons average (half of 36)
- **Worst Case:** 36 comparisons (~5-10μs)
- **Comparison:** `strcmp()` on average 5-10 characters

### Memory Footprint
- **Per Entry:** 16 bytes (8-byte pointer + 8-byte pointer)
- **Total Table:** ~36 entries × 16 bytes = ~576 bytes
- **Plus:** String literals for names (~400 bytes)
- **Total:** ~1KB for entire registry

### Execution Overhead
- **Lookup:** ~5-10μs
- **Function Call:** ~50-100ns (direct function pointer)
- **Total Overhead:** Negligible compared to program execution

### Optimization Opportunities
- **Hash Table:** O(1) lookup for large program counts
- **Sorted Array:** Binary search O(log n)
- **Trie/Radix Tree:** Efficient for string keys
- **Bloom Filter:** Fast negative lookups

**Current Assessment:** No optimization needed for current scale.

## Error Handling

### Program Not Found
```c
prog_fn_t fn;
if (program_lookup("nosuchcommand", &fn) != 0) {
    uart_puts("Command not found\n");
}
```

### Invalid Arguments
Programs typically handle invalid arguments by writing usage to output:
```c
if (argc < 2) {
    const char *usage = "usage: command <arg>\n";
    // Write usage to output buffer
    return strlen(usage);
}
```

### Buffer Overflow Prevention
Programs must respect output buffer capacity:
```c
size_t len = strlen(result);
if (len > out_cap) len = out_cap;  // Truncate
memcpy(out, result, len);
return len;
```

### NULL Pointer Safety
Programs should handle NULL input gracefully:
```c
if (in == NULL || in_len == 0) {
    // No input, handle accordingly
}
```

## Extension Mechanisms

### Adding New Programs
1. Implement function with standard signature
2. Add forward declaration to `programs.h`
3. Register in `prog_table[]` in `programs.c`
4. Rebuild kernel

### Adding Aliases
Simply register the same function under multiple names:
```c
{"mycommand", prog_mycommand},
{"mc", prog_mycommand},  // Alias
```

### Program Categories
Programs can be logically grouped (no code changes needed):
- **Filesystem:** ls, cat, mkdir, rm, cp, mv
- **Text Processing:** echo, grep, head, tail, more
- **System:** ps, sleep, wait, kill, free
- **Editing:** edit, vim, view
- **Service Management:** systemctl
- **Export/Import:** ramfs-export, ramfs-import

### Future Enhancements
- **Dynamic Loading:** Load programs from files at runtime
- **Permissions:** Per-program permission checks
- **Resource Limits:** CPU time, memory usage limits per program
- **Help System:** Per-program help text registration
- **Categories:** Tag programs with categories for help
- **Versioning:** Track program version numbers
