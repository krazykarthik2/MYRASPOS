# Utility Libraries Documentation

## Overview

MYRASPOS includes several utility libraries that provide essential functionality across the kernel. These libraries implement standard C library functions, pattern matching, image handling, and error handling.

**Files**:
- `kernel/lib.c`, `kernel/lib.h` - Standard library functions
- `kernel/glob.c`, `kernel/glob.h` - Pattern matching
- `kernel/image.c`, `kernel/image.h` - Image loading/handling
- `kernel/lodepng.c`, `kernel/lodepng.h` - PNG decoder (third-party)
- `kernel/lodepng_glue.c`, `kernel/lodepng_glue.h` - PNG integration
- `kernel/panic.c`, `kernel/panic.h` - Kernel panic handler
- `kernel/write.c` - Write utilities

## Standard Library (lib.c/h)

### Purpose
Provides essential C standard library functions not available in freestanding environment.

### Memory Functions

#### memset
```c
void *memset(void *s, int c, size_t n);
```

**Purpose**: Fill memory with constant byte

**Implementation**:
```c
void *memset(void *s, int c, size_t n) {
    unsigned char *p = s;
    while (n--) {
        *p++ = (unsigned char)c;
    }
    return s;
}
```

**Design**: Simple byte-by-byte fill
- **Trade-off**: Not optimized (could use word-size writes)
- **Simplicity**: Easy to understand and debug

#### memcpy
```c
void *memcpy(void *dest, const void *src, size_t n);
```

**Purpose**: Copy memory region

**Implementation**:
```c
void *memcpy(void *dest, const void *src, size_t n) {
    char *d = dest;
    const char *s = src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}
```

**Constraint**: Undefined for overlapping regions (use memmove)

#### memmove
```c
void *memmove(void *dest, const void *src, size_t n);
```

**Purpose**: Copy memory with overlap handling

**Implementation**:
```c
void *memmove(void *dest, const void *src, size_t n) {
    char *d = dest;
    const char *s = src;
    
    if (d < s) {
        // Copy forward
        while (n--) *d++ = *s++;
    } else {
        // Copy backward
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}
```

**Design**: Handles overlapping regions correctly

#### memcmp
```c
int memcmp(const void *a, const void *b, size_t n);
```

**Purpose**: Compare memory regions

**Returns**: 0 if equal, <0 if a < b, >0 if a > b

### String Functions

#### strlen
```c
size_t strlen(const char *s);
```

**Purpose**: Calculate string length

**Implementation**:
```c
size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}
```

#### strcmp
```c
int strcmp(const char *a, const char *b);
```

**Purpose**: Compare strings

**Returns**: 0 if equal, <0 if a < b, >0 if a > b

**Implementation**:
```c
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *(unsigned char*)a - *(unsigned char*)b;
}
```

#### strncmp
```c
int strncmp(const char *a, const char *b, size_t n);
```

**Purpose**: Compare up to n characters

#### strcpy
```c
char *strcpy(char *dest, const char *src);
```

**Purpose**: Copy string

**Constraint**: Destination must have enough space

#### strncpy
```c
char *strncpy(char *dest, const char *src, size_t n);
```

**Purpose**: Copy up to n characters

**Behavior**: Pads with zeros if src < n

#### strcat
```c
char *strcat(char *dest, const char *src);
```

**Purpose**: Concatenate strings

#### strchr
```c
char *strchr(const char *s, int c);
```

**Purpose**: Find first occurrence of character

**Returns**: Pointer to character or NULL

#### strrchr
```c
char *strrchr(const char *s, int c);
```

**Purpose**: Find last occurrence of character

#### strstr
```c
char *strstr(const char *haystack, const char *needle);
```

**Purpose**: Find substring

**Implementation**:
```c
char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char*)haystack;
    
    size_t needle_len = strlen(needle);
    
    while (*haystack) {
        if (strncmp(haystack, needle, needle_len) == 0) {
            return (char*)haystack;
        }
        haystack++;
    }
    
    return NULL;
}
```

