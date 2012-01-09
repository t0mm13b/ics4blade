
#include <stdio.h>
#include <common.h>
#include <debug.h>
#include <hash.h>
#include <libelf.h>
#include <libebl.h>
#include <libebl_arm.h>
#include <elf.h>
#include <gelf.h>
#include <string.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#ifdef DEBUG
    #include <rangesort.h>
#endif

/* static void print_shdr_array(shdr_info_t *, int); */

#include <elfcopy.h>

#define COPY_SECTION_DATA_BUFFER (0)

/* When this macro is set to a nonzero value, we replace calls to elf_strptr()
   on the target ELF handle with code that extracts the strings directly from
   the data buffers of that ELF handle.  In this case, elf_strptr() does not
   work as expected, as it tries to read the data buffer of the associated
   string section directly from the file, and that buffer does not exist yet
   in the file, since we haven't committed our changes yet.
*/
#define ELF_STRPTR_IS_BROKEN     (1)

static void update_relocations_section_symbol_references(Elf *newelf, Elf *elf,
                                                         shdr_info_t *info, int info_len,
                                                         shdr_info_t *relsect_info,
                                                         Elf32_Word *newsymidx);

static void update_relocations_section_offsets(Elf *newelf, Elf *elf, Ebl *ebl,
                                               shdr_info_t *info,
                                               int info_len,
                                               shdr_info_t *relsect_info,
                                               Elf_Data *data,
                                               range_list_t *old_section_ranges);

static void update_hash_table(Elf *newelf, Elf *elf,
                              Elf32_Word hash_scn_idx,
                              shdr_info_t *symtab_info);

static inline
Elf_Data *create_section_data(shdr_info_t *, Elf_Scn *);

static Elf64_Off section_to_header_mapping(Elf *elf,
                                           int phdr_idx,
                                           shdr_info_t *shdr_info,
                                           int num_shdr_info,
                                           Elf64_Off *file_end,
                                           Elf64_Off *mem_end);

static void build_dynamic_segment_strings(Elf *elf, Ebl *oldebl,
                                          int dynidx, /* index of .dynamic section */
                                          int symtabidx, /* index of symbol table section */
                                          shdr_info_t *shdr_info,
                                          int shdr_info_len);

#ifdef DEBUG
static void print_dynamic_segment_strings(Elf *elf, Ebl *oldebl,
                                          int dynidx, /* index of .dynamic section */
                                          int symtabidx, /* index of symbol table section */
                                          shdr_info_t *shdr_info,
                                          int shdr_info_len);
#endif

static void adjust_dynamic_segment_offsets(Elf *elf, Ebl *oldebl,
                                           Elf *newelf,
                                           int idx, /* index of .dynamic section */
                                           shdr_info_t *shdr_info,
                                           int shdr_info_len);

static void update_symbol_values(Elf *elf, GElf_Ehdr *ehdr,
                                 Elf *newelf,
                                 shdr_info_t *shdr_info,
                                 int num_shdr_info,
                                 int shady,
                                 int dynamic_idx);

static bool section_belongs_to_header(GElf_Shdr *shdr, GElf_Phdr *phdr);

static range_list_t *
update_section_offsets(Elf *elf,
                       Elf *newelf,
                       GElf_Phdr *phdr_info,
                       shdr_info_t *shdr_info,
                       int num_shdr_info,
                       range_list_t *section_ranges,
                       bool adjust_alloc_section_offsets);

void handle_range_error(range_error_t err, range_t *left, range_t *right);

#ifdef DEBUG
static void
verify_elf(GElf_Ehdr *ehdr, struct shdr_info_t *shdr_info, int shdr_info_len,
           GElf_Phdr *phdr_info);
#endif

