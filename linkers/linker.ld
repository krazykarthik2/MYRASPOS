ENTRY(_start)

PHDRS
{
  text PT_LOAD FLAGS(5);  /* R + X */
  data PT_LOAD FLAGS(6);  /* R + W */
}

SECTIONS
{
  . = 0x40000000;

  .text : ALIGN(4) {
    KEEP(*(.text.boot))
    *(.text .text.*)
  } :text

  .rodata : ALIGN(4) {
    *(.rodata .rodata.*)
  } :text

  .data : ALIGN(4) {
    *(.data .data.*)
  } :data

  .bss : ALIGN(4) {
    __bss_start = .;
    *(.bss .bss.*)
    *(COMMON)
    __bss_end = .;
  } :data

  . = ALIGN(8);
  . = . + 0x4000;
  stack_top = .;
}
