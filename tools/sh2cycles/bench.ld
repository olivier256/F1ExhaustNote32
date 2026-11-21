/* Édition de liens du banc sh2cycles : la carte mémoire de la 32X.
   - SDRAM (accès par le cache) en 0600 0000h : code, données, tiles, pile ;
   - marqueurs du banc en 2000 0000h ;
   - frame buffer (sans cache) en 2400 0000h.
   Un segment par zone, pour que le fichier ne contienne pas les trous. */

ENTRY(_start)

PHDRS
{
    sdram PT_LOAD;
    marker PT_LOAD;
    fb PT_LOAD;
}

SECTIONS
{
    . = 0x06000000;
    .text : { *(.text .text.*) } :sdram
    .rodata : { *(.rodata .rodata.*) } :sdram
    .data : { *(.data .data.*) } :sdram
    .bss : { *(.bss .bss.*) *(COMMON) } :sdram
    . = ALIGN(16);
    .stack : { . += 0x2000; bench_stack_top = .; } :sdram
    bench_sdram_end = .;

    . = 0x20000000;
    .marker : { *(.marker) } :marker

    . = 0x24000000;
    .fb : { *(.fb) } :fb
}