void adjust_elf(Elf *elf, const char *elf_name,
                Elf *newelf, const char *newelf_name __attribute__((unused)),
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
                bool adjust_alloc_section_offsets,
                bool rebuild_shstrtab)
{
    int cnt;      /* general-purpose counter */
    Elf_Scn *scn; /* general-purpose section */

    *shstrtab_data = NULL;

    /* When this flag is true, we have dropped some symbols, which caused
       a change in the order of symbols in the symbol table (all symbols after
       the removed symbol have shifted forward), and a change in its size as
       well.  When the symbol table changes this way, we need to modify the
       relocation entries that relocate symbols in this symbol table, and we
       also need to rebuild the hash table (the hash is outdated).

       Note that it is possible to change the symbols in the symbol table
       without changing their position (that is, without cutting any symbols
       out).  If a section that a symbol refers to changes (i.e., moves), we
       need to update that section's index in the symbol entry in the symbol
       table.  Therefore, there are symbol-table changes that can be made and
       still have symtab_size_changed == false!
    */
    bool symtab_size_changed = false;

    /* We allow adjusting of offsets only for files that are shared libraries.
       We cannot mess with the relative positions of sections for executable
       files, because we do not have enough information to adjust them.  The
       text section is already linked to fixed addresses.
    */
    ASSERT(!adjust_alloc_section_offsets || ehdr->e_type == ET_DYN);

    if (!sections_dropped_or_rearranged)
         INFO("Note: we aren't dropping or rearranging any sections.\n");

    /* Index of the section header table in the shdr_info array.  This is
       an important variable because it denotes the last section of the old
       file, as well as the location of the section-strings section of the
       new one.

       Note: we use this variable only when we are re-creating the section-
       header-strings table.  Otherwise, we keep it as zero.
    */

    size_t shdridx = shstrndx;
    if (rebuild_shstrtab) {
        INFO("Creating new section-strings section...\n");

        shdridx = shnum;

        /* Create the new section-name-strings section */
        {
            INFO("\tNew index will be %d (was %d).\n", highest_scn_num, shstrndx);

            /* Add the section header string table section name. */
            shdr_info[shdridx] = shdr_info[shstrndx];
            ASSERT(!strcmp(shdr_info[shdridx].name, ".shstrtab"));
            shdr_info[shdridx].se = ebl_strtabadd (shst, ".shstrtab", 10);
            ASSERT(shdr_info[shdridx].se != NULL);
            shdr_info[shdridx].idx = highest_scn_num;

            /* Create the section header. */
            shdr_info[shdridx].shdr.sh_type = SHT_STRTAB;
            shdr_info[shdridx].shdr.sh_flags = 0;
            shdr_info[shdridx].shdr.sh_addr = 0;
            shdr_info[shdridx].shdr.sh_link = SHN_UNDEF;
            shdr_info[shdridx].shdr.sh_info = SHN_UNDEF;
            shdr_info[shdridx].shdr.sh_entsize = 0;

            shdr_info[shdridx].shdr.sh_offset = shdr_info[shdridx].old_shdr.sh_offset;
            shdr_info[shdridx].shdr.sh_addralign = 1;

            /* Create the section. */
            FAILIF_LIBELF((shdr_info[shdridx].newscn = elf_newscn(newelf)) == NULL,
                          elf_newscn);
            ASSERT(elf_ndxscn (shdr_info[shdridx].newscn) == highest_scn_num);

            {
                /* Finalize the string table and fill in the correct indices in
                   the section headers. */
                FAILIF_LIBELF((*shstrtab_data =
                               elf_newdata (shdr_info[shdridx].newscn)) == NULL,
                              elf_newdata);
                ebl_strtabfinalize (shst, *shstrtab_data);
                /* We have to set the section size. */
                INFO("\tNew size will be %d.\n", (*shstrtab_data)->d_size);
                shdr_info[shdridx].shdr.sh_size = (*shstrtab_data)->d_size;
                /* Setting the data pointer tells the update loop below not to
                   copy the information from the original section. */

                shdr_info[shdridx].data = *shstrtab_data;
#if COPY_SECTION_DATA_BUFFER
                shdr_info[shdridx].data->d_buf = MALLOC(shdr_info[shdridx].data->d_size);
                ASSERT((*shstrtab_data)->d_buf);
                memcpy(shdr_info[shdridx].data->d_buf, (*shstrtab_data)->d_buf, (*shstrtab_data)->d_size);
#endif
            }
        }
    } /* if (rebuild_shstrtab) */
    else {
        /* When we are not rebuilding shstrtab, we expect the input parameter
           shstrndx to be the index of .shstrtab BOTH in shdr_info[] and in
           as a section index in the ELF file.
        */
        ASSERT(!strcmp(shdr_info[shdridx].name, ".shstrtab"));
    }

    INFO("Updating section information...\n");
    /* Update the section information. */

#ifdef DEBUG
    /* We use this flag to ASSERT that the symbol tables comes
       before the .dynamic section in the file.  See comments
       further below.
    */
    bool visited_dynsym = false;
#endif

    for (cnt = 1; cnt < shdr_info_len; ++cnt) {
        if (shdr_info[cnt].idx > 0) {
            Elf_Data *newdata;

            INFO("\t%03d: Updating section %s (index %d, address %lld offset %lld, size %lld, alignment %d)...\n",
                 cnt,
                 (shdr_info[cnt].name ?: "(no name)"),
                 shdr_info[cnt].idx,
                 shdr_info[cnt].shdr.sh_addr,
                 shdr_info[cnt].shdr.sh_offset,
                 shdr_info[cnt].shdr.sh_size,
                 shdr_info[cnt].shdr.sh_addralign);

            scn = shdr_info[cnt].newscn;
            ASSERT(scn != NULL);
            ASSERT(scn == elf_getscn(newelf, shdr_info[cnt].idx));

            /* Update the name. */
            if (rebuild_shstrtab) {
                Elf64_Word new_sh_name = ebl_strtaboffset(shdr_info[cnt].se);
                INFO("\t\tname offset %d (was %d).\n",
                     new_sh_name,
                     shdr_info[cnt].shdr.sh_name);
                shdr_info[cnt].shdr.sh_name = new_sh_name;
            }

            /* Update the section header from the input file.  Some fields
               might be section indices which now have to be adjusted. */
            if (shdr_info[cnt].shdr.sh_link != 0) {
                INFO("\t\tsh_link %d (was %d).\n",
                     shdr_info[shdr_info[cnt].shdr.sh_link].idx,
                     shdr_info[cnt].shdr.sh_link);

                shdr_info[cnt].shdr.sh_link =
                shdr_info[shdr_info[cnt].shdr.sh_link].idx;
            }

            /* Handle the SHT_REL, SHT_RELA, and SHF_INFO_LINK flag. */
            if (SH_INFO_LINK_P (&shdr_info[cnt].shdr)) {
                INFO("\t\tsh_info %d (was %d).\n",
                     shdr_info[shdr_info[cnt].shdr.sh_info].idx,
                     shdr_info[cnt].shdr.sh_info);

                shdr_info[cnt].shdr.sh_info =
                shdr_info[shdr_info[cnt].shdr.sh_info].idx;
            }

            /* Get the data from the old file if necessary.  We already
               created the data for the section header string table, which
               has a section number equal to shnum--hence the ASSERT().
            */
            ASSERT(!rebuild_shstrtab || shdr_info[cnt].data || cnt < shnum);
            newdata = create_section_data(shdr_info + cnt, scn);

            /* We know the size. */
            shdr_info[cnt].shdr.sh_size = shdr_info[cnt].data->d_size;

            /* We have to adjust symbol tables.  Each symbol contains
               a reference to the section it belongs to.  Since we have
               renumbered the sections (and dropped some), we need to adjust
               the symbols' section indices as well.  Also, if we do not want
               to keep a symbol, we drop it from the symbol table in this loop.

               When we drop symbols from the dynamic-symbol table, we need to
               remove the names of the sybmols from the dynamic-symbol-strings
               table.  Changing the dynamic-symbol-strings table means that we
               also have to rebuild the strings that go into the .dynamic
               section (such as the DT_NEEDED strings, which lists the libraries
               that the file depends on), since those strings are kept in the
               same dynamic-symbol-strings table.  That latter statement
               is an assumption (which we ASSERT against, read on below).

               Note: we process the symbol-table sections only when the user
               specifies a symbol filter AND that leads to a change in the
               symbol table, or when section indices change.
            */

            /* The .dynamic section's strings need not be contained in the
               same section as the strings of the dynamic symbol table,
               but we assume that they are (I haven't seen it be otherwise).
               We assert the validity of our assumption here.

               If this assertion fails, then we *may* need to reorganize
               this code as follows: we will need to call function
               build_dynamic_segment_strings() even when sections numbers
               don't change and there is no filter.  Also, if string section
               containing the .dynamic section strings changes, then we'd
               need to update the sh_link of the .dynamic section to point
               to the new section.
            */

            ASSERT(shdr_info[dynamic_idx].shdr.sh_link ==
                   shdr_info[dynsym_idx].shdr.sh_link);

            if (sections_dropped_or_rearranged || (sym_filter != NULL))
            {
                if(shdr_info[cnt].shdr.sh_type == SHT_DYNSYM)
                {
                    INFO("\t\tupdating a symbol table.\n");

                    /* Calculate the size of the external representation of a
                       symbol. */
                    size_t elsize = gelf_fsize (elf, ELF_T_SYM, 1, ehdr->e_version);

                    /* Check the length of the dynamic-symbol filter. (This is the
                       second of two identical checks, the first one being in
                       the loop that checks for exceptions.)

                       NOTE: We narrow this assertion down to the dynamic-symbol
                             table only.  Since we expect the symbol filter to
                             be parallel to .dynsym, and .dynsym in general
                             contains fewer symbols than .strtab, we cannot
                             make this assertion for .strtab.
                    */
                    FAILIF(sym_filter != NULL &&
                           num_symbols != shdr_info[cnt].data->d_size / elsize,
                           "Length of dynsym filter (%d) must equal the number"
                           " of dynamic symbols (%d) in section [%s]!\n",
                           num_symbols,
                           shdr_info[cnt].data->d_size / elsize,
                           shdr_info[cnt].name);

                    shdr_info[cnt].symse =
                        (struct Ebl_Strent **)MALLOC(
                            (shdr_info[cnt].data->d_size/elsize) *
                            sizeof(struct Ebl_Strent *));
                    shdr_info[cnt].dynsymst = ebl_strtabinit(1);
                    FAILIF_LIBELF(NULL == shdr_info[cnt].dynsymst, ebl_strtabinit);

                    /* Allocate an array of Elf32_Word, one for each symbol.  This
                       array will hold the new symbol indices.
                    */
                    shdr_info[cnt].newsymidx =
                    (Elf32_Word *)CALLOC(shdr_info[cnt].data->d_size / elsize,
                                         sizeof (Elf32_Word));

                    bool last_was_local = true;
                    size_t destidx, // index of the symbol in the new symbol table
                        inner,  // index of the symbol in the old table
                        last_local_idx = 0;
                    int num_kept_undefined_and_special = 0;
                    int num_kept_global_or_weak = 0;
                    int num_thrown_away = 0;

                    unsigned long num_symbols = shdr_info[cnt].data->d_size / elsize;
                    INFO("\t\tsymbol table has %ld symbols.\n", num_symbols);

                    /* In the loop below, determine whether to remove or not each
                       symbol.
                    */
                    for (destidx = inner = 1; inner < num_symbols; ++inner)
                    {
                        Elf32_Word sec; /* index of section a symbol refers to */
                        Elf32_Word xshndx; /* extended-section index of symbol */
                        /* Retrieve symbol information and separate section index
                           from the symbol table at the given index. */
                        GElf_Sym sym_mem; /* holds the symbol */

                        /* Retrieve symbol information and separate section index
                           from the symbol table at the given index. */
                        GElf_Sym *sym = gelf_getsymshndx (shdr_info[cnt].data,
                                                          NULL, inner,
                                                          &sym_mem, &xshndx);
                        ASSERT(sym != NULL);

                        FAILIF(sym->st_shndx == SHN_XINDEX,
                               "Can't handle symbol's st_shndx == SHN_XINDEX!\n");

                        /* Do not automatically strip the symbol if:
                            -- the symbol filter is NULL or
                            -- the symbol is marked to keep or
                            -- the symbol is neither of:
                                -- imported or refers to a nonstandard section
                                -- global
                                -- weak

                            We do not want to strip imported symbols, because then
                            we won't be able to link against them.  We do not want
                            to strip global or weak symbols, because then someone
                            else will fail to link against them.  Finally, we do
                            not want to strip nonstandard symbols, because we're
                            not sure what they are doing there.
                        */

                        char *symname = elf_strptr(elf,
                                                   shdr_info[cnt].old_shdr.sh_link,
                                                   sym->st_name);

                        if (NULL == sym_filter || /* no symfilter */
                            sym_filter[inner] ||  /* keep the symbol! */
                            /* don't keep the symbol, but the symbol is undefined
                               or refers to a specific section */
                            sym->st_shndx == SHN_UNDEF || sym->st_shndx >= shnum ||
                            /* don't keep the symbol, which defined and refers to
                               a normal section, but the symbol is neither global
                               nor weak. */
                            (ELF32_ST_BIND(sym->st_info) != STB_GLOBAL &&
                             ELF32_ST_BIND(sym->st_info) != STB_WEAK))
                        {
                            /* Do not remove the symbol. */
                            if (sym->st_shndx == SHN_UNDEF ||
                                sym->st_shndx >= shnum)
                            {
                                /* This symbol has no section index (it is
                                   absolute). Leave the symbol alone unless it is
                                   moved. */
                                FAILIF_LIBELF(!(destidx == inner ||
                                                gelf_update_symshndx(
                                                    shdr_info[cnt].data,
                                                    NULL,
                                                    destidx,
                                                    sym,
                                                    xshndx)),
                                              gelf_update_symshndx);

                                shdr_info[cnt].newsymidx[inner] = destidx;
                                INFO("\t\t\tkeeping %s symbol %d (new index %d), name [%s]\n",
                                     (sym->st_shndx == SHN_UNDEF ? "undefined" : "special"),
                                     inner,
                                     destidx,
                                     symname);
                                /* mark the symbol as kept */
                                if (sym_filter) sym_filter[inner] = 1;
                                shdr_info[cnt].symse[destidx] =
                                    ebl_strtabadd (shdr_info[cnt].dynsymst,
                                                   symname, 0);
                                ASSERT(shdr_info[cnt].symse[destidx] != NULL);
                                num_kept_undefined_and_special++;
                                if (GELF_ST_BIND(sym->st_info) == STB_LOCAL)
                                    last_local_idx = destidx;
                                destidx++;
                            } else {
                                /* Get the full section index. */
                                sec = shdr_info[sym->st_shndx].idx;

                                if (sec) {
                                    Elf32_Word nxshndx;

                                    ASSERT (sec < SHN_LORESERVE);
                                    nxshndx = 0;

                                    /* Update the symbol only if something changed,
                                       that is, if either the symbol's position in
                                       the symbol table changed (because we deleted
                                       some symbols), or because its section moved!

                                       NOTE: We don't update the symbol's section
                                       index, sym->st_shndx here, but in function
                                       update_symbol_values() instead.  The reason
                                       is that if we update the symbol-section index,
                                       now, it won't refer anymore to the shdr_info[]
                                       entry, which we will need in
                                       update_symbol_values().
                                    */
                                    if (inner != destidx)
                                    {
                                        FAILIF_LIBELF(0 ==
                                                      gelf_update_symshndx(
                                                          shdr_info[cnt].data,
                                                          NULL,
                                                          destidx, sym,
                                                          nxshndx),
                                                      gelf_update_symshndx);
                                    }

                                    shdr_info[cnt].newsymidx[inner] = destidx;

                                    /* If we are not filtering out some symbols,
                                       there's no point to printing this message
                                       for every single symbol. */
                                    if (sym_filter) {
                                        INFO("\t\t\tkeeping symbol %d (new index %d), name (index %d) [%s]\n",
                                             inner,
                                             destidx,
                                             sym->st_name,
                                             symname);
                                        /* mark the symbol as kept */
                                        sym_filter[inner] = 1;
                                    }
                                    shdr_info[cnt].symse[destidx] =
                                        ebl_strtabadd(shdr_info[cnt].dynsymst,
                                                      symname, 0);
                                    ASSERT(shdr_info[cnt].symse[destidx] != NULL);
                                    num_kept_global_or_weak++;
                                    if (GELF_ST_BIND(sym->st_info) == STB_LOCAL)
                                        last_local_idx = destidx;
                                    destidx++;
                                } else {
                                    /* I am not sure, there might be other types of
                                       symbols that do not refer to any section, but
                                       I will handle them case by case when this
                                       assertion fails--I want to know if each of them
                                       is safe to remove!
                                    */
                                    ASSERT(GELF_ST_TYPE (sym->st_info) == STT_SECTION ||
                                           GELF_ST_TYPE (sym->st_info) == STT_NOTYPE);
                                    INFO("\t\t\tignoring %s symbol [%s]"
                                         " at index %d refering to section %d\n",
                                         (GELF_ST_TYPE(sym->st_info) == STT_SECTION
                                          ? "STT_SECTION" : "STT_NOTYPE"),
                                         symname,
                                         inner,
                                         sym->st_shndx);
                                    num_thrown_away++;
                                    /* mark the symbol as thrown away */
                                    if (sym_filter) sym_filter[inner] = 0;
                                }
                            }
                        } /* to strip or not to strip? */
                        else {
                            INFO("\t\t\tremoving symbol [%s]\n", symname);
                            shdr_info[cnt].newsymidx[inner] = (Elf32_Word)-1;
                            num_thrown_away++;
                            /* mark the symbol as thrown away */
                            if (sym_filter) sym_filter[inner] = 0;
                        }

                        /* For symbol-table sections, sh_info is one greater than the
                           symbol table index of the last local symbol.  This is why,
                           when we find the last local symbol, we update the sh_info
                           field.
                        */

                        if (last_was_local) {
                            if (GELF_ST_BIND (sym->st_info) != STB_LOCAL) {
                                last_was_local = false;
                                if (last_local_idx) {
                                    INFO("\t\t\tMARKING ONE PAST LAST LOCAL INDEX %d\n",
                                         last_local_idx + 1);
                                    shdr_info[cnt].shdr.sh_info =
                                        last_local_idx + 1;
                                }
                                else shdr_info[cnt].shdr.sh_info = 0;

                            }
                        } else FAILIF(0 && GELF_ST_BIND (sym->st_info) == STB_LOCAL,
                                      "Internal error in ELF file: symbol table has"
                                      " local symbols after first global"
                                      " symbol!\n");
                    } /* for each symbol */

                    INFO("\t\t%d undefined or special symbols were kept.\n",
                         num_kept_undefined_and_special);
                    INFO("\t\t%d global or weak symbols were kept.\n",
                         num_kept_global_or_weak);
                    INFO("\t\t%d symbols were thrown away.\n",
                         num_thrown_away);

                    if (destidx != inner) {
                        /* The symbol table changed. */
                        INFO("\t\t\tthe symbol table has changed.\n");
                        INFO("\t\t\tdestidx = %d, inner = %d.\n", destidx, inner);
                        INFO("\t\t\tnew size %d (was %lld).\n",
                             destidx * elsize,
                             shdr_info[cnt].shdr.sh_size);
                        shdr_info[cnt].shdr.sh_size = newdata->d_size = destidx * elsize;
                        symtab_size_changed = true;
                    } else {
                        /* The symbol table didn't really change. */
                        INFO("\t\t\tthe symbol table did not change.\n");
                        FREE (shdr_info[cnt].newsymidx);
                        shdr_info[cnt].newsymidx = NULL;
                    }
#ifdef DEBUG
                    visited_dynsym = shdr_info[cnt].shdr.sh_type == SHT_DYNSYM;
#endif
                } /* if it's a symbol table... */
                else if (shdr_info[cnt].shdr.sh_type == SHT_DYNAMIC) {
                    /* We get here either when we drop some sections, or
                       when we are dropping symbols.  If we are not dropping
                       symbols, then the dynamic-symbol-table and its strings
                       section won't change, so we won't need to rebuild the
                       symbols for the SHT_DYNAMIC section either.

                       NOTE: If ever in the future we add the ability in
                       adjust_elf() to change the strings in the SHT_DYNAMIC
                       section, then we would need to find a way to rebuild
                       the dynamic-symbol-table-strings section.
                    */

                    /* symtab_size_changed has a meaningful value only after
                       we've processed the symbol table.  If this assertion
                       is ever violated, it will be because the .dynamic section
                       came before the symbol table in the list of section in
                       a file.  If that happens, then we have to break up the
                       loop into two: one that finds and processes the symbol
                       tables, and another, after the first one, that finds
                       and handles the .dynamic sectio.
                     */
                    ASSERT(visited_dynsym == true);
                    if (sym_filter != NULL && symtab_size_changed) {
                        /* Walk the old dynamic segment.  For each tag that represents
                           a string, build an entry into the dynamic-symbol-table's
                           strings table. */
                        INFO("\t\tbuilding strings for the dynamic section.\n");
                        ASSERT(cnt == dynamic_idx);

                        /* NOTE:  By passing the the index (in shdr_info[]) of the
                           dynamic-symbol table to build_dynamic_segment_strings(),
                           we are making the assumption that those strings will be
                           kept in that table.  While this does not seem to be
                           mandated by the ELF spec, it seems to be always the case.
                           Where else would you put these strings?  You already have
                           the dynamic-symbol table and its strings table, and that's
                           guaranteed to be in the file, so why not put it there?
                        */
                        build_dynamic_segment_strings(elf, ebl,
                                                      dynamic_idx,
                                                      dynsym_idx,
                                                      shdr_info,
                                                      shdr_info_len);
                    }
                    else {
                        INFO("\t\tThe dynamic-symbol table is not changing, so no "
                             "need to rebuild strings for the dynamic section.\n");
#ifdef DEBUG
                        print_dynamic_segment_strings(elf, ebl,
                                                      dynamic_idx,
                                                      dynsym_idx,
                                                      shdr_info,
                                                      shdr_info_len);
#endif
                    }
                }
            }

            /* Set the section header in the new file. There cannot be any
               overflows. */
            INFO("\t\tupdating section header (size %lld)\n",
                 shdr_info[cnt].shdr.sh_size);

            FAILIF(!gelf_update_shdr (scn, &shdr_info[cnt].shdr),
                   "Could not update section header for section %s!\n",
                   shdr_info[cnt].name);
        } /* if (shdr_info[cnt].idx > 0) */
        else INFO("\t%03d: not updating section %s, it will be discarded.\n",
                  cnt,
                  shdr_info[cnt].name);
    } /* for (cnt = 1; cnt < shdr_info_len; ++cnt) */

    /* Now, if we removed some symbols and thus modified the symbol table,
       we need to update the hash table, the relocation sections that use these
       symbols, and the symbol-strings table to cut out the unused symbols.
    */
    if (symtab_size_changed) {
        for (cnt = 1; cnt < shnum; ++cnt) {
            if (shdr_info[cnt].idx == 0) {
                /* Ignore sections which are discarded, unless these sections
                   are relocation sections.  This case is for use by the
                   prelinker. */
                if (shdr_info[cnt].shdr.sh_type != SHT_REL &&
                    shdr_info[cnt].shdr.sh_type != SHT_RELA) {
                    continue;
                }
            }

            if (shdr_info[cnt].shdr.sh_type == SHT_REL ||
                shdr_info[cnt].shdr.sh_type == SHT_RELA) {
                /* shdr_info[cnt].old_shdr.sh_link is index of old symbol-table
                   section that this relocation-table section was relative to.
                   We can access shdr_info[] at that index to get to the
                   symbol-table section.
                */
                Elf32_Word *newsymidx =
                shdr_info[shdr_info[cnt].old_shdr.sh_link].newsymidx;

                /* The referred-to-section must be a symbol table!  Note that
                   alrhough shdr_info[cnt].shdr refers to the updated section
                   header, this assertion is still valid, since when updating
                   the section header we never modify the sh_type field.
                */
                {
                    Elf64_Word sh_type =
                    shdr_info[shdr_info[cnt].shdr.sh_link].shdr.sh_type;
                    FAILIF(sh_type != SHT_DYNSYM,
                           "Section refered to from relocation section is not"
                           " a dynamic symbol table (sh_type=%d)!\n",
                           sh_type);
                }

                /* If that symbol table hasn't changed, then its newsymidx
                   field is NULL (see comments to shdr_info_t), so we
                   don't have to update this relocation-table section
                */
                if (newsymidx == NULL) continue;

                update_relocations_section_symbol_references(newelf, elf,
                                                             shdr_info, shnum,
                                                             shdr_info + cnt,
                                                             newsymidx);

            } else if (shdr_info[cnt].shdr.sh_type == SHT_HASH) {
                /* We have to recompute the hash table.  A hash table's
                   sh_link field refers to the symbol table for which the hash
                   table is generated.
                */
                Elf32_Word symtabidx = shdr_info[cnt].old_shdr.sh_link;

                /* We do not have to recompute the hash table if the symbol
                   table was not changed. */
                if (shdr_info[symtabidx].newsymidx == NULL)
                    continue;

                FAILIF(shdr_info[cnt].shdr.sh_entsize != sizeof (Elf32_Word),
                       "Can't handle 64-bit ELF files!\n");

                update_hash_table(newelf,  /* new ELF */
                                  elf,     /* old ELF */
                                  shdr_info[cnt].idx, /* hash table index */
                                  shdr_info + symtabidx);
            } /* if SHT_REL else if SHT_HASH ... */
            else if (shdr_info[cnt].shdr.sh_type == SHT_DYNSYM)
            {
                /* The symbol table's sh_link field contains the index of the
                   strings table for this symbol table.  We want to find the
                   index of the section in the shdr_info[] array.  That index
                   corresponds to the index of the section in the original ELF file,
                   which is why we look at shdr_info[cnt].old_shdr and not
                   shdr_info[cnt].shdr.
                */

                int symstrndx = shdr_info[cnt].old_shdr.sh_link;
                INFO("Updating [%s] (symbol-strings-section data for [%s]).\n",
                     shdr_info[symstrndx].name,
                     shdr_info[cnt].name);
                ASSERT(shdr_info[symstrndx].newscn);
                size_t new_symstrndx = elf_ndxscn(shdr_info[symstrndx].newscn);
                Elf_Data *newdata = elf_getdata(shdr_info[symstrndx].newscn, NULL);
                ASSERT(NULL != newdata);
                INFO("\tbefore update:\n"
                     "\t\tbuffer: %p\n"
                     "\t\tsize: %d\n",
                     newdata->d_buf,
                     newdata->d_size);
                ASSERT(shdr_info[cnt].dynsymst);
                ebl_strtabfinalize (shdr_info[cnt].dynsymst, newdata);
                INFO("\tafter update:\n"
                     "\t\tbuffer: %p\n"
                     "\t\tsize: %d\n",
                     newdata->d_buf,
                     newdata->d_size);
                FAILIF(new_symstrndx != shdr_info[cnt].shdr.sh_link,
                       "The index of the symbol-strings table according to elf_ndxscn() is %d, "
                       "according to shdr_info[] is %d!\n",
                       new_symstrndx,
                       shdr_info[cnt].shdr.sh_link);

                INFO("%d nonprintable\n",
                     dump_hex_buffer(stdout, newdata->d_buf, newdata->d_size, 0));

                shdr_info[symstrndx].shdr.sh_size = newdata->d_size;
                FAILIF(!gelf_update_shdr(shdr_info[symstrndx].newscn,
                                         &shdr_info[symstrndx].shdr),
                       "Could not update section header for section %s!\n",
                       shdr_info[symstrndx].name);

                /* Now, update the symbol-name offsets. */
                {
                    size_t i;
                    size_t elsize = gelf_fsize (elf, ELF_T_SYM, 1, ehdr->e_version);
                    for (i = 1; i < shdr_info[cnt].shdr.sh_size / elsize; ++i) {
                        Elf32_Word xshndx;
                        GElf_Sym sym_mem;
                        /* retrieve the symbol information; */
                        GElf_Sym *sym = gelf_getsymshndx (shdr_info[cnt].data,
                                                          NULL, i,
                                                          &sym_mem, &xshndx);
                        ASSERT(sym != NULL);
                        ASSERT(NULL != shdr_info[cnt].symse[i]);
                        /* calculate the new name offset; */
                        size_t new_st_name =
                            ebl_strtaboffset(shdr_info[cnt].symse[i]);
#if 1
                        ASSERT(!strcmp(newdata->d_buf + new_st_name,
                                       elf_strptr(elf, shdr_info[cnt].old_shdr.sh_link,
                                                  sym->st_name)));
#endif
                        if (sym_filter && (sym->st_name != new_st_name)) {
                            /* FIXME: For some reason, elf_strptr() does not return the updated
                               string value here.  It looks like ebl_strtabfinalize() doesn't
                               update libelf's internal structures well enough for elf_strptr()
                               to work on an ELF file that's being compose.
                            */
                            INFO("Symbol [%s]'s name (index %d, old value %llx) changes offset: %d -> %d\n",
#if 0
                                 newdata->d_buf + new_st_name,
#else
                                 elf_strptr(elf, shdr_info[cnt].old_shdr.sh_link,
                                            sym->st_name),
#endif
                                 i,
                                 sym->st_value,
                                 sym->st_name,
                                 new_st_name);
                        }
                        sym->st_name = new_st_name;
                        /* update the symbol info; */
                        FAILIF_LIBELF(0 ==
                                      gelf_update_symshndx(
                                          shdr_info[cnt].data,
                                          NULL,
                                          i, sym,
                                          xshndx),
                                      gelf_update_symshndx);
                    } /* for each symbol... */
                }
            }

            FAILIF(shdr_info[cnt].shdr.sh_type == SHT_GNU_versym,
                   "Can't handle SHT_GNU_versym!\n");
            FAILIF(shdr_info[cnt].shdr.sh_type == SHT_GROUP,
                   "Can't handle section groups!\n");
        } /* for (cnt = 1; cnt < shnum; ++cnt) */
    } /* if (symtab_size_changed) */


    range_list_t *old_section_ranges = init_range_list();
    range_list_t *section_ranges = NULL;
    /* Analyze gaps in the ranges before we compact the sections. */
    INFO("Analyzing gaps in ranges before compacting sections...\n");
    {
        size_t scnidx;
        /* Gather the ranges */
        for (scnidx = 1; scnidx < shdr_info_len; scnidx++) {
            if (shdr_info[scnidx].idx > 0) {
                if (/*shdr_info[scnidx].old_shdr.sh_type != SHT_NOBITS &&*/
                    shdr_info[scnidx].old_shdr.sh_flags & SHF_ALLOC) {
                    add_unique_range_nosort(
                        old_section_ranges,
                        shdr_info[scnidx].old_shdr.sh_addr,
                        shdr_info[scnidx].old_shdr.sh_size,
                        shdr_info + scnidx,
                        handle_range_error,
                        NULL);
                }
            }
        }
        sort_ranges(old_section_ranges);
#ifdef DEBUG
        int num_ranges;
        /* Analyze gaps in the ranges before we compact the sections. */
        range_t *ranges = get_sorted_ranges(old_section_ranges, &num_ranges);
        if (ranges) {
            GElf_Off last_end = ranges->start;
            int i;
            for (i = 0; i < num_ranges; i++) {
                shdr_info_t *curr = (shdr_info_t *)ranges[i].user;
                ASSERT(ranges[i].start >= last_end);
                int col_before, col_after;
                INFO("[%016lld, %016lld] %n[%s]%n",
                     ranges[i].start,
                     ranges[i].start + ranges[i].length,
                     &col_before,
                     curr->name,
                     &col_after);
                if (ranges[i].start > last_end) {
                    shdr_info_t *prev = (shdr_info_t *)ranges[i-1].user;
                    ASSERT(prev && curr);
                    while (col_after++ - col_before < 20) INFO(" ");
                    INFO(" [GAP: %lld bytes with %s]\n",
                         (ranges[i].start - last_end),
                         prev->name);
                }
                else INFO("\n");
                last_end = ranges[i].start + ranges[i].length;
            }
        }
#endif/*DEBUG*/
    }

    /* Calculate the final section offsets */
    INFO("Calculating new section offsets...\n");
    section_ranges = update_section_offsets(elf,
                                            newelf,
                                            phdr_info,
                                            shdr_info,
                                            shdr_info_len,
                                            init_range_list(),
                                            adjust_alloc_section_offsets);

#ifdef DEBUG
    {
        /* Analyze gaps in the ranges after we've compacted the sections. */
        int num_ranges;
        range_t *ranges = get_sorted_ranges(section_ranges, &num_ranges);
        if (ranges) {
            int last_end = ranges->start;
            int i;
            for (i = 0; i < num_ranges; i++) {
                shdr_info_t *curr = (shdr_info_t *)ranges[i].user;
                ASSERT(ranges[i].start >= last_end);
                int col_before, col_after;
                INFO("[%016lld, %016lld] %n[%s]%n",
                     ranges[i].start,
                     ranges[i].start + ranges[i].length,
                     &col_before,
                     curr->name,
                     &col_after);
                if (ranges[i].start > last_end) {
                    shdr_info_t *prev = (shdr_info_t *)ranges[i-1].user;
                    ASSERT(prev && curr);
                    while (col_after++ - col_before < 20) INFO(" ");
                    INFO(" [GAP: %lld bytes with %s]\n",
                         (ranges[i].start - last_end),
                         prev->name);
                }
                else INFO("\n");
                last_end = ranges[i].start + ranges[i].length;
            }
        }
    }
#endif

    {
        /* Now that we have modified the section offsets, we need to scan the
           symbol tables once again and update their st_value fields.  A symbol's
           st_value field (in a shared library) contains the virtual address of the
           symbol.  For each symbol we encounter, we look up the section it was in.
           If that section's virtual address has changed, then we calculate the
           delta and update the symbol.
        */

#if 0
        {
            /* for debugging: Print out all sections and their data pointers and
               sizes. */
            int i = 1;
            for (; i < shdr_info_len; i++) {
                PRINT("%8d: %-15s: %2lld %8lld %08lx (%08lx:%8d) %08lx (%08lx:%8d)\n",
                      i,
                      shdr_info[i].name,
                      shdr_info[i].shdr.sh_entsize,
                      shdr_info[i].shdr.sh_addralign,
                      (long)shdr_info[i].data,
                      (long)(shdr_info[i].data ? shdr_info[i].data->d_buf : 0),
                      (shdr_info[i].data ? shdr_info[i].data->d_size : 0),
                      (long)shdr_info[i].newdata,
                      (long)(shdr_info[i].newdata ? shdr_info[i].newdata->d_buf : 0),
                      (shdr_info[i].newdata ? shdr_info[i].newdata->d_size : 0));
                if (!strcmp(shdr_info[i].name, ".got") /* ||
                                                          !strcmp(shdr_info[i].name, ".plt") */) {
                    dump_hex_buffer(stdout,
                                    shdr_info[i].newdata->d_buf,
                                    shdr_info[i].newdata->d_size,
                                    shdr_info[i].shdr.sh_entsize);
                }
            }
        }
#endif

        INFO("Updating symbol values...\n");
        update_symbol_values(elf, ehdr, newelf, shdr_info, shdr_info_len,
                             shady,
                             dynamic_idx);

        /* If we are not stripping the debug sections, then we need to adjust
         * them accordingly, so that the new ELF file is actually debuggable.
         * For that glorios reason, we call update_dwarf().  Note that
         * update_dwarf() won't do anything if there, in fact, no debug
         * sections to speak of.
         */

        INFO("Updating DWARF records...\n");
        int num_total_dwarf_patches = 0, num_failed_dwarf_patches = 0;
        update_dwarf_if_necessary(
            elf, ehdr, newelf,
            shdr_info, shdr_info_len,
            &num_total_dwarf_patches, &num_failed_dwarf_patches);
        INFO("DWARF: %-15s: total %8d failed %8d.\n", elf_name, num_total_dwarf_patches, num_failed_dwarf_patches);

        /* Adjust the program-header table.  Since the file offsets of the various
           sections may have changed, the file offsets of their containing segments
           must change as well.  We update those offsets in the loop below.
        */
        {
            INFO("Adjusting program-header table...\n");
            int pi; /* program-header index */
            for (pi = 0; pi < ehdr->e_phnum; ++pi) {
                /* Print the segment number.  */
                INFO("\t%2.2zu\t", pi);
                INFO("PT_ header type: %d", phdr_info[pi].p_type);
                if (phdr_info[pi].p_type == PT_NULL) {
                    INFO(" PT_NULL (skip)\n");
                }
                else if (phdr_info[pi].p_type == PT_PHDR) {
                    INFO(" PT_PHDR\n");
                    ASSERT(phdr_info[pi].p_memsz == phdr_info[pi].p_filesz);
                    /* Although adjust_elf() does not remove program-header entries,
                       we perform this update here because I've seen object files
                       whose PHDR table is bigger by one element than it should be.
                       Here we check and correct the size, if necessary.
                    */
                    if (phdr_info[pi].p_memsz != ehdr->e_phentsize * ehdr->e_phnum) {
                        ASSERT(phdr_info[pi].p_memsz > ehdr->e_phentsize * ehdr->e_phnum);
                        INFO("WARNING: PT_PHDR file and memory sizes are incorrect (%ld instead of %ld).  Correcting.\n",
                             (long)phdr_info[pi].p_memsz,
                             (long)(ehdr->e_phentsize * ehdr->e_phnum));
                        phdr_info[pi].p_memsz = ehdr->e_phentsize * ehdr->e_phnum;
                        phdr_info[pi].p_filesz = phdr_info[pi].p_memsz;
                    }
                }
                else {

                    /*  Go over the section array and find which section's offset
                        field matches this program header's, and update the program
                        header's offset to reflect the new value.
                    */
                    Elf64_Off file_end, mem_end;
                    Elf64_Off new_phdr_offset =
                        section_to_header_mapping(elf, pi,
                                                  shdr_info, shdr_info_len,
                                                  &file_end,
                                                  &mem_end);

                    if (new_phdr_offset == (Elf64_Off)-1) {
                        INFO("PT_ header type: %d does not contain any sections.\n",
                               phdr_info[pi].p_type);
                        /* Move to the next program header. */
                        FAILIF_LIBELF(gelf_update_phdr (newelf, pi, &phdr_info[pi]) == 0,
                                      gelf_update_phdr);
                        continue;
                    }

                    /* Alignments of 0 and 1 mean nothing.  Higher alignments are
                       interpreted as powers of 2. */
                    if (phdr_info[pi].p_align > 1) {
                        INFO("\t\tapplying alignment of 0x%llx to new offset %lld\n",
                             phdr_info[pi].p_align,
                             new_phdr_offset);
                        new_phdr_offset &= ~(phdr_info[pi].p_align - 1);
                    }

                    Elf32_Sxword delta = new_phdr_offset - phdr_info[pi].p_offset;

                    INFO("\t\tnew offset %lld (was %lld)\n",
                         new_phdr_offset,
                         phdr_info[pi].p_offset);

                    phdr_info[pi].p_offset = new_phdr_offset;

                    INFO("\t\tnew vaddr 0x%llx (was 0x%llx)\n",
                         phdr_info[pi].p_vaddr + delta,
                         phdr_info[pi].p_vaddr);
                    phdr_info[pi].p_vaddr += delta;

                    INFO("\t\tnew paddr 0x%llx (was 0x%llx)\n",
                         phdr_info[pi].p_paddr + delta,
                         phdr_info[pi].p_paddr);
                    phdr_info[pi].p_paddr += delta;

                    INFO("\t\tnew mem size %lld (was %lld)\n",
                         mem_end - new_phdr_offset,
                         phdr_info[pi].p_memsz);
                    //phdr_info[pi].p_memsz = mem_end - new_phdr_offset;
                    phdr_info[pi].p_memsz = mem_end - phdr_info[pi].p_vaddr;

                    INFO("\t\tnew file size %lld (was %lld)\n",
                         file_end - new_phdr_offset,
                         phdr_info[pi].p_filesz);
                    //phdr_info[pi].p_filesz = file_end - new_phdr_offset;
                    phdr_info[pi].p_filesz = file_end - phdr_info[pi].p_offset;
                }

                FAILIF_LIBELF(gelf_update_phdr (newelf, pi, &phdr_info[pi]) == 0,
                              gelf_update_phdr);
            }
        }

        if (dynamic_idx >= 0) {
            /* NOTE: dynamic_idx is the index of .dynamic section in the shdr_info[] array, NOT the
               index of the section in the ELF file!
            */
            adjust_dynamic_segment_offsets(elf, ebl,
                                           newelf,
                                           dynamic_idx,
                                           shdr_info,
                                           shdr_info_len);
        }
        else INFO("There is no dynamic section in this file.\n");

        /* Walk the relocation sections (again).  This time, update offsets of the
           relocation entries.  Note that there is an implication here that the
           offsets are virual addresses, because we are handling a shared library!
        */
        for (cnt = 1; cnt < shdr_info_len; cnt++) {
            /* Note here that we process even those relocation sections that are
             * marked for removal.  Normally, we wouldn't need to do this, but
             * in the case where we run adjust_elf() after a dry run of
             * prelink() (see apriori), we still want to update the relocation
             * offsets because those will be picked up by the second run of
             * prelink(). If this all seems too cryptic, go yell at Iliyan
             * Malchev.
             */
            if (/* shdr_info[cnt].idx > 0 && */
                (shdr_info[cnt].shdr.sh_type == SHT_REL ||
                 shdr_info[cnt].shdr.sh_type == SHT_RELA))
            {
                int hacked = shdr_info[cnt].idx == 0;
                Elf_Data *data;
                if (hacked) {
                    /* This doesn't work!  elf_ndxscn(shdr_info[cnt].scn) will return the section number
                       of the new sectin that has moved into this slot. */
                    shdr_info[cnt].idx = elf_ndxscn(shdr_info[cnt].scn);
                    data = elf_getdata (elf_getscn (elf, shdr_info[cnt].idx), NULL);
                    INFO("PRELINKER HACK: Temporarily restoring index of to-be-removed section [%s] to %d.\n",
                         shdr_info[cnt].name,
                         shdr_info[cnt].idx);
                }
                else
                    data = elf_getdata (elf_getscn (newelf, shdr_info[cnt].idx), NULL);

                update_relocations_section_offsets(newelf, elf, ebl,
                                                   shdr_info, shdr_info_len,
                                                   shdr_info + cnt,
                                                   data,
                                                   old_section_ranges);
                if (hacked) {
                    INFO("PRELINKER HACK: Done with hack, marking section [%s] for removal again.\n",
                         shdr_info[cnt].name);
                    shdr_info[cnt].idx = 0;
                }
            }
        }
    }

    /* Finally finish the ELF header.  Fill in the fields not handled by
       libelf from the old file. */
    {
        GElf_Ehdr *newehdr, newehdr_mem;
        newehdr = gelf_getehdr (newelf, &newehdr_mem);
        FAILIF_LIBELF(newehdr == NULL, gelf_getehdr);

        INFO("Updating ELF header.\n");

        memcpy (newehdr->e_ident, ehdr->e_ident, EI_NIDENT);
        newehdr->e_type    = ehdr->e_type;
        newehdr->e_machine = ehdr->e_machine;
        newehdr->e_version = ehdr->e_version;
        newehdr->e_entry   = ehdr->e_entry;
        newehdr->e_flags   = ehdr->e_flags;
        newehdr->e_phoff   = ehdr->e_phoff;

        /* We need to position the section header table. */
        {
            const size_t offsize = gelf_fsize (elf, ELF_T_OFF, 1, EV_CURRENT);
            newehdr->e_shoff = get_last_address(section_ranges);
            newehdr->e_shoff += offsize - 1;
            newehdr->e_shoff &= ~((GElf_Off) (offsize - 1));
            newehdr->e_shentsize = gelf_fsize (elf, ELF_T_SHDR, 1, EV_CURRENT);
            INFO("\tsetting section-header-table offset to %lld\n",
                 newehdr->e_shoff);
        }

        if (rebuild_shstrtab) {
            /* If we are rebuilding the section-headers string table, then
               the new index must not be zero.  This is to guard against
               code breakage resulting from rebuild_shstrtab and shdridx
               somehow getting out of sync. */
            ASSERT(shdridx);
            /* The new section header string table index. */
            FAILIF(!(shdr_info[shdridx].idx < SHN_HIRESERVE) &&
                   likely (shdr_info[shdridx].idx != SHN_XINDEX),
                   "Can't handle extended section indices!\n");
        }

        INFO("Index of shstrtab is now %d (was %d).\n",
             shdr_info[shdridx].idx,
             ehdr->e_shstrndx);
        newehdr->e_shstrndx = shdr_info[shdridx].idx;

        FAILIF_LIBELF(gelf_update_ehdr(newelf, newehdr) == 0, gelf_update_ehdr);
    }
    if (section_ranges != NULL) destroy_range_list(section_ranges);
    destroy_range_list(old_section_ranges);

#ifdef DEBUG
    verify_elf (ehdr, shdr_info, shdr_info_len, phdr_info);
#endif

}

