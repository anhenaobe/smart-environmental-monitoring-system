/* GNU ld script, STM32L432KC. Keep the stack watermark in an explicitly
 * reserved 4 KiB region at the top of contiguous SRAM. */
ENTRY(Reset_Handler)
_Min_Heap_Size = 0x200;
_Min_Stack_Size = 0x1000;
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 256K
    RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 64K
}
_estack = ORIGIN(RAM) + LENGTH(RAM);
_stack_bottom = _estack - _Min_Stack_Size;
SECTIONS
{
    .isr_vector : { . = ALIGN(4); KEEP(*(.isr_vector)) . = ALIGN(4); } > FLASH
    .text : {
        . = ALIGN(4);
        *(.text*) *(.glue_7*) *(.glue_7t*) *(.eh_frame*)
        KEEP(*(.init)) KEEP(*(.fini))
        . = ALIGN(4); _etext = .;
    } > FLASH
    .rodata : { . = ALIGN(4); *(.rodata*) . = ALIGN(4); } > FLASH
    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
    .ARM.exidx : {
        __exidx_start = .; *(.ARM.exidx*); __exidx_end = .;
    } > FLASH
    .preinit_array : {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > FLASH
    .init_array : {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > FLASH
    .fini_array : {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > FLASH
    _sidata = LOADADDR(.data);
    .data : {
        . = ALIGN(4); _sdata = .;
        *(.data*) *(.RamFunc*)
        . = ALIGN(4); _edata = .;
    } > RAM AT> FLASH
    .bss (NOLOAD) : {
        . = ALIGN(4); _sbss = .; __bss_start__ = .;
        *(.bss*) *(COMMON)
        . = ALIGN(4); _ebss = .; __bss_end__ = .;
    } > RAM
    .heap (NOLOAD) : {
        . = ALIGN(8); PROVIDE(end = .); PROVIDE(_end = .);
        . += _Min_Heap_Size; . = ALIGN(8);
        _heap_limit = .;
    } > RAM
    .stack _stack_bottom (NOLOAD) : {
        . += _Min_Stack_Size;
    } > RAM
    ASSERT(_heap_limit <= _stack_bottom, "RAM data/heap overlap reserved stack")
    .ARM.attributes 0 : { *(.ARM.attributes) }
}
