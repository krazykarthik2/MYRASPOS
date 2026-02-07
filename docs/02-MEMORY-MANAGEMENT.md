# Memory Management

MYRASPOS uses a layered memory model combining physical page allocation, a kernel heap, and MMU configuration.

- Page allocator: [kernel/memory/palloc.c.md](kernel/memory/palloc.c.md)
- Kernel heap: [kernel/memory/kmalloc.c.md](kernel/memory/kmalloc.c.md)
- MMU setup and page tables: [kernel/memory/mmu.c.md](kernel/memory/mmu.c.md)

