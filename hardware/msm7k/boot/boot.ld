SECTIONS {
   BOOTLOADER_START = 0x00000000 ;
  .text BOOTLOADER_START :
  {
    *(.text)
    . = ALIGN(8);
  }
  .init : { 
    . = ALIGN(8);
    BOOTLOADER_INIT_FIRST = . ;
    *(.init.func.0)
    BOOTLOADER_INIT_LAST = . ;
  }
  .data : { 
    . = ALIGN(8);
    *(.data) 
    . = ALIGN(8);
  }
  .bss  : { 
    BOOTLOADER_BSS = . ;
    . = ALIGN(8);
    *(.bss) *(COMMON) 
    . = ALIGN(8);
    BOOTLOADER_END = . ;
  }
  BOOTLOADER_STACK = 0x000FF000 ;
  BOOTLOADER_HEAP  = 0x00100000 ;
}

