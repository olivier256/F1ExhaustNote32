/* Édition de liens du banc sh2cycles, cache en mode « 2 voies + RAM » :
   comme bench.ld, mais le code de compose.c (la boucle de rendu) et
   celui de merge_sh2.S (option SUPERVDP_ASM) sont
   placés dans la RAM interne du SH2, en C000 0000h (2 Ko au plus).
   Sur la console, le programme le recopiera là au démarrage, après avoir
   mis le cache en mode 2 voies (CCR) ; sous qemu, le segment y est
   chargé directement, mais qemu (mode utilisateur) n'accepte pas
   d'adresse au-delà de 2 Go : run.sh passe --defsym RAM_BASE=0x50000000,
   et le greffon traite cette adresse comme la RAM interne (option
   ram=0x50000000). Le temps simulé est le même. */

RAM_BASE = DEFINED(RAM_BASE) ? RAM_BASE : 0xC0000000;

ENTRY(_start)

PHDRS
{
    sdram PT_LOAD;
    marker PT_LOAD;
    fb PT_LOAD;
    ram PT_LOAD;
}

SECTIONS
{
    . = 0x06000000;
    /* Tout le code en SDRAM, sauf celui de compose.c et de l'assembleur
       du moteur (*_sh2.o), qui va en RAM interne ; le code froid de
       compose.c (section .text.sdram) reste en SDRAM. */
    .text : {
        *(EXCLUDE_FILE(*compose.o *_sh2.o) .text .text.*)
        *compose.o(.text.sdram)
    } :sdram
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

    . = RAM_BASE;
    .ram : { *compose.o(.text .text.*) *_sh2.o(.text) } :ram
    ASSERT(SIZEOF(.ram) <= 0x800, "le code en RAM interne depasse 2 Ko")
}