static void update_hash_table(Elf *newelf, Elf *elf,
                              Elf32_Word hash_scn_idx,
                              shdr_info_t *symtab_info) {
    GElf_Shdr shdr_mem, *shdr = NULL;
    Elf32_Word *chain;
    Elf32_Word nbucket;

    /* The hash table section and data in the new file. */
    Elf_Scn *hashscn = elf_getscn (newelf, hash_scn_idx);
    ASSERT(hashscn != NULL);
    Elf_Data *hashd = elf_getdata (hashscn, NULL);
    ASSERT (hashd != NULL);
    Elf32_Word *bucket = (Elf32_Word *) hashd->d_buf; /* Sane arches first. */

    /* The symbol table data. */
    Elf_Data *symd = elf_getdata (elf_getscn (newelf, symtab_info->idx), NULL);
    ASSERT (symd != NULL);

    GElf_Ehdr ehdr_mem;
    GElf_Ehdr *ehdr = gelf_getehdr (elf, &ehdr_mem);
    FAILIF_LIBELF(NULL == ehdr, gelf_getehdr);
    size_t strshndx = symtab_info->old_shdr.sh_link;
    size_t elsize = gelf_fsize (elf, ELF_T_SYM, 1,
                                ehdr->e_version);

    /* Convert to the correct byte order. */
    FAILIF_LIBELF(gelf_xlatetom (newelf, hashd, hashd,
                                 BYTE_ORDER == LITTLE_ENDIAN
                                 ? ELFDATA2LSB : ELFDATA2MSB) == NULL,
                  gelf_xlatetom);

    /* Adjust the nchain value.  The symbol table size changed.  We keep the
       same size for the bucket array. */
    INFO("hash table: buckets: %d (no change).\n", bucket[0]);
    INFO("hash table: chains: %d (was %d).\n",
         symd->d_size / elsize,
         bucket[1]);
    bucket[1] = symd->d_size / elsize;
    nbucket = bucket[0];
    bucket += 2;
    chain = bucket + nbucket;

    /* New size of the section. */
    shdr = gelf_getshdr (hashscn, &shdr_mem);
    ASSERT(shdr->sh_type == SHT_HASH);
    shdr->sh_size = (2 + symd->d_size / elsize + nbucket) * sizeof (Elf32_Word);
    INFO("hash table: size %lld (was %d) bytes.\n",
         shdr->sh_size,
         hashd->d_size);
    hashd->d_size = shdr->sh_size;
    (void)gelf_update_shdr (hashscn, shdr);

    /* Clear the arrays. */
    memset (bucket, '\0',
            (symd->d_size / elsize + nbucket)
            * sizeof (Elf32_Word));

    size_t inner;
    for (inner = symtab_info->shdr.sh_info;
        inner < symd->d_size / elsize;
        ++inner) {
        const char *name;
        GElf_Sym sym_mem;
        GElf_Sym *sym = gelf_getsym (symd, inner, &sym_mem);
        ASSERT (sym != NULL);

        name = elf_strptr (elf, strshndx, sym->st_name);
        ASSERT (name != NULL);
        size_t hidx = elf_hash (name) % nbucket;

        if (bucket[hidx] == 0)
            bucket[hidx] = inner;
        else {
            hidx = bucket[hidx];
            while (chain[hidx] != 0)
                hidx = chain[hidx];
            chain[hidx] = inner;
        }
    }

    /* Convert back to the file byte order. */
    FAILIF_LIBELF(gelf_xlatetof (newelf, hashd, hashd,
                                 BYTE_ORDER == LITTLE_ENDIAN
                                 ? ELFDATA2LSB : ELFDATA2MSB) == NULL,
                  gelf_xlatetof);
}