**Complexity**: O(n*m) where n=haystack length, m=needle length

#### strtok
```c
char *strtok(char *s, const char *delim);
```

**Purpose**: Tokenize string

**Warning**: Modifies input string, not thread-safe

**Implementation**:
```c
static char *strtok_saved = NULL;

char *strtok(char *s, const char *delim) {
    if (s) strtok_saved = s;
    if (!strtok_saved) return NULL;
    
    // Skip leading delimiters
    while (*strtok_saved && strchr(delim, *strtok_saved)) {
        strtok_saved++;
    }
    
    if (!*strtok_saved) return NULL;
    
    char *token = strtok_saved;
    
    // Find next delimiter
    while (*strtok_saved && !strchr(delim, *strtok_saved)) {
        strtok_saved++;
    }
    
    if (*strtok_saved) {
        *strtok_saved = '\0';
        strtok_saved++;
    }
    
    return token;
}
```

### Conversion Functions

#### atoi
```c
int atoi(const char *s);
```

**Purpose**: Convert string to integer

**Implementation**:
```c
int atoi(const char *s) {
    int result = 0;
    int sign = 1;
    
    // Skip whitespace
    while (*s == ' ' || *s == '\t') s++;
    
    // Handle sign
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    
    // Convert digits
    while (*s >= '0' && *s <= '9') {
        result = result * 10 + (*s - '0');
        s++;
    }
    
    return sign * result;
}
```

**Limitation**: No error checking, no overflow detection

#### tolower
```c
int tolower(int c);
```

**Purpose**: Convert character to lowercase

**Implementation**:
```c
int tolower(int c) {
    if (c >= 'A' && c <= 'Z') {
        return c + ('a' - 'A');
    }
    return c;
}
```

### Advanced String Functions

#### strcasestr
```c
char *strcasestr(const char *haystack, const char *needle);
```

**Purpose**: Case-insensitive substring search

**Implementation**:
```c
char *strcasestr(const char *haystack, const char *needle) {
    size_t needle_len = strlen(needle);
    
    while (*haystack) {
        if (strncasecmp(haystack, needle, needle_len) == 0) {
            return (char*)haystack;
        }
        haystack++;
    }
    
    return NULL;
}
```

#### levenshtein_distance
```c
int levenshtein_distance(const char *s1, const char *s2);
```

**Purpose**: Calculate edit distance between strings

**Use Case**: Fuzzy matching, spell checking

**Implementation** (Dynamic Programming):
```c
int levenshtein_distance(const char *s1, const char *s2) {
    int len1 = strlen(s1);
    int len2 = strlen(s2);
    
    // Allocate matrix
    int matrix[len1 + 1][len2 + 1];
    
    // Initialize
    for (int i = 0; i <= len1; i++) matrix[i][0] = i;
    for (int j = 0; j <= len2; j++) matrix[0][j] = j;
    
    // Fill matrix
    for (int i = 1; i <= len1; i++) {
        for (int j = 1; j <= len2; j++) {
            int cost = (s1[i-1] == s2[j-1]) ? 0 : 1;
            
            int del = matrix[i-1][j] + 1;
            int ins = matrix[i][j-1] + 1;
            int sub = matrix[i-1][j-1] + cost;
            
            matrix[i][j] = min(min(del, ins), sub);
        }
    }
    
    return matrix[len1][len2];
}
```

**Complexity**: O(n*m) time, O(n*m) space

**Design Decision**: Variable-length arrays on stack
- **Advantage**: No heap allocation
- **Risk**: Stack overflow for very long strings
- **Constraint**: Keep strings reasonably short

#### levenshtein_distance_ci
```c
int levenshtein_distance_ci(const char *s1, const char *s2);
```

**Purpose**: Case-insensitive Levenshtein distance

## Pattern Matching (glob.c/h)

### Purpose
Implements glob-style pattern matching (wildcards: *, ?)

### glob_match
```c
int glob_match(const char *pattern, const char *string);
```

