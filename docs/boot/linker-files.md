# Linker Scripts

MYRASPOS uses two linker scripts that describe the memory layout:

- QEMU virt: [linker.ld.md](linker.ld.md)
- Raspberry Pi: [linker_pi.ld.md](linker_pi.ld.md)

Both scripts live in `docs/boot/` and are referenced by the build system; see [build/BUILD-SYSTEM.md](../build/BUILD-SYSTEM.md) for usage details.