/* This function updates the symbol indices of relocation entries.  It does not
   update the section offsets of those entries.
*/
static void update_relocations_section_symbol_references(
    Elf *newelf, Elf *elf __attribute__((unused)),
    shdr_info_t *info,
    int info_len __attribute__((unused)),
    shdr_info_t *relsect_info,
    Elf32_Word *newsymidx)
{
    /* Get this relocation section's data */
    Elf_Data *d = elf_getdata (elf_getscn (newelf, relsect_info->idx), NULL);
    ASSERT (d != NULL);
    ASSERT (d->d_size == relsect_info->shdr.sh_size);

    size_t old_nrels =
        relsect_info->old_shdr.sh_size / relsect_info->old_shdr.sh_entsize;
    size_t new_nrels =
        relsect_info->shdr.sh_size / relsect_info->shdr.sh_entsize;

    size_t nrels = new_nrels;
    if (relsect_info->use_old_shdr_for_relocation_calculations) {
        nrels = old_nrels;
        /* Now, we update d->d_size to point to the old size in order to
           prevent gelf_update_rel() and gelf_update_rela() from returning
           an error.  We restore the value at the end of the function.
        */
        d->d_size = old_nrels * relsect_info->shdr.sh_entsize;
    }

    /* Now, walk the relocations one by one.  For each relocation,
       check to see whether the symbol it refers to has a new
       index in the symbol table, and if so--update it.  We know
       if a symbol's index has changed when we look up that
       the newsymidx[] array at the old index.  If the value at that
       location is different from the array index, then the
       symbol's index has changed; otherwise, it remained the same.
    */
    INFO("Scanning %d relocation entries in section [%s] (taken from %s section header (old %d, new %d))...\n",
         nrels,
         relsect_info->name,
         (relsect_info->use_old_shdr_for_relocation_calculations ? "old" : "new"),
         old_nrels, new_nrels);

    size_t relidx, newidx;
    if (relsect_info->shdr.sh_type == SHT_REL) {
        for (newidx = relidx = 0; relidx < nrels; ++relidx) {
            GElf_Rel rel_mem;
            FAILIF_LIBELF(gelf_getrel (d, relidx, &rel_mem) == NULL,
                          gelf_getrel);
            size_t symidx = GELF_R_SYM (rel_mem.r_info);
            if (newsymidx[symidx] != (Elf32_Word)-1)
            {
                rel_mem.r_info = GELF_R_INFO (newsymidx[symidx],
                                              GELF_R_TYPE (rel_mem.r_info));
                FAILIF_LIBELF(gelf_update_rel (d, newidx, &rel_mem) == 0,
                              gelf_update_rel);
                newidx++;
            }
            else {
                INFO("Discarding REL entry for symbol [%d], section [%d]\n",
                     symidx,
                     relsect_info->shdr.sh_info);
            }
        } /* for each rel entry... */
    } else {
        for (newidx = relidx = 0; relidx < nrels; ++relidx) {
            GElf_Rela rel_mem;
            FAILIF_LIBELF(gelf_getrela (d, relidx, &rel_mem) == NULL,
                          gelf_getrela);
            size_t symidx = GELF_R_SYM (rel_mem.r_info);
            if (newsymidx[symidx] != (Elf32_Word)-1)
            {
                rel_mem.r_info
                = GELF_R_INFO (newsymidx[symidx],
                               GELF_R_TYPE (rel_mem.r_info));

                FAILIF_LIBELF(gelf_update_rela (d, newidx, &rel_mem) == 0,
                              gelf_update_rela);
                newidx++;
            }
            else {
                INFO("Discarding RELA entry for symbol [%d], section [%d]\n",
                     symidx,
                     relsect_info->shdr.sh_info);
            }
        } /* for each rela entry... */
    } /* if rel else rela */

    if (newidx != relidx)
    {
        INFO("Shrinking relocation section from %lld to %lld bytes (%d -> %d "
             "entries).\n",
             relsect_info->shdr.sh_size,
             relsect_info->shdr.sh_entsize * newidx,
             relidx,
             newidx);

        d->d_size = relsect_info->shdr.sh_size =
            relsect_info->shdr.sh_entsize * newidx;
    } else INFO("Relocation section [%s]'s size (relocates: %s(%d), "
                "symab: %s(%d)) does not change.\n",
                relsect_info->name,
                info[relsect_info->shdr.sh_info].name,
                relsect_info->shdr.sh_info,
                info[relsect_info->shdr.sh_link].name,
                relsect_info->shdr.sh_link);

    /* Restore d->d_size if necessary. */
    if (relsect_info->use_old_shdr_for_relocation_calculations)
        d->d_size = new_nrels * relsect_info->shdr.sh_entsize;
}

