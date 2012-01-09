#ifndef FIXDWARF_H
#define FIXDWARF_H

#include <elf.h>
#include <gelf.h>
#include <elfcopy.h>

extern void update_dwarf_if_necessary(
    Elf *elf, GElf_Ehdr *ehdr, Elf *newelf,
    shdr_info_t *shdr_info, int num_shdr_info,
    int *num_total_patches, int *num_failed_patches);


#endif
