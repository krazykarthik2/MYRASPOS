ENTRY(_start)

PHDRS
{
  text PT_LOAD FLAGS(5);  /* R + X */
  data PT_LOAD FLAGS(6);  /* R + W */
}

SECTIONS
{
  /* Move kernel base further up to avoid collision with framebuffer at 0x40000000.
     A 1024x768x4 framebuffer takes ~4MB. 0x40800000 is 8MB offset. */
  . = 0x40800000;

  .text : ALIGN(4) {
    KEEP(*(.text.boot))
    *(.text .text.*)
  } :text

  .rodata : ALIGN(4) {
    *(.rodata .rodata.*)
  } :text

  .data : ALIGN(4096) {
    *(.data .data.*)
  } :data

  .bss : ALIGN(4096) {
    __bss_start = .;
    *(.bss .bss.*)
    *(COMMON)
    __bss_end = .;
  } :data

  . = ALIGN(8);
  . = . + 0x4000;
  stack_top = .;
}