static void update_relocations_section_offsets(Elf *newelf __attribute((unused)), Elf *elf,
                                               Ebl *ebl __attribute__((unused)),
                                               shdr_info_t *info,
                                               int info_len __attribute__((unused)),
                                               shdr_info_t *relsect_info,
                                               Elf_Data *d,
                                               range_list_t *old_section_ranges)
{
    /* Get this relocation section's data */
    ASSERT (d != NULL);
    if (d->d_size != relsect_info->shdr.sh_size) {
        /* This is not necessarily a fatal error.  In the case where we call adjust_elf() from apriori
           (the prelinker), we may call this function for a relocation section that is marked for
           removal.  We still want to process this relocation section because, even though it is marked
           for removal, its relocatin entries will be used by the prelinker to know what to prelink.
           Once the prelinker is done, it will call adjust_elf() one more time to actually eliminate the
           relocation section. */
        PRINT("WARNING: section size according to section [%s]'s header is %lld, but according to data buffer is %ld.\n",
              relsect_info->name,
              relsect_info->shdr.sh_size,
              d->d_size);
        ASSERT((relsect_info->shdr.sh_type == SHT_REL || relsect_info->shdr.sh_type == SHT_RELA) &&
               relsect_info->use_old_shdr_for_relocation_calculations);
    }

    size_t old_nrels =
        relsect_info->old_shdr.sh_size / relsect_info->old_shdr.sh_entsize;
    size_t new_nrels =
        relsect_info->shdr.sh_size / relsect_info->shdr.sh_entsize;

    size_t nrels = new_nrels;
    if (relsect_info->use_old_shdr_for_relocation_calculations) {
        nrels = old_nrels;
        /* Now, we update d->d_size to point to the old size in order to
           prevent gelf_update_rel() and gelf_update_rela() from returning
           an error.  We restore the value at the end of the function.
        */
        d->d_size = old_nrels * relsect_info->shdr.sh_entsize;
    }

    /* Now, walk the relocations one by one.  For each relocation,
       check to see whether the symbol it refers to has a new
       index in the symbol table, and if so--update it.  We know
       if a symbol's index has changed when we look up that
       the newsymidx[] array at the old index.  If the value at that
       location is different from the array index, then the
       symbol's index has changed; otherwise, it remained the same.
    */
    INFO("Scanning %d relocation entries in section [%s] (taken from %s section header (old %d, new %d))...\n",
         nrels,
         relsect_info->name,
         (relsect_info->use_old_shdr_for_relocation_calculations ? "old" : "new"),
         old_nrels, new_nrels);

    if (relsect_info->old_shdr.sh_info == 0) {
        PRINT("WARNING: Relocation section [%s] relocates the NULL section.\n",
              relsect_info->name);
    }
    else {
        FAILIF(info[relsect_info->old_shdr.sh_info].idx == 0,
               "Section [%s] relocates section [%s] (index %d), which is being "
               "removed!\n",
               relsect_info->name,
               info[relsect_info->old_shdr.sh_info].name,
               relsect_info->old_shdr.sh_info);
    }

    size_t relidx;
    FAILIF(relsect_info->shdr.sh_type == SHT_RELA,
           "Can't handle SHT_RELA relocation entries.\n");

    if (relsect_info->shdr.sh_type == SHT_REL) {
        for (relidx = 0; relidx < nrels; ++relidx) {
            GElf_Rel rel_mem;
            FAILIF_LIBELF(gelf_getrel (d, relidx, &rel_mem) == NULL,
                          gelf_getrel);

            if (GELF_R_TYPE(rel_mem.r_info) == R_ARM_NONE)
                continue;

            range_t *old_range = find_range(old_section_ranges,
                                            rel_mem.r_offset);
#if 1
            if (NULL == old_range) {
                GElf_Sym *sym, sym_mem;
                unsigned sym_idx = GELF_R_SYM(rel_mem.r_info);
                /* relsect_info->shdr.sh_link is the index of the associated
                   symbol table. */
                sym = gelf_getsymshndx(info[relsect_info->shdr.sh_link].data,
                                       NULL,
                                       sym_idx,
                                       &sym_mem,
                                       NULL);
                /* info[relsect_info->shdr.sh_link].shdr.sh_link is the index
                   of the string table associated with the symbol table
                   associated with the relocation section rel_sect. */
                const char *symname = elf_strptr(elf,
                                                 info[relsect_info->shdr.sh_link].shdr.sh_link,
                                                 sym->st_name);

                {
                    int i = 0;
                    INFO("ABOUT TO FAIL for symbol [%s]: old section ranges:\n", symname);

                    int num_ranges;
                    range_t *ranges = get_sorted_ranges(old_section_ranges, &num_ranges);

                    for (; i < num_ranges; i++) {
                        shdr_info_t *inf = (shdr_info_t *)ranges[i].user;
                        INFO("\t[%8lld, %8lld] (%8lld bytes) [%8lld, %8lld] (%8lld bytes) [%-15s]\n",
                             ranges[i].start,
                             ranges[i].start + ranges[i].length,
                             ranges[i].length,
                             inf->old_shdr.sh_addr,
                             inf->old_shdr.sh_addr + inf->old_shdr.sh_size,
                             inf->old_shdr.sh_size,
                             inf->name);
                    }
                    INFO("\n");
                }

                FAILIF(1,
                       "No range matches relocation entry value 0x%llx (%d) [%s]!\n",
                       rel_mem.r_offset,
                       rel_mem.r_offset,
                       symname);
            }
#else
            FAILIF(NULL == old_range,
                   "No range matches relocation entry value 0x%llx!\n",
                   rel_mem.r_offset);
#endif
            ASSERT(old_range->start <= rel_mem.r_offset &&
                   rel_mem.r_offset < old_range->start + old_range->length);
            ASSERT(old_range->user);
            shdr_info_t *old_range_info = (shdr_info_t *)old_range->user;
            ASSERT(old_range_info->idx > 0);
            if (relsect_info->old_shdr.sh_info &&
                old_range_info->idx != relsect_info->old_shdr.sh_info) {
                PRINT("Relocation offset 0x%llx does not match section [%s] "
                      "but section [%s]!\n",
                      rel_mem.r_offset,
                      info[relsect_info->old_shdr.sh_info].name,
                      old_range_info->name);
            }

#if 0 /* This is true only for shared libraries, but not for executables */
            ASSERT(old_range_info->shdr.sh_addr == old_range_info->shdr.sh_offset);
            ASSERT(old_range_info->old_shdr.sh_addr == old_range_info->old_shdr.sh_offset);
#endif
            Elf64_Sxword delta =
                old_range_info->shdr.sh_addr - old_range_info->old_shdr.sh_addr;

            if (delta) {
                extern int verbose_flag;
                /* Print out some info about the relocation entry we are
                   modifying. */
                if (unlikely(verbose_flag)) {
                    /* Get associated (new) symbol table. */
                    Elf64_Word symtab = relsect_info->shdr.sh_link;
                    /* Get the symbol that is being relocated. */
                    size_t symidx = GELF_R_SYM (rel_mem.r_info);
                    GElf_Sym sym_mem, *sym;
                    /* Since by now we've already updated the symbol index,
                       we need to retrieve the symbol from the new symbol table.
                    */
                    sym = gelf_getsymshndx (elf_getdata(info[symtab].newscn, NULL),
                                            NULL,
                                            symidx, &sym_mem, NULL);
                    FAILIF_LIBELF(NULL == sym, gelf_getsymshndx);
                    char buf[64];
                    INFO("\t%02d (%-15s) off 0x%llx -> 0x%llx (%lld) (relocates [%s:(%d)%s])\n",
                         (unsigned)GELF_R_TYPE(rel_mem.r_info),
                         ebl_reloc_type_name(ebl,
                                             GELF_R_TYPE(rel_mem.r_info),
                                             buf,
                                             sizeof(buf)),
                         rel_mem.r_offset, rel_mem.r_offset + delta, delta,
                         old_range_info->name,
                         symidx,
#if ELF_STRPTR_IS_BROKEN
                         /* libelf does not keep track of changes very well.
                            Looks like, if you use elf_strptr() on a file that
                            has not been updated yet, you get bogus results. */
                         ((char *)info[info[symtab].old_shdr.sh_link].
                          newdata->d_buf) + sym->st_name
#else
                         elf_strptr(newelf,
                                    info[symtab].shdr.sh_link,
                                    sym->st_name)
#endif
                         );
                } /* if (verbose_flag) */

                rel_mem.r_offset += delta;
                FAILIF_LIBELF(gelf_update_rel (d, relidx, &rel_mem) == 0,
                              gelf_update_rel);

#ifdef ARM_SPECIFIC_HACKS
                if (GELF_R_TYPE(rel_mem.r_info) == R_ARM_RELATIVE) {
                    FAILIF(GELF_R_SYM(rel_mem.r_info) != 0,
                           "Can't handle relocation!\n");
                    /* From the ARM documentation: "when the symbol is zero,
                       the R_ARM_RELATIVE entry resolves to the difference
                       between the address at which the segment being
                       relocated was loaded and the address at which it
                       was linked."
                    */

                    int *ptr =
                        (int *)(((char *)old_range_info->newdata->d_buf) +
                                (rel_mem.r_offset -
                                 old_range_info->shdr.sh_addr));
                    *ptr += (int)delta;

                }
#endif
            } /* if (delta) */
        } /* for each rel entry... */
    }

    /* Restore d->d_size if necessary. */
    if (relsect_info->use_old_shdr_for_relocation_calculations)
        d->d_size = new_nrels * relsect_info->shdr.sh_entsize;
}

static inline
Elf_Data *create_section_data(shdr_info_t *info, Elf_Scn *scn)
{
    Elf_Data *newdata = NULL;

    if (info->data == NULL) {
        info->data = elf_getdata (info->scn, NULL);
        FAILIF_LIBELF(NULL == info->data, elf_getdata);
        INFO("\t\tcopying data from original section (%d bytes).\n",
             info->data->d_size);
        /* Set the data.  This is done by copying from the old file. */
        newdata = elf_newdata (scn);
        FAILIF_LIBELF(newdata == NULL, elf_newdata);
        /* Copy the structure.  Note that the data buffer pointer gets
           copied, but the buffer itself does not. */
        *newdata = *info->data;
#if COPY_SECTION_DATA_BUFFER
        if (info->data->d_buf != NULL) {
            newdata->d_buf = MALLOC(newdata->d_size);
            memcpy(newdata->d_buf, info->data->d_buf, newdata->d_size);
        }
#endif
    } else {
        INFO("\t\tassigning new data to section (%d bytes).\n",
             info->data->d_size);
        newdata = info->data;
    }

    info->newdata = newdata;
    return newdata;
}

#if 0
static void print_shdr_array(shdr_info_t *info, int num_entries) {
    extern int verbose_flag;
    if (verbose_flag) {
        int i;
        for (i = 0; i < num_entries; i++) {
            INFO("%03d:"
                 "\tname [%s]\n"
                 "\tidx  [%d]\n",
                 i, info[i].name, info[i].idx);
        }
    } /* if (verbose_flag) */
}
#endif

static size_t do_update_dyn_entry_address(Elf *elf,
                                          GElf_Dyn *dyn,
                                          shdr_info_t *shdr_info,
                                          int shdr_info_len,
                                          int newline)
{
    size_t scnidx = 0;
    INFO("%#0*llx",
         gelf_getclass (elf) == ELFCLASS32 ? 10 : 18,
         dyn->d_un.d_val);
    for (scnidx = 1; scnidx < shdr_info_len; scnidx++) {
        if (shdr_info[scnidx].old_shdr.sh_addr == dyn->d_un.d_ptr) {
            if (shdr_info[scnidx].idx > 0) {
                INFO(" (updating to 0x%08llx per section %d (shdr_info[] index %d): [%s])",
                     shdr_info[scnidx].shdr.sh_addr,
                     shdr_info[scnidx].idx,
                     scnidx,
                     shdr_info[scnidx].name);
                dyn->d_un.d_ptr = shdr_info[scnidx].shdr.sh_addr;
                break;
            }
            else {
                /* FIXME:  This should be more intelligent.  What if there is more than one section that fits the
                           dynamic entry, and just the first such is being removed?  We should keep on searching here.
                */
                INFO(" (Setting to ZERO per section (shdr_info[] index %d) [%s], which is being removed)",
                     scnidx,
                     shdr_info[scnidx].name);
                dyn->d_un.d_ptr = 0;
                break;
            }
        }
    }
    if (newline) INFO("\n");
    return scnidx == shdr_info_len ? 0 : scnidx;
}

static inline size_t update_dyn_entry_address(Elf *elf,
                                              GElf_Dyn *dyn,
                                              shdr_info_t *shdr_info,
                                              int shdr_info_len)
{
    return do_update_dyn_entry_address(elf, dyn, shdr_info, shdr_info_len, 1);
}

static void update_dyn_entry_address_and_size(Elf *elf, Ebl *oldebl,
                                              GElf_Dyn *dyn,
                                              shdr_info_t *shdr_info,
                                              int shdr_info_len,
                                              Elf_Data *dyn_data,
                                              size_t *dyn_size_entries,
                                              int dyn_entry_idx)
{
    size_t scnidx = do_update_dyn_entry_address(elf, dyn,
                                                shdr_info, shdr_info_len,
                                                0);
    if (scnidx) {
        char buf[64];
        INFO(" (affects tag %s)",
             ebl_dynamic_tag_name(oldebl, dyn_entry_idx,
                                  buf, sizeof (buf)));
        if (dyn_size_entries[dyn_entry_idx]) {
            /* We previously encountered this size entry, and because
               we did not know which section would affect it, we saved its
               index in the dyn_size_entries[] array so that we can update
               the entry when we do know.  Now we know that the field
               shdr_info[scnidx].shdr.sh_size contains that new value.
            */
            GElf_Dyn *szdyn, szdyn_mem;

            szdyn = gelf_getdyn (dyn_data,
                                 dyn_size_entries[dyn_entry_idx],
                                 &szdyn_mem);
            FAILIF_LIBELF(NULL == szdyn, gelf_getdyn);
            ASSERT(szdyn->d_tag == dyn_entry_idx);

            INFO("\n (!)\t%-17s completing deferred update (%lld -> %lld bytes)"
                 " per section %d [%s]",
                 ebl_dynamic_tag_name (oldebl, szdyn->d_tag,
                                       buf, sizeof (buf)),
                 szdyn->d_un.d_val,
                 shdr_info[scnidx].shdr.sh_size,
                 shdr_info[scnidx].idx,
                 shdr_info[scnidx].name);

            szdyn->d_un.d_val = shdr_info[scnidx].shdr.sh_size;
            FAILIF_LIBELF(0 == gelf_update_dyn(dyn_data,
                                               dyn_size_entries[dyn_entry_idx],
                                               szdyn),
                          gelf_update_dyn);
#ifdef DEBUG
            dyn_size_entries[dyn_entry_idx] = -1;
#endif
        }
        else dyn_size_entries[dyn_entry_idx] = scnidx;
    } /* if (scnidx) */

    INFO("\n");
}

