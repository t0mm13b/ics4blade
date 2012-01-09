#ifndef HASH_H
#define HASH_H

#include <common.h>
#include <libelf.h>
#include <gelf.h>

void setup_hash(Elf_Data *hash_data,
                Elf32_Word nbuckets,
                Elf32_Word nchains);

void add_to_hash(Elf_Data *hash_data,
                 const char *symbol,
                 int symindex);

int hash_lookup(Elf *elf, 
                section_info_t *hash,
                section_info_t *symtab,
                const char *symname,
                GElf_Sym *sym_mem);

#endif/*HASH_H*/