**Purpose**: Match string against glob pattern

**Patterns**:
- `*` - Matches any sequence of characters
- `?` - Matches any single character
- Other characters match literally

**Examples**:
```c
glob_match("*.txt", "readme.txt")    // Returns 1 (match)
glob_match("file?.c", "file1.c")    // Returns 1 (match)
glob_match("test*", "testing")      // Returns 1 (match)
glob_match("*.png", "image.jpg")    // Returns 0 (no match)
```

**Implementation** (Recursive):
```c
int glob_match(const char *pattern, const char *string) {
    if (!*pattern && !*string) return 1;  // Both empty = match
    
    if (*pattern == '*') {
        // Try matching rest of pattern with rest of string
        // or skip one character in string
        return glob_match(pattern + 1, string) ||
               (*string && glob_match(pattern, string + 1));
    }
    
    if (*pattern == '?' || *pattern == *string) {
        return glob_match(pattern + 1, string + 1);
    }
    
    return 0;  // No match
}
```

**Design Decision**: Recursive implementation
- **Advantage**: Simple and clear
- **Trade-off**: Can be slow for complex patterns
- **Constraint**: Stack depth limited

## Image Handling (image.c/h)

### Purpose
Load and decode PNG images for display

### img_load_png
```c
int img_load_png(const char *filename, int *width, int *height, 
                 uint32_t **data);
```

**Purpose**: Load PNG file from filesystem

**Returns**: 0 on success, -1 on error

**Implementation**:
```c
int img_load_png(const char *filename, int *width, int *height, 
                 uint32_t **data) {
    // Open file
    int fd = open(filename, O_RDONLY);
    if (fd < 0) return -1;
    
    // Get file size
    size_t file_size = get_file_size(fd);
    
    // Read entire file
    unsigned char *png_data = kmalloc(file_size);
    if (!png_data) {
        close(fd);
        return -1;
    }
    
    read(fd, png_data, file_size);
    close(fd);
    
    // Decode PNG
    unsigned char *image_data;
    unsigned w, h;
    unsigned error = lodepng_decode32(&image_data, &w, &h, 
                                      png_data, file_size);
    
    kfree(png_data);
    
    if (error) {
        return -1;
    }
    
    *width = w;
    *height = h;
    *data = (uint32_t*)image_data;
    
    return 0;
}
```

**Design Decisions**:
- **Full file read**: Load entire file into memory first
- **LodePNG**: Use third-party library for PNG decode
- **RGBA 32-bit**: Standard format for framebuffer

**Constraints**:
- **Memory intensive**: Full image uncompressed in memory
- **PNG only**: No JPEG, GIF, or other formats
- **No streaming**: Must fit entire file + decoded image in memory

## LodePNG Integration (lodepng.c/h, lodepng_glue.c/h)

### Purpose
Third-party PNG decoder library integrated into kernel

### Configuration
```c
#define LODEPNG_NO_COMPILE_ALLOCATORS  // Use kernel malloc/free
#define LODEPNG_NO_COMPILE_DISK         // No disk I/O in library
```

### Memory Integration (lodepng_glue.c)
```c
void* lodepng_malloc(size_t size) {
    return kmalloc(size);
}

void lodepng_free(void* ptr) {
    kfree(ptr);
}
```

**Design Decision**: Integrate with kernel memory management
- **Advantage**: No separate allocator needed
- **Tracking**: All allocations go through kmalloc

### lodepng_decode32
```c
unsigned lodepng_decode32(unsigned char** out, unsigned* w, unsigned* h,
                         const unsigned char* in, size_t insize);
```

**Purpose**: Decode PNG to 32-bit RGBA

**Returns**: Error code (0 = success)

**Output**: Allocated buffer with RGBA pixel data

## Panic Handler (panic.c/h)

### Purpose
Handle fatal kernel errors with diagnostic information

### panic
```c
void panic(const char *fmt, ...);
```

**Purpose**: Print error message and halt system