static void do_build_dynamic_segment_strings(Elf *elf, Ebl *oldebl,
                                             int dynidx, /* index of .dynamic section */
                                             int symtabidx, /* index of symbol table section */
                                             shdr_info_t *shdr_info,
                                             int shdr_info_len __attribute__((unused)),
                                             bool print_strings_only)
{
    Elf_Scn *dynscn = elf_getscn(elf, dynidx);
    FAILIF_LIBELF(NULL == dynscn, elf_getscn);
    Elf_Data *data = elf_getdata (dynscn, NULL);
    ASSERT(data != NULL);

    size_t cnt;

    if (!print_strings_only) {
      /* Allocate an array of string-offset structures. */
      shdr_info[dynidx].symse =
        (struct Ebl_Strent **)CALLOC(
                                     shdr_info[dynidx].shdr.sh_size/shdr_info[dynidx].shdr.sh_entsize,
                                     sizeof(struct Ebl_Strent *));
    }

    for (cnt = 0;
         cnt < shdr_info[dynidx].shdr.sh_size/shdr_info[dynidx].shdr.sh_entsize;
         ++cnt)
    {
        char buf[64];
        GElf_Dyn dynmem;
        GElf_Dyn *dyn;

        dyn = gelf_getdyn (data, cnt, &dynmem);
        FAILIF_LIBELF(NULL == dyn, gelf_getdyn);

        switch (dyn->d_tag) {
        case DT_NEEDED:
        case DT_SONAME:
        case DT_RPATH:
        case DT_RUNPATH:
            {
                const char *str =
                    elf_strptr (elf,
                                 shdr_info[dynidx].shdr.sh_link,
                                 dyn->d_un.d_val);
                ASSERT(str != NULL);
                INFO("\t\t\t%-17s: ",
                     ebl_dynamic_tag_name (oldebl,
                                           dyn->d_tag,
                                           buf, sizeof (buf)));
                INFO("[%s] (offset %ld)\n", str, dyn->d_un.d_val);
                if (!print_strings_only) {
                    /* We append the strings to the string table belonging to the
                       dynamic-symbol-table section.  We keep the dynsymst handle
                       for the strings section in the shdr_info[] entry for the
                       dynamic-sybmol table.  Confusing, I know.
                    */
                    ASSERT(shdr_info[symtabidx].dynsymst);
                    /* The string tables for the symbol table and the .dynamic
                       section must be the same.
                    */
                    ASSERT(shdr_info[symtabidx].shdr.sh_link ==
                           shdr_info[dynidx].shdr.sh_link);
                    shdr_info[dynidx].symse[cnt] =
                      ebl_strtabadd(shdr_info[symtabidx].dynsymst, str?:"", 0);
                    ASSERT(shdr_info[dynidx].symse[cnt] != NULL);
                }
            }
            break;
        default:
            break;
        }
    } /* for (...) */
} /* build_dynamic_segment_strings() */

static void build_dynamic_segment_strings(Elf *elf, Ebl *oldebl,
                                          int dynidx, /* index of .dynamic section */
                                          int symtabidx, /* index of symbol table section */
                                          shdr_info_t *shdr_info,
                                          int shdr_info_len __attribute__((unused)))
{
    INFO("\t\tbuilding string offsets for dynamic section [%s], index %d\n",
         shdr_info[dynidx].name,
         dynidx);
    do_build_dynamic_segment_strings(elf, oldebl, dynidx, symtabidx,
                                     shdr_info, shdr_info_len, false);
}

#ifdef DEBUG
static void print_dynamic_segment_strings(Elf *elf, Ebl *oldebl,
                                          int dynidx, /* index of .dynamic section */
                                          int symtabidx, /* index of symbol table section */
                                          shdr_info_t *shdr_info,
                                          int shdr_info_len __attribute__((unused)))
{
    INFO("\t\tprinting string offsets for dynamic section [%s], index %d\n",
         shdr_info[dynidx].name,
         dynidx);
    do_build_dynamic_segment_strings(elf, oldebl, dynidx, symtabidx,
                                     shdr_info, shdr_info_len, true);
}
#endif

static void adjust_dynamic_segment_offsets(Elf *elf, Ebl *oldebl,
                                           Elf *newelf __attribute__((unused)),
                                           int dynidx, /* index of .dynamic section in shdr_info[] */
                                           shdr_info_t *shdr_info,
                                           int shdr_info_len)
{
    Elf_Scn *scn = shdr_info[dynidx].newscn;
    FAILIF_LIBELF(NULL == scn, elf_getscn);
    Elf_Data *data = elf_getdata (scn, NULL);
    ASSERT(data != NULL);

    size_t cnt;
    INFO("Updating dynamic section [%s], index %d\n",
         shdr_info[dynidx].name,
         dynidx);

    size_t *dyn_size_entries = (size_t *)CALLOC(DT_NUM, sizeof(size_t));

    ASSERT(data->d_type == ELF_T_DYN);

    for (cnt = 0; cnt < shdr_info[dynidx].shdr.sh_size / shdr_info[dynidx].shdr.sh_entsize; ++cnt) {
        char buf[64];
        GElf_Dyn dynmem;
        GElf_Dyn *dyn;

        dyn = gelf_getdyn (data, cnt, &dynmem);
        FAILIF_LIBELF(NULL == dyn, gelf_getdyn);

        INFO("\t%-17s ",
             ebl_dynamic_tag_name (oldebl, dyn->d_tag, buf, sizeof (buf)));

        switch (dyn->d_tag) {
        /* Updates to addresses */

        /* We assume that the address entries come before the size entries.
        */

        case DT_PLTGOT:
        case DT_HASH:
        case DT_SYMTAB:
            (void)update_dyn_entry_address(elf, dyn, shdr_info, shdr_info_len);
            break;
        case DT_STRTAB:
            /* Defer-update DT_STRSZ as well, if not already updated. */
            update_dyn_entry_address_and_size(elf, oldebl, dyn,
                                              shdr_info, shdr_info_len,
                                              data,
                                              dyn_size_entries,
                                              DT_STRSZ);
            break;
        case DT_RELA:
            /* Defer-update DT_RELASZ as well, if not already updated. */
            update_dyn_entry_address_and_size(elf, oldebl, dyn,
                                              shdr_info, shdr_info_len,
                                              data,
                                              dyn_size_entries,
                                              DT_RELASZ);
            break;
        case DT_REL:
            /* Defer-update DT_RELSZ as well, if not already updated. */
            update_dyn_entry_address_and_size(elf, oldebl, dyn,
                                              shdr_info, shdr_info_len,
                                              data,
                                              dyn_size_entries,
                                              DT_RELSZ);
            break;
        case DT_JMPREL:
            /* Defer-update DT_PLTRELSZ as well, if not already updated. */
            update_dyn_entry_address_and_size(elf, oldebl, dyn,
                                              shdr_info, shdr_info_len,
                                              data,
                                              dyn_size_entries,
                                              DT_PLTRELSZ);
            break;
        case DT_INIT_ARRAY:
        case DT_FINI_ARRAY:
        case DT_PREINIT_ARRAY:
        case DT_INIT:
        case DT_FINI:
             (void)update_dyn_entry_address(elf, dyn, shdr_info, shdr_info_len);
             break;

        /* Updates to sizes */
        case DT_PLTRELSZ: /* DT_JMPREL or DT_PLTGOT */
        case DT_STRSZ:    /* DT_STRTAB */
        case DT_RELSZ:    /* DT_REL */
        case DT_RELASZ:   /* DR_RELA */
            if (dyn_size_entries[dyn->d_tag] == 0) {
                /* We have not yet found the new size for this entry, so we
                   save the index of the dynamic entry in the dyn_size_entries[]
                   array.  When we find the section affecting this field (in
                   code above), we will update the entry.
                */
                INFO("(!) (deferring update: new value not known yet)\n");
                dyn_size_entries[dyn->d_tag] = cnt;
            }
            else {
                ASSERT(dyn_size_entries[dyn->d_tag] < shdr_info_len);
                INFO("%lld (bytes) (updating to %lld bytes "
                     "per section %d [%s])\n",
                     dyn->d_un.d_val,
                     shdr_info[dyn_size_entries[dyn->d_tag]].shdr.sh_size,
                     shdr_info[dyn_size_entries[dyn->d_tag]].idx,
                     shdr_info[dyn_size_entries[dyn->d_tag]].name);
                dyn->d_un.d_val =
                    shdr_info[dyn_size_entries[dyn->d_tag]].shdr.sh_size;
#ifdef DEBUG
                /* Clear the array so that we know we are done with it. */
                dyn_size_entries[dyn->d_tag] = (size_t)-1;
#endif
            }
            break;
        /* End of updates. */

        case DT_NULL:
        case DT_DEBUG:
        case DT_BIND_NOW:
        case DT_TEXTREL:
            /* No further output.  */
            INFO("\n");
            break;

            /* String-entry updates. */
        case DT_NEEDED:
        case DT_SONAME:
        case DT_RPATH:
        case DT_RUNPATH:
            if (shdr_info[dynidx].symse != NULL)
            {
                Elf64_Xword new_offset =
                    ebl_strtaboffset(shdr_info[dynidx].symse[cnt]);
                INFO("string [%s] offset changes: %lld -> %lld\n",
                     elf_strptr (elf,
                                 shdr_info[dynidx].shdr.sh_link,
                                 dyn->d_un.d_val),
                     dyn->d_un.d_val,
                     new_offset);
                dyn->d_un.d_val = new_offset;
                FAILIF_LIBELF(0 == gelf_update_dyn(data, cnt, dyn),
                              gelf_update_dyn);
            }
            else
                INFO("string [%s] offset has not changed from %lld, not updating\n",
                     elf_strptr (elf,
                                 shdr_info[dynidx].shdr.sh_link,
                                 dyn->d_un.d_val),
                     dyn->d_un.d_val);
            break;

        case DT_RELAENT:
        case DT_SYMENT:
        case DT_RELENT:
        case DT_PLTPADSZ:
        case DT_MOVEENT:
        case DT_MOVESZ:
        case DT_INIT_ARRAYSZ:
        case DT_FINI_ARRAYSZ:
        case DT_SYMINSZ:
        case DT_SYMINENT:
        case DT_GNU_CONFLICTSZ:
        case DT_GNU_LIBLISTSZ:
            INFO("%lld (bytes)\n", dyn->d_un.d_val);
            break;

        case DT_VERDEFNUM:
        case DT_VERNEEDNUM:
        case DT_RELACOUNT:
        case DT_RELCOUNT:
            INFO("%lld\n", dyn->d_un.d_val);
            break;

        case DT_PLTREL: /* Specifies whether PLTREL (same as JMPREL) has REL or RELA entries */
            INFO("%s (%d)\n", ebl_dynamic_tag_name (oldebl, dyn->d_un.d_val, NULL, 0), dyn->d_un.d_val);
            break;

        default:
            INFO("%#0*llx\n",
                 gelf_getclass (elf) == ELFCLASS32 ? 10 : 18,
                 dyn->d_un.d_val);
            break;
        }

        FAILIF_LIBELF(0 == gelf_update_dyn(data, cnt, dyn),
                      gelf_update_dyn);
    } /* for (...) */

#ifdef DEBUG
    if (1) {
        int i;
        for (i = 0; i < DT_NUM; i++)
            ASSERT((ssize_t)dyn_size_entries[i] <= 0);
    }
#endif

    FREE(dyn_size_entries);
} /* adjust_dynamic_segment_offsets() */

static bool section_belongs_to_header(GElf_Shdr *shdr, GElf_Phdr *phdr)
{
    if (shdr->sh_size) {
       /* Compare allocated sections by VMA, unallocated
          sections by file offset.  */
        if(shdr->sh_flags & SHF_ALLOC) {
            if(shdr->sh_addr >= phdr->p_vaddr
               && (shdr->sh_addr + shdr->sh_size
                   <= phdr->p_vaddr + phdr->p_memsz))
            {
                return true;
            }
        }
        else {
            if (shdr->sh_offset >= phdr->p_offset
                && (shdr->sh_offset + shdr->sh_size
                    <= phdr->p_offset + phdr->p_filesz))
            {
                return true;
            }
        }
    }

    return false;
}

static Elf64_Off section_to_header_mapping(Elf *elf,
                                           int phdr_idx,
                                           shdr_info_t *shdr_info,
                                           int num_shdr_info,
                                           Elf64_Off *file_end,
                                           Elf64_Off *mem_end)
{
    Elf64_Off start;
    GElf_Phdr phdr_mem;
    GElf_Phdr *phdr = gelf_getphdr (elf, phdr_idx, &phdr_mem);
    FAILIF_LIBELF(NULL == phdr, gelf_getphdr);
    size_t inner;

    FAILIF(phdr->p_type == PT_GNU_RELRO,
           "Can't handle segments of type PT_GNU_RELRO!\n");

    /* Iterate over the sections.  */
    start = (Elf64_Off)-1;
    *file_end = *mem_end = 0;
    INFO("\n\t\t");
    for (inner = 1; inner < num_shdr_info; ++inner)
    {
        if (shdr_info[inner].idx > 0) {
            /* Check to see the section is in the segment.  We use the old
               header because that header contains the old offset and length
               information about a section.
            */
            if (section_belongs_to_header(&shdr_info[inner].old_shdr, phdr))
            {
                INFO("%-17s", shdr_info[inner].name);
#define SECT_MEM_END(s) ((s).sh_addr + (s).sh_size)
                if ((shdr_info[inner].shdr.sh_flags & SHF_ALLOC)) {
                    if (SECT_MEM_END(shdr_info[inner].shdr) > *mem_end) {
                        INFO("(mem_end 0x%llx --> 0x%llx) ", *mem_end, SECT_MEM_END(shdr_info[inner].shdr));
                        *mem_end = SECT_MEM_END(shdr_info[inner].shdr);
                    }
#undef SECT_MEM_END
#define SECT_FILE_END(s) ((s).sh_offset + (s).sh_size)
                    if (shdr_info[inner].shdr.sh_type != SHT_NOBITS) {
                        if (SECT_FILE_END(shdr_info[inner].shdr) > *file_end) {
                            INFO("(file_end 0x%llx --> 0x%llx) ", *file_end, SECT_FILE_END(shdr_info[inner].shdr));
                            *file_end = SECT_FILE_END(shdr_info[inner].shdr);
                        }
                    }
#undef SECT_FILE_END
                    if (shdr_info[inner].shdr.sh_offset < start) {
                        start = shdr_info[inner].shdr.sh_offset;
                    }
                } /* if section takes space */
                INFO("\n\t\t");
            }
            else
              INFO("(!) %-17s does not belong\n\t\t", shdr_info[inner].name);
        }
        else
          INFO("(!) %-17s is not considered, it is being removed\n\t\t", shdr_info[inner].name);
    }

    /* Finish the line.  */
    INFO("start: %lld\n", start);
    INFO("\t\tends: %lld file, %lld mem\n", *file_end, *mem_end);

    return start;
}

