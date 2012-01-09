#ifndef ELFCOPY_H
#define ELFCOPY_H

#include <libelf.h>
#include <libebl.h>
#include <elf.h>
#include <gelf.h>

typedef struct shdr_info_t {
	/* data from original file: */
	Elf_Scn *scn;			/* original section */
	/* Original-section header. */
    GElf_Shdr old_shdr;
    /* Starts out as the original header, but we modify this variable when we
       compose the new section information. */
	GElf_Shdr shdr;
    /* This oddly-named flag causes adjust_elf() to look at the size of the
       relocation sections before the modification, as opposed to the new
       size, in order to determine the number of relocation entries. */
    bool use_old_shdr_for_relocation_calculations;
	const char *name;		/* name of the original section */
    /* If we do not want to modify a section's data, we set this field to NULL.
       This will cause clone_elf() to extract the original section's data and
       copy it over to the new section.  If, on the other hand, we do want to
       change the data, we call elf_newdata() by ourselves and set *data to
       the return value.
    */
	Elf_Data *data;
    Elf_Data *newdata;

	/* data for new file  */

	/* Index in new file. Before we assign numbers to the sections in the
	   new file, the idx field has the following meaning:
		0 -- will strip
		1 -- present but not yet investigated
		2 -- handled (stripped or decided not to stip).
	*/
	Elf32_Word idx;
	Elf_Scn *newscn; /* new section handle */
	struct Ebl_Strent *se; /* contribution to shstr section */
	/* The following three variables are for symbol-table-sections (SHT_DYNSYM
       and SHT_SYMTAB).

       newsymidx: contains a mapping between the indices of old symbols and new
                  symbols. If a symbol table has changed, then newsymidx !=
                  NULL; otherwise, it is NULL.  Thus newsymidx can be used also
                  as a flag.

       dynsymst: handle to the new symbol-strings section.
	*/
	Elf32_Word *newsymidx;
    struct Ebl_Strtab *dynsymst;
    /* The following variable is used by SHT_DYNSYM, SHT_SYMTAB and SHT_DYNAMIC
       sections only.  For the symbol tables, this is a parallel array to the
       symbol table that stores the symbol name's index into the symbol-strings
       table.

       For the dynamic section, this is an array parallel to the array of
       structures that the dynamic section is; for each structure that
       represents a string field, the element at the same index into symse
       contains the offset of that string into the new dynamic-symbol table.
    */
    struct Ebl_Strent **symse;
} shdr_info_t;

/*
Symbol_filter:
	On input: symbol_filter[i] indicates whether to keep a symbol (1) or to
	          remove it from the symbol table.
    On output: symbol_filter[i] indicates whether a symbol was removed (0) or
	           kept (1) in the symbol table.
*/

void adjust_elf(Elf *elf, const char *elf_name,
                Elf *newelf, const char *newelf_name,
                Ebl *ebl,
                GElf_Ehdr *ehdr, /* store ELF header of original library */
                bool *sym_filter, int num_symbols,
                struct shdr_info_t *shdr_info, int shdr_info_len,
                GElf_Phdr *phdr_info,
                size_t highest_scn_num,
                size_t shnum,
                size_t shstrndx,
                struct Ebl_Strtab *shst,
                bool sections_dropped_or_rearranged,
                int dynamic_idx, /* index in shdr_info[] of .dynamic section */
                int dynsym_idx, /* index in shdr_info[] of dynamic symbol table */
                int shady,
                Elf_Data **shstrtab_data,
                bool adjust_section_offsets,
                bool rebuild_shstrtab);

#endif/*ELFCOPY_H*/