**Implementation**:
```c
void panic(const char *fmt, ...) {
    // Disable interrupts
    __asm__ volatile("msr daifset, #0xf");
    
    uart_puts("\n*** KERNEL PANIC ***\n");
    
    // Print formatted message
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    
    uart_puts("\n");
    
    // Print register state
    print_registers();
    
    // Print stack trace (if available)
    print_stack_trace();
    
    uart_puts("\n*** SYSTEM HALTED ***\n");
    
    // Infinite loop
    while (1) {
        __asm__ volatile("wfi");  // Wait for interrupt (never happens)
    }
}
```

**Design Decisions**:
- **Disable interrupts**: Prevent re-entry
- **UART output**: Reliable even if framebuffer broken
- **Halt system**: No recovery, prevent corruption
- **Diagnostic info**: Help debugging

**Example Usage**:
```c
if (!ptr) {
    panic("Failed to allocate memory for %s", name);
}

if (magic != 0xDEADC0DE) {
    panic("Task structure corrupted: magic=%08x", magic);
}
```

### print_registers
```c
void print_registers(void);
```

**Purpose**: Display CPU register state

**Implementation**:
```c
void print_registers(void) {
    uint64_t x0, x1, x2, x3, x30, sp, pc;
    
    __asm__ volatile(
        "mov %0, x0\n"
        "mov %1, x1\n"
        "mov %2, x2\n"
        "mov %3, x3\n"
        "mov %4, x30\n"
        "mov %5, sp\n"
        : "=r"(x0), "=r"(x1), "=r"(x2), "=r"(x3), "=r"(x30), "=r"(sp)
    );
    
    pc = (uint64_t)__builtin_return_address(0);
    
    uart_puts("Registers:\n");
    uart_printf("  x0  = %016llx\n", x0);
    uart_printf("  x1  = %016llx\n", x1);
    // ... more registers
    uart_printf("  x30 = %016llx (LR)\n", x30);
    uart_printf("  sp  = %016llx\n", sp);
    uart_printf("  pc  = %016llx\n", pc);
}
```

## Write Utilities (write.c)

### Purpose
Formatted output and string manipulation

### Implementation
Helper functions for `printf` and similar functionality.

**Note**: Actual implementation may vary; typically implements:
- Number to string conversion
- Formatted output helpers
- Buffer management

## Design Philosophy

### Simplicity
- **Clear implementations**: Easy to understand
- **No optimization**: Correct before fast
- **Minimal dependencies**: Self-contained

### Completeness
- **Common functions**: Most frequently used functions
- **No POSIX**: Not full POSIX compliance
- **Kernel-specific**: Tailored to kernel needs

### Safety
- **Bounds checking**: Where practical
- **Null checks**: Defensive programming
- **Panic on errors**: Fail fast for critical errors

## Constraints

### Global Constraints
1. **No dynamic allocation in some functions**: memset, memcpy use stack only
2. **No floating point**: Integer arithmetic only
3. **ASCII only**: No Unicode support
4. **Limited buffer sizes**: Fixed-size buffers where needed

### Performance Constraints
- **Unoptimized**: Byte-by-byte operations
- **No SIMD**: No vectorization
- **Simple algorithms**: O(n²) acceptable for small n

### Memory Constraints
- **Stack usage**: Some functions use VLA (variable-length arrays)
- **Heap usage**: Image loading allocates large buffers
- **No garbage collection**: Manual memory management

## Future Enhancements

### Planned
- Optimized memcpy/memset (word-size operations)
- More string functions (snprintf, etc.)
- UTF-8 support (basic)
- Better panic diagnostics (stack unwinding)

### Not Planned
- Full libc compatibility (out of scope)
- Floating point (kernel doesn't need it)
- Locale support (single locale only)
- Complex regex (use simple globbing)

## See Also
- [Memory Management](../memory/kmalloc.c.md)
- [Image Viewer App](../apps/README.md#4-image-viewer)
- [Window Manager](../wm/README.md)
- [Shell Commands](../commands/README.md)