static void
update_symbol_values(Elf *elf, GElf_Ehdr *ehdr,
                     Elf *newelf __attribute__((unused)),
                     shdr_info_t *shdr_info,
                     int num_shdr_info,
                     int shady,
                     int dynamic_idx)
{
    /* Scan the sections, looking for the symbol table. */
    size_t i;
    for (i = 1; i < num_shdr_info; i++) {
        if (shdr_info[i].idx > 0 &&
            (shdr_info[i].shdr.sh_type == SHT_SYMTAB ||
             shdr_info[i].shdr.sh_type == SHT_DYNSYM))
        {
            size_t inner;
            size_t elsize = gelf_fsize (elf, ELF_T_SYM, 1, ehdr->e_version);
            Elf_Data *symdata = shdr_info[i].newdata;
            /* shdr_info[i].old_shdr.sh_link is the index of the strings table
               in the old ELF file.  This index still points to the same section
               in the shdr_info[] array.  The idx field of that entry is that
               section's new index.  That index must, therefore, be equal to
               the new value of sh_link. */
            ASSERT(shdr_info[shdr_info[i].old_shdr.sh_link].idx ==
                   shdr_info[i].shdr.sh_link);
            ASSERT(shdr_info[shdr_info[i].old_shdr.sh_link].data);

            INFO("\tupdating symbol values for section [%s]...\n",
                 shdr_info[i].name);

#if 1 /* DEBUG */
            {
                Elf_Scn *symstrscn = elf_getscn(newelf,  shdr_info[i].shdr.sh_link);
                ASSERT(symstrscn);
                Elf_Data *symstrdata = elf_getdata(symstrscn, NULL);
                ASSERT(symstrdata);
                INFO("%d nonprintable\n",
                     dump_hex_buffer(stdout, symstrdata->d_buf, symstrdata->d_size, 0));
            }
#endif

            INFO("\tnumber of symbols to update: %d (%d bytes)\n",
                 symdata->d_size / elsize, symdata->d_size);
            for (inner = 0; inner < symdata->d_size / elsize; ++inner)
            {
                GElf_Sym sym_mem;
                GElf_Sym *sym;
                size_t shnum;
                FAILIF_LIBELF(elf_getshnum (elf, &shnum) < 0, elf_getshnum);

                sym = gelf_getsymshndx (symdata, NULL,
                                        inner, &sym_mem, NULL);
                FAILIF_LIBELF(sym == NULL, gelf_getsymshndx);

#if 0 /* DEBUG */
                if (shdr_info[i].shdr.sh_type == SHT_SYMTAB) {
                    PRINT("%8d: name %d info %02x other %02x shndx %d size %lld value %lld\n",
                          inner,
                          sym->st_info,
                          sym->st_name,
                          sym->st_other,
                          sym->st_shndx,
                          sym->st_size,
                          sym->st_value);
                }
#endif

                size_t scnidx = sym->st_shndx;
                FAILIF(scnidx == SHN_XINDEX,
                       "Can't handle SHN_XINDEX!\n");

                char *symname = NULL;
                {
#if ELF_STRPTR_IS_BROKEN
                    Elf_Scn *symstrscn = elf_getscn(newelf,  shdr_info[i].shdr.sh_link);
                    ASSERT(symstrscn);
                    Elf_Data *symstrdata = elf_getdata(symstrscn, NULL);
                    ASSERT(symstrdata);
                    symname = symstrdata->d_buf + sym->st_name;
#else
                    symname = elf_strptr(newelf,
                                         shdr_info[i].shdr.sh_link,
                                         sym->st_name);
#endif
                }

                extern int verbose_flag;
                if (unlikely(verbose_flag))
                {
                    int c, max = 40;
                    INFO("%-8d [", inner);
                    for (c=0; c<max-1; c++) {
                        if (symname[c]) {
                            INFO("%c", symname[c]);
                        }
                        else break;
                    }
                    if (c < max-1) {
                        while (c++ < max) INFO(" ");
                    }
                    else INFO("<");
                    INFO("]");
                } /* if (unlikely(verbose_flag)) */

                /* Notice that shdr_info[] is an array whose indices correspond
                   to the section indices in the original ELF file.  Of those
                   sections, some have been discarded, and one is moved to the
                   end of the file--this is section .shstrtab.  Of course, no
                   symbol refers to this section, so it is safe for us to
                   address sections by their original indices in the
                   shdr_info[] array directly.
                */

                /* Note that we do not skip over the STT_SECTION symbols. Since
                   they contain the addresses of sections, we update their
                   values as well.
                */
                if (scnidx == SHN_UNDEF) {
                    INFO("   undefined\n");
                    continue;
                }
                if (scnidx >= shnum ||
                    (scnidx >= SHN_LORESERVE &&
                     scnidx <= SHN_HIRESERVE))
                {
                    INFO("   special (scn %d, value 0x%llx, size %lld)\n",
                         scnidx,
                         sym->st_value,
                         sym->st_size);

                    /* We shouldn't be messing with these symbols, but they are
                       often absolute symbols that encode the starting address
                       or the ending address of some section.  As a heuristic,
                       we will check to see if the value of the symbol matches
                       the start or the end of any section, and if so, we will
                       update it, but only if --shady is enabled.
                    */

                    if (shady && sym->st_value) {
                        size_t scnidx;
                        /* Is it the special symbol _DYNAMIC? */
                        if (!strcmp(symname, "_DYNAMIC")) {
                            /* The _DYNAMIC symbol points to the DYNAMIC
                               segment.  It is used by linker to bootstrap
                               itself. */
                            ASSERT(dynamic_idx >= 0);
                            PRINT("*** SHADY *** symbol %s: "
                                  "new st_value = %lld (was %lld), "
                                  "st_size = %lld (was %lld)\n",
                                  symname,
                                  shdr_info[dynamic_idx].shdr.sh_addr,
                                  sym->st_value,
                                  shdr_info[dynamic_idx].shdr.sh_size,
                                  sym->st_size);
                            sym->st_value =
                                shdr_info[dynamic_idx].shdr.sh_addr;
                            sym->st_size  =
                                shdr_info[dynamic_idx].shdr.sh_size;
                            /* NOTE: We don't update st_shndx, because this is a special
                                     symbol.  I am not sure if it's necessary though.
                            */
                            FAILIF_LIBELF(gelf_update_symshndx(symdata,
                                                               NULL,
                                                               inner,
                                                               sym,
                                                               0) == 0,
                                          gelf_update_symshndx);
                        }
                        else {
                            for (scnidx = 1; scnidx < num_shdr_info; scnidx++) {
                                if (sym->st_value ==
                                    shdr_info[scnidx].old_shdr.sh_addr) {
                                    if (shdr_info[scnidx].shdr.sh_addr !=
                                        sym->st_value) {
                                        PRINT("*** SHADY *** symbol %s matches old "
                                              "start %lld of section %s, updating "
                                              "to %lld.\n",
                                              symname,
                                              shdr_info[scnidx].old_shdr.sh_addr,
                                              shdr_info[scnidx].name,
                                              shdr_info[scnidx].shdr.sh_addr);
                                        sym->st_value = shdr_info[scnidx].shdr.sh_addr;
                                    }
                                    break;
                                }
                                else {
                                    Elf64_Addr oldaddr =
                                        shdr_info[scnidx].old_shdr.sh_addr +
                                        shdr_info[scnidx].old_shdr.sh_size;
                                    if (sym->st_value == oldaddr) {
                                        Elf64_Addr newaddr =
                                            shdr_info[scnidx].shdr.sh_addr +
                                            shdr_info[scnidx].shdr.sh_size;
                                        if (newaddr != sym->st_value) {
                                            PRINT("*** SHADY *** symbol %s matches old "
                                                  "end %lld of section %s, updating "
                                                  "to %lld.\n",
                                                  symname,
                                                  oldaddr,
                                                  shdr_info[scnidx].name,
                                                  newaddr);
                                            sym->st_value = newaddr;
                                        }
                                        break;
                                    }
                                }
                            } /* for each section... */
                            /* NOTE: We don't update st_shndx, because this is a special
                                     symbol.  I am not sure if it's necessary though.
                            */
                            if (scnidx < num_shdr_info) {
                                FAILIF_LIBELF(gelf_update_symshndx(symdata,
                                                                   NULL,
                                                                   inner,
                                                                   sym,
                                                                   0) == 0,
                                              gelf_update_symshndx);
                            }
                        } /* if symbol is _DYNAMIC else */
                    }

                    continue;
                } /* handle special-section symbols */

                /* The symbol must refer to a section which is not being
                   removed. */
                if(shdr_info[scnidx].idx == 0)
                {
                    FAILIF(GELF_ST_TYPE (sym->st_info) != STT_SECTION,
                           "Non-STT_SECTION symbol [%s] refers to section [%s],"
                           " which is being removed.\n",
                           symname,
                           shdr_info[scnidx].name);
                    INFO("STT_SECTION symbol [%s] refers to section [%s], "
                         "which is being removed.  Skipping...\n",
                         symname,
                         shdr_info[scnidx].name);
                    continue;
                }

                INFO("   %8d %-17s   ",
                     sym->st_shndx,
                     shdr_info[sym->st_shndx].name);

                /* Has the section's offset (hence its virtual address,
                   because we set that to the same value as the offset) changed?
                   If so, calculate the delta and update the symbol entry.
                */
                Elf64_Sxword delta;
                delta =
                    shdr_info[scnidx].shdr.sh_offset -
                    shdr_info[scnidx].old_shdr.sh_offset;

                Elf64_Sxword vaddr_delta;
                vaddr_delta =
                    shdr_info[scnidx].shdr.sh_addr -
                    shdr_info[scnidx].old_shdr.sh_addr;

                if (vaddr_delta || shdr_info[scnidx].idx != scnidx) {

                    if (sym->st_value)
                        INFO("0x%llx -> 0x%llx (delta %lld)",
                             sym->st_value,
                             sym->st_value + vaddr_delta,
                             vaddr_delta);
                    else {
                        INFO("(value is zero, not adjusting it)");
                        /* This might be a bit too paranoid, but symbols with values of
                           zero for which we are not adjusting the value must be in the
                           static-symbol section and refer to a section which is
                           not loaded at run time.  If this assertion ever fails, figure
                           out why and also figure out whether the zero value should have
                           been adjusted, after all.
                        */
                        ASSERT(!(shdr_info[sym->st_shndx].shdr.sh_flags & SHF_ALLOC));
                        ASSERT(shdr_info[i].shdr.sh_type == SHT_SYMTAB);
                    }

                    /* The section index of the symbol must coincide with
                       the shdr_info[] index of the section that the
                       symbol refers to.  Since that section may have been
                       moved, its new setion index, which is stored in
                       the idx field, may have changed.  However the index
                       of the original section must match.
                    */
                    ASSERT(scnidx == elf_ndxscn(shdr_info[scnidx].scn));

                    if(unlikely(verbose_flag)) {
                        if (shdr_info[scnidx].idx != scnidx) {
                          INFO(" (updating sym->st_shndx = %lld --> %lld)\n",
                               sym->st_shndx,
                               shdr_info[scnidx].idx);
                        }
                        else INFO("(sym->st_shndx remains %lld)\n", sym->st_shndx);
                    }

                    sym->st_shndx = shdr_info[scnidx].idx;
                    if (sym->st_value)
                        sym->st_value += vaddr_delta;
                    FAILIF_LIBELF(gelf_update_symshndx(symdata,
                                                       NULL,
                                                       inner,
                                                       sym,
                                                       0) == 0,
                                  gelf_update_symshndx);
                }
                else {
                    INFO(" (no change)\n");
                }
            } /* for each symbol */
        } /* if it's a symbol table... */
    } /* for each section... */
}

static void adjust_section_offset(Elf *newelf,
                                  shdr_info_t *shdr_info,
                                  Elf64_Sxword delta)
{
    Elf_Scn *scn = elf_getscn (newelf, shdr_info->idx);
    ASSERT(scn != NULL);

    ASSERT(((Elf64_Sxword)shdr_info->shdr.sh_offset) + delta >= 0);
    shdr_info->shdr.sh_offset += delta;
    ASSERT(shdr_info->shdr.sh_addralign);
#ifdef DEBUG
    /* The assumption is that the delta is calculated so that it will preserve
       the alignment.  Of course, we don't trust ourselves so we verify.

       NOTE:  The assertion below need not hold about NOBITS sections (such as
       the .bss section), for which the offset in the file and the address at
       which the section is to be loaded may differ.
    */
    if (shdr_info->shdr.sh_type != SHT_NOBITS)
    {
        Elf64_Off new_offset = shdr_info->shdr.sh_offset;
        new_offset += shdr_info->shdr.sh_addralign - 1;
        new_offset &= ~((GElf_Off)(shdr_info->shdr.sh_addralign - 1));

        ASSERT(shdr_info->shdr.sh_offset == new_offset);
    }
#endif
    INFO("\t\t\t\tsection offset %lld -> %lld%s\n",
         shdr_info->old_shdr.sh_offset,
         shdr_info->shdr.sh_offset,
         (shdr_info->old_shdr.sh_offset ==
          shdr_info->shdr.sh_offset ? " (SAME)" : ""));

    /* If there is a delta for an ALLOC section, then the sections address must match the sections's offset in
       the file, if that section is not marked SHT_NOBITS.  For SHT_NOBITS sections, the two may differ.
       Note that we compare against the old_shdr.sh_offset because we just modified shdr.sh_offset!
    */

    ASSERT(!delta ||
           !(shdr_info->shdr.sh_flags & SHF_ALLOC) ||
           shdr_info->shdr.sh_type == SHT_NOBITS ||
           shdr_info->shdr.sh_addr == shdr_info->old_shdr.sh_offset);

    if ((shdr_info->shdr.sh_flags & SHF_ALLOC) == SHF_ALLOC)
    {
        ASSERT(shdr_info->shdr.sh_addr);
        shdr_info->shdr.sh_addr += delta;
        INFO("\t\t\t\tsection address %lld -> %lld%s\n",
             shdr_info->old_shdr.sh_addr,
             shdr_info->shdr.sh_addr,
             (shdr_info->old_shdr.sh_addr ==
              shdr_info->shdr.sh_addr ? " (SAME)" : ""));
    }

    /* Set the section header in the new file. There cannot be any
       overflows. */
    INFO("\t\t\t\tupdating section header (size %lld)\n",
         shdr_info->shdr.sh_size);
    FAILIF(!gelf_update_shdr (scn, &shdr_info->shdr),
           "Could not update section header for section %s!\n",
           shdr_info->name);
}

#ifdef MOVE_SECTIONS_IN_RANGES
static int get_end_of_range(shdr_info_t *shdr_info,
                            int num_shdr_info,
                            int start,
                            Elf64_Xword *alignment,
                            Elf32_Word *real_align)
{
    int end = start;
    ASSERT(start < num_shdr_info);

    /* Note that in the loop below we do not check to see if a section is
       being thrown away.  If a section in the middle of a range is thrown
       away, that will cause the section to be removed, but it will not cause
       the relative offsets of the sections in the block to be modified.
    */

    *alignment = real_align[start];
    while (end < num_shdr_info &&
           ((shdr_info[end].shdr.sh_flags & SHF_ALLOC) == SHF_ALLOC) &&
           ((shdr_info[end].shdr.sh_type == SHT_PROGBITS) ||
            (shdr_info[end].shdr.sh_type == SHT_INIT_ARRAY) ||
            (shdr_info[end].shdr.sh_type == SHT_FINI_ARRAY) ||
            (shdr_info[end].shdr.sh_type == SHT_PREINIT_ARRAY) ||
         /* (shdr_info[end].shdr.sh_type == SHT_NOBITS) || */
#ifdef ARM_SPECIFIC_HACKS
            /* SHF_ALLOC sections with with names starting with ".ARM." are
               part of the ARM EABI extensions to ELF.
            */
            !strncmp(shdr_info[end].name, ".ARM.", 5) ||
#endif
            (shdr_info[end].shdr.sh_type == SHT_DYNAMIC)))
    {
        if (real_align[end] > *alignment) {
            *alignment = real_align[end];
        }
        end++;
    }

    return end == start ? end + 1 : end;
}
#endif/*MOVE_SECTIONS_IN_RANGES*/

static GElf_Off update_last_offset(shdr_info_t *shdr_info,
                                   range_list_t *section_ranges,
                                   GElf_Off offset)
{
    GElf_Off filesz = 0;
    if (shdr_info->shdr.sh_type != SHT_NOBITS) {
        /* This function is used as an assertion: if the range we are
           adding conflicts with another range already in the list,
           then add_unique_range() will call FAILIF().
        */
        add_unique_range_nosort(section_ranges,
                                shdr_info->shdr.sh_offset,
                                shdr_info->shdr.sh_size,
                                shdr_info,
                                handle_range_error,
                                NULL);

        filesz = shdr_info->shdr.sh_size;
    }

    /* Remember the last section written so far. */
    if (offset < shdr_info->shdr.sh_offset + filesz) {
        offset = shdr_info->shdr.sh_offset + filesz;
        INFO("\t\t\t\tupdated lastoffset to %lld\n", offset);
    }

    return offset;
}

static GElf_Off move_sections(Elf *newelf,
                              shdr_info_t *shdr_info,
                              int num_shdr_info,
                              int start,
                              int end,
                              GElf_Off offset,
                              Elf64_Xword alignment,
                              range_list_t *section_ranges,
                              bool adjust_alloc_section_offsets)
{
    /* The alignment parameter is expected to contain the largest alignment of
       all sections in the block.  Thus, when we iterate over all sections in
       the block and apply the same offset to them, we are guaranteed to
       preserve (a) the relative offsets between the sections in the block and
       (b) the alignment requirements of each individual section.
    */

    ASSERT(start < num_shdr_info);
    ASSERT(end <= num_shdr_info);

    Elf64_Sxword delta = offset - shdr_info[start].shdr.sh_offset;
    delta += (alignment - 1);
    delta &= ~(alignment - 1);
    while (start < end) {
        if (shdr_info[start].idx > 0) {
            if (adjust_alloc_section_offsets || (shdr_info[start].shdr.sh_flags & SHF_ALLOC) != SHF_ALLOC) {
                INFO("\t\t\t%03d:\tAdjusting offset of section %s "
                     "(index %d) from 0x%llx (%lld) to 0x%llx (%lld) (DELTA %lld)...\n",
                     start,
                     (shdr_info[start].name ?: "(no name)"),
                     shdr_info[start].idx,
                     shdr_info[start].old_shdr.sh_offset, shdr_info[start].old_shdr.sh_offset,
                     offset, offset,
                     delta);

                /* Compute the new offset of the section. */
                adjust_section_offset(newelf, shdr_info + start, delta);
            }
            else {
                INFO("\t\t\t%03d: NOT adjusting offset of section %s (index %d)"
                     ": (not moving SHF_ALLOC sections)...\n",
                     start,
                     (shdr_info[start].name ?: "(no name)"),
                     shdr_info[start].idx);
            }
            offset = update_last_offset(shdr_info + start,
                                        section_ranges,
                                        offset);
        } /* if (shdr_info[start].idx > 0) */
        else {
            INFO("\t\t\t%03d: NOT adjusting offset of section %s (index %d)"
                 " (ignored)...\n",
                 start,
                 (shdr_info[start].name ?: "(no name)"),
                 shdr_info[start].idx);
        }
        start++;
    }

    sort_ranges(section_ranges);
    return offset;
}

/* Compute the alignments of sections with consideration of segment
   alignments.  Returns an array of Elf32_Word containing the alignment
   of sections.  Callee is responsible to deallocate the array after use.  */
Elf32_Word *
get_section_real_align (GElf_Ehdr *ehdr, GElf_Phdr *phdr_info,
                        struct shdr_info_t *shdr_info, int shdr_info_len)
{
    size_t max_align_array_size;
    Elf32_Word *max_align;
    size_t first_section;
    bool propagate_p;
    int si, pi;

    max_align_array_size = sizeof(Elf32_Word) * shdr_info_len;
    max_align = (Elf32_Word*) malloc (max_align_array_size);
    FAILIF(!max_align, "malloc(%zu) failed.\n",  max_align_array_size);

    /* Initialize alignment array.  */
    max_align[0] = 0;
    for (si = 1; si < shdr_info_len; si++)
        max_align[si] = shdr_info[si].shdr.sh_addralign;

    /* Determine which sections need to be aligned with the alignment of
       containing segments.  Becasue the first section in a segment may
       be deleted, we need to look at all sections and compare their offsets.
     */
    for (pi = 0; pi < ehdr->e_phnum; ++pi) {
        /* Skip null segment. */
        if (phdr_info[pi].p_type == PT_NULL)
            continue;

        /* Look for the first non-deleted section of a segment in output.
           We assume asections are sorted by offsets. Also check to see if
           a segment starts with a section.  We only want to propagate
           alignment if the segment starts with a section.  */
        propagate_p = false;
        first_section = 0;
        for (si = 1; si < shdr_info_len && first_section == 0; si++) {
            if (shdr_info[si].old_shdr.sh_offset == phdr_info[pi].p_offset)
                propagate_p = true;

            if (shdr_info[si].idx > 0
                && section_belongs_to_header(&shdr_info[si].old_shdr,
                                             &phdr_info[pi]))
                first_section = si;
        }

        if (!propagate_p || first_section == 0)
            continue;

        /* Adjust alignment of first section.  Note that a section can appear
           in multiple segments.  We only need the extra alignment if the
           section's alignment is smaller than that of the segment.  */
       if (first_section != 0 &&
            max_align[first_section] < phdr_info[pi].p_align) {
            max_align[first_section] = phdr_info[pi].p_align;
       }
    }

    return max_align;
}

static range_list_t *
update_section_offsets(Elf *elf,
                       Elf *newelf,
                       GElf_Phdr *phdr_info,
                       shdr_info_t *shdr_info,
                       int num_shdr_info,
                       range_list_t *section_ranges,
                       bool adjust_alloc_section_offsets)
{
    Elf32_Word *real_align;

    ASSERT(section_ranges);
    INFO("Updating section addresses and offsets...\n");
    /* The initial value of lastoffset is set to the size of the ELF header
       plus the size of the program-header table.  libelf seems to always
       place the program-header table for a new file immediately after the
       ELF header itself... or I could not find any other way to change it
       otherwise.
    */
    GElf_Ehdr ehdr_mem, *ehdr;
    ehdr = gelf_getehdr (elf, &ehdr_mem);
    FAILIF_LIBELF(NULL == ehdr, gelf_getehdr);
    const size_t ehdr_size = gelf_fsize (elf, ELF_T_EHDR, 1, EV_CURRENT);
    FAILIF(ehdr->e_phoff != ehdr_size,
           "Expecting the program-header table to follow the ELF header"
           " immediately!\n");

    GElf_Off lastoffset = 0;
    lastoffset += ehdr_size;
    lastoffset += ehdr->e_phnum * ehdr->e_phentsize;
    INFO("Section offsets will start from %lld.\n", lastoffset);

    int start = 1, end = 1;
    ASSERT(num_shdr_info > 0);
    real_align = get_section_real_align (ehdr, phdr_info, shdr_info,
                                         num_shdr_info);
    while (end < num_shdr_info) {
        Elf64_Xword alignment;
        /* end is the index one past the last section of the block. */
#ifdef MOVE_SECTIONS_IN_RANGES
        end = get_end_of_range(shdr_info, num_shdr_info,
                               start, &alignment, real_align);
#else
        end = start + 1;
        alignment = real_align[start];
#endif

        INFO("\tAdjusting sections [%d - %d) as a group (start offset %lld, alignment %lld)\n",
             start, end, lastoffset, alignment);
        lastoffset = move_sections(newelf,
                                   shdr_info,
                                   num_shdr_info,
                                   start, end,
                                   lastoffset,
                                   alignment,
                                   section_ranges,
                                   adjust_alloc_section_offsets);

        start = end;
    }

    ASSERT(lastoffset == get_last_address(section_ranges));
    free (real_align);
    return section_ranges;
}

void handle_range_error(range_error_t err, range_t *left, range_t *right)
{
    shdr_info_t *info_l = (shdr_info_t *)left->user;
    shdr_info_t *info_r = (shdr_info_t *)right->user;
    ASSERT(info_l);
    ASSERT(info_r);

    switch (err) {
    case ERROR_CONTAINS:
        ERROR("ERROR: section [%s] (%lld, %lld bytes) contains "
              "section [%s] (%lld, %lld bytes)\n",
              info_l->name,
              left->start, left->length,
              info_r->name,
              right->start, right->length);
        break;
    case ERROR_OVERLAPS:
        ERROR("ERROR: Section [%s] (%lld, %lld bytes) intersects "
              "section [%s] (%lld, %lld bytes)\n",
              info_l->name,
              left->start, left->length,
              info_r->name,
              right->start, right->length);
        break;
    default:
        ASSERT(!"Unknown range error code!");
    }

    FAILIF(1, "Range error.\n");
}

#ifdef DEBUG

/* Functions to ELF file is still sane after adjustment.  */

static bool
sections_overlap_p (GElf_Shdr *s1, GElf_Shdr *s2)
{
    GElf_Addr a1, a2;
    GElf_Off o1, o2;

    if ((s1->sh_flags & s2->sh_flags & SHF_ALLOC) != 0) {
        a1 = (s1->sh_addr > s2->sh_addr)? s1->sh_addr : s2->sh_addr;
        a2 = ((s1->sh_addr + s1->sh_size < s2->sh_addr + s2->sh_size)?
              (s1->sh_addr + s1->sh_size) : (s2->sh_addr + s2->sh_size));
        if (a1 < a2)
            return true;
    }

    if (s1->sh_type != SHT_NOBITS && s2->sh_type != SHT_NOBITS) {
        o1 = (s1->sh_offset > s2->sh_offset)? s1->sh_offset : s2->sh_offset;
        o2 = ((s1->sh_offset + s1->sh_size < s2->sh_offset + s2->sh_size)?
              (s1->sh_offset + s1->sh_size) : (s2->sh_offset + s2->sh_size));
        if (o1 < o2)
            return true;
    }

    return false;
}

/* Return size of the overlapping portion of section S and segment P
   in memory.  */

static GElf_Word
mem_overlap_size (GElf_Shdr *s, GElf_Phdr *p)
{
    GElf_Addr a1, a2;

    if (s->sh_flags & SHF_ALLOC) {
        a1 = p->p_vaddr > s->sh_addr ? p->p_vaddr : s->sh_addr;
        a2 = ((p->p_vaddr + p->p_memsz < s->sh_addr + s->sh_size) ?
              (p->p_vaddr + p->p_memsz) : (s->sh_addr + s->sh_size));
        if (a1 < a2) {
            return a2 - a1;
        }
    }
    return 0;
}

/* Return size of the overlapping portion of section S and segment P
   in file.  */

static GElf_Word
file_overlap_size (GElf_Shdr *s, GElf_Phdr *p)
{
    GElf_Off o1, o2;

    if (s->sh_type != SHT_NOBITS) {
        o1 = p->p_offset > s->sh_offset ? p->p_offset : s->sh_offset;
        o2 = ((p->p_offset + p->p_filesz < s->sh_offset + s->sh_size) ?
              (p->p_offset + p->p_filesz) : (s->sh_offset + s->sh_size));
        if (o1 < o2) {
            return o2 - o1;
        }
    }
    return 0;
}

/* Verify the ELF file is sane. */
static void
verify_elf(GElf_Ehdr *ehdr, struct shdr_info_t *shdr_info, int shdr_info_len,
           GElf_Phdr *phdr_info)
{
    int si, sj, pi;
    GElf_Word addralign;
    GElf_Word m_size, f_size;

    /* Check all sections */
    for (si = 1; si < shdr_info_len; si++) {
        if (shdr_info[si].idx <= 0)
            continue;

        /* Check alignment */
        addralign = shdr_info[si].shdr.sh_addralign;
        if (addralign != 0) {
            if (shdr_info[si].shdr.sh_flags & SHF_ALLOC) {
                FAILIF ((addralign - 1) & shdr_info[si].shdr.sh_addr,
                        "Load address %llx of section %s is not "
                        "aligned to multiples of %u\n",
                        (long long unsigned) shdr_info[si].shdr.sh_addr,
                        shdr_info[si].name,
                        addralign);
            }

            if (shdr_info[si].shdr.sh_type != SHT_NOBITS) {
                FAILIF ((addralign - 1) & shdr_info[si].shdr.sh_offset,
                        "Offset %lx of section %s is not "
                        "aligned to multiples of %u\n",
                        shdr_info[si].shdr.sh_offset,
                        shdr_info[si].name,
                        addralign);
            }
        }

        /* Verify that sections do not overlap. */
        for (sj = si + 1; sj < shdr_info_len; sj++) {
            if (shdr_info[sj].idx <= 0)
                continue;

            FAILIF (sections_overlap_p (&shdr_info[si].shdr,
                                        &shdr_info[sj].shdr),
                    "sections %s and %s overlap.\n", shdr_info[si].name,
                    shdr_info[sj].name);
        }

        /* Verify that section is properly contained in segments. */
        for (pi = 0; pi < ehdr->e_phnum; pi++) {
            if (phdr_info[pi].p_type == PT_NULL)
                continue;

            f_size = file_overlap_size (&shdr_info[si].shdr, &phdr_info[pi]);
            m_size = mem_overlap_size (&shdr_info[si].shdr, &phdr_info[pi]);

            if (f_size) {
                FAILIF (shdr_info[si].shdr.sh_size > phdr_info[pi].p_filesz,
                        "Section %s is larger than segment %d\n",
                        shdr_info[si].name, pi);
                FAILIF (f_size != shdr_info[si].shdr.sh_size,
                        "Section %s partially overlaps segment %d in file.\n",
                        shdr_info[si].name, pi);
            }

            if (m_size) {
                FAILIF (shdr_info[si].shdr.sh_size > phdr_info[pi].p_memsz,
                        "Section %s is larger than segment %d\n",
                        shdr_info[si].name, pi);
                FAILIF (m_size != shdr_info[si].shdr.sh_size,
                        "Section %s partially overlaps segment %d in memory.\n",
                        shdr_info[si].name, pi);
            }

        }
    }
}
#endif /* DEBUG */
