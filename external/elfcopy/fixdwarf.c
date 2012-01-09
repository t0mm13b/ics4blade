#include <fixdwarf.h>
#include <common.h>
#include <debug.h>
#include <hash.h>

#include <libelf.h>
#include <libebl.h>
#include <libebl_arm.h>

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/* When this macro is set to a nonzero value, we maintain a BST where we store each address once
   we update the value at that address, and check to make sure that the address has not been
   visited before we udpate it.  This way we make sure that we do not do multiple updates at any
   any given address.  The feature is disabled by default because it is very expensive.  It should
   be enabled as a first step in debugging problems with the DWARF patches that this code makes.
*/

#define PARANOIA (0)

#define _str(name) #name
#define _id(a,b) a ## b

#if PARANOIA
#define COLLECT_BACKTRACES (0)

#if COLLECT_BACKTRACES
#include <execinfo.h>
#endif
#endif/*PARANOIA*/

#include <dwarf.h>

int load_debug_section (enum dwarf_section_display_enum debug, void *file);
void free_debug_section (enum dwarf_section_display_enum debug);

static shdr_info_t *s_shdr_info;
static int s_shdr_info_len;
static int dwarf_to_shdr[max];
static shdr_info_t *s_cached_find_section_result = NULL;
static int s_num_total_patches = 0;
static int s_num_failed_patches = 0;

static void init_value_free_lists();

#if PARANOIA
typedef struct value_struct {
    unsigned long key;
    struct value_struct *left;
    struct value_struct *right;
#if COLLECT_BACKTRACES
#define BACKTRACE_DEPTH (10)
    void *backtrace[BACKTRACE_DEPTH];
    int backtrace_depth;
#endif/*COLLECT_BACKTRACES*/
} value_t;

static value_t *s_visited_values; /* BST of visited values */
#endif/*PARANOIA*/

static void dump_dwarf_section (enum dwarf_section_display_enum dwarf_idx);
static void byte_set_little_endian (
    unsigned char *field, int size, dwarf_vma val);
static void byte_set_big_endian (
    unsigned char *field, int size, dwarf_vma val);
static void (*byte_set) (unsigned char *, int, dwarf_vma);

void update_dwarf_if_necessary(Elf *elf __attribute__((unused)),
                               GElf_Ehdr *ehdr,
                               Elf *newelf __attribute__((unused)),
                               shdr_info_t *shdr_info, int num_shdr_info,
                               int *num_total_patches, int *num_failed_patches)
{
    /* Find the debug sections */

    int cnt;

    /* Initialize the static variables, which might have been left in
       nondefault states from a previous call to this function.
    */
    s_shdr_info = NULL;
    s_cached_find_section_result = NULL;
    s_shdr_info_len = 0;
    s_num_total_patches = 0;
    s_num_failed_patches = 0;
    memset(dwarf_to_shdr, 0, sizeof(dwarf_to_shdr));
    for(cnt = 0; cnt < max; cnt++)
        free_debug_section(cnt);
#if PARANOIA
    s_visited_values = NULL;
    init_value_free_lists();
#endif/*PARANOIA*/
    init_dwarf_variables();

    cnt = 0;

    /* Locate the .debug_<xxx> sections, and save
       their indices (in shdr_info) in the respective
       idx_debug_<xxx> variable. If a section is not
       prwesent in the file, the variable will have
       a negative value after this loop.
    */

#define CHECK_DEBUG_SECTION(sname)                                           \
        ASSERT(shdr_info[cnt].name != NULL);                                 \
        if (!strcmp(shdr_info[cnt].name,                                     \
                    ".debug_" _str(sname))) {                                \
            FAILIF(dwarf_to_shdr[sname] > 0,                                 \
                   ".debug_" _str(sname) " is already found at index %d!\n", \
                   dwarf_to_shdr[sname]);                                    \
            INFO("Index of \".debug_" _str(name) " is %d", cnt);             \
            if (shdr_info[cnt].idx > 0)                                      \
                dwarf_to_shdr[sname] = cnt;                                  \
            else INFO(", but the section is being removed.");                \
            INFO("\n");                                                      \
        }

    for(cnt = 1; cnt < num_shdr_info; cnt++) {
        CHECK_DEBUG_SECTION(aranges);
        CHECK_DEBUG_SECTION(info);
        CHECK_DEBUG_SECTION(abbrev);
        CHECK_DEBUG_SECTION(line);
        CHECK_DEBUG_SECTION(frame);
        CHECK_DEBUG_SECTION(loc);
        CHECK_DEBUG_SECTION(ranges);
        CHECK_DEBUG_SECTION(pubnames);
        CHECK_DEBUG_SECTION(str);
    }
#undef CHECK_DEBUG_SECTION

    {
        is_relocatable = (ehdr->e_type == ET_REL);
        eh_addr_size = 4;

        if (ehdr->e_ident[EI_DATA] == ELFDATA2LSB) {
            byte_get = byte_get_little_endian;
            byte_set = byte_set_little_endian;
        }
        else {
            ASSERT(ehdr->e_ident[EI_DATA] == ELFDATA2MSB);
            byte_get = byte_get_big_endian;
            byte_set = byte_set_big_endian;
        }
    }

#define ADJUST_IF_NECESSARY(sname)                                                   \
    do {                                                                             \
        if (dwarf_to_shdr[sname] > 0) {                                              \
            INFO("\nAdjusting for %s.\n", shdr_info[dwarf_to_shdr[sname]].name);     \
            dump_dwarf_section(sname);                                               \
        }                                                                            \
        else {                                                                       \
            INFO("\nNot adjusting for %s.\n", shdr_info[dwarf_to_shdr[sname]].name); \
        }                                                                            \
    } while(0)

    s_shdr_info = shdr_info;
    s_shdr_info_len = num_shdr_info;

    ADJUST_IF_NECESSARY(info);
    ADJUST_IF_NECESSARY(loc);
    ADJUST_IF_NECESSARY(aranges);
    ADJUST_IF_NECESSARY(frame);
    ADJUST_IF_NECESSARY(ranges);
    ADJUST_IF_NECESSARY(line);
    ADJUST_IF_NECESSARY(str);
    ADJUST_IF_NECESSARY(pubnames);
    ADJUST_IF_NECESSARY(abbrev);

#undef ADJUST_IF_NECESSRY

    *num_total_patches = s_num_total_patches;
    *num_failed_patches = s_num_failed_patches;
}

int
load_debug_section (enum dwarf_section_display_enum debug,
                    void *file __attribute__((unused)))
{
    struct dwarf_section *section = &debug_displays [debug].section;
    int shdr_idx = dwarf_to_shdr[debug];
    if (!shdr_idx) {
        INFO("Could not load section %s: it is not in the file.\n",
             debug_displays[debug].section.name);
        return 0;
    }
    ASSERT(s_shdr_info);

    INFO("Loading DWARF section type %s index %d (type %d)\n",
         s_shdr_info[shdr_idx].name,
         s_shdr_info[shdr_idx].idx,
         debug);

    /* If it is already loaded, do nothing.  */
    if (section->start != NULL) {
        INFO("\tAlready loaded DWARF section type %s (type %d)\n", s_shdr_info[shdr_idx].name, debug);
        return 1;
    }

    ASSERT(s_shdr_info[shdr_idx].newdata);

    section->address = s_shdr_info[shdr_idx].shdr.sh_addr;
    section->start = s_shdr_info[shdr_idx].newdata->d_buf;
    section->size = s_shdr_info[shdr_idx].newdata->d_size;
    ASSERT(s_shdr_info[shdr_idx].newdata->d_off == 0);

    ASSERT(section->size != 0);
    ASSERT(s_shdr_info[shdr_idx].shdr.sh_size == s_shdr_info[shdr_idx].newdata->d_size);
    ASSERT(section->start != NULL);

    return 1;
}

void
free_debug_section (enum dwarf_section_display_enum debug)
{
    struct dwarf_section *section = &debug_displays [debug].section;

    INFO("Unloading DWARF section type %d\n", debug);

    if (section->start == NULL)
        return;

    section->start = NULL;
    section->address = 0;
    section->size = 0;
}

static void
dump_dwarf_section (enum dwarf_section_display_enum dwarf_idx)
{
    int shdr_idx = dwarf_to_shdr[dwarf_idx];
    ASSERT(shdr_idx);
    ASSERT(s_shdr_info);
    ASSERT(s_shdr_info[shdr_idx].idx);
    ASSERT(s_shdr_info[shdr_idx].name);

    ASSERT(!strcmp (debug_displays[dwarf_idx].section.name, s_shdr_info[shdr_idx].name));

    if (!debug_displays[dwarf_idx].eh_frame) {
        struct dwarf_section *sec = &debug_displays [dwarf_idx].section;

        if (load_debug_section (dwarf_idx, NULL)) {
            INFO("Dumping DWARF section [%s] (type %d).\n",
                 s_shdr_info[shdr_idx].name,
                 dwarf_idx);
            debug_displays[dwarf_idx].display (sec, NULL);
            if (dwarf_idx != info && dwarf_idx != abbrev)
                free_debug_section (dwarf_idx);
        }
    }
}

static shdr_info_t *find_section(int value)
{
    ASSERT(s_shdr_info != NULL);
    ASSERT(s_shdr_info_len > 0);

#define IN_RANGE(v,s,l) ((s)<=(v) && (v)<((s)+(l)))
    if (s_cached_find_section_result != NULL &&
        IN_RANGE((unsigned)value,
                 s_cached_find_section_result->old_shdr.sh_addr,
                 s_cached_find_section_result->old_shdr.sh_size)) {
        return s_cached_find_section_result;
    }

    /* Find the section to which the address belongs. */
    int cnt;
    for (cnt = 0; cnt < s_shdr_info_len; cnt++) {
        if (s_shdr_info[cnt].idx > 0 &&
            (s_shdr_info[cnt].old_shdr.sh_flags & SHF_ALLOC) &&
            IN_RANGE((unsigned) value,
                     s_shdr_info[cnt].old_shdr.sh_addr,
                     s_shdr_info[cnt].old_shdr.sh_size)) {

            s_cached_find_section_result = s_shdr_info + cnt;
            return s_cached_find_section_result;
        }
    }
#undef IN_RANGE

    return NULL;
}

#if PARANOIA
static value_t **s_value_free_lists;
static int s_num_free_lists;
static int s_cur_free_list;
static int s_alloc_values; /* number of allocated values in the list */
#define LISTS_INCREMENT (10)
#define NUM_VALUES_PER_LIST (10000)

static void init_value_free_lists()
{
    if (s_value_free_lists) {
        value_t **trav = s_value_free_lists;
        while(s_cur_free_list) {
            FREE(*trav++);
            s_cur_free_list--;
        }
        FREE(s_value_free_lists);
        s_value_free_lists = NULL;
    }
    s_num_free_lists = 0;
    s_alloc_values = 0;
}

static value_t *alloc_value()
{
    if (s_alloc_values == NUM_VALUES_PER_LIST) {
        s_cur_free_list++;
        s_alloc_values = 0;
    }

    if (s_cur_free_list == s_num_free_lists) {
        s_num_free_lists += LISTS_INCREMENT;
        s_value_free_lists = REALLOC(s_value_free_lists,
                                     s_num_free_lists * sizeof(value_t *));
        memset(s_value_free_lists + s_cur_free_list,
               0,
               (s_num_free_lists - s_cur_free_list) * sizeof(value_t *));
    }

    if (s_value_free_lists[s_cur_free_list] == NULL) {
        s_value_free_lists[s_cur_free_list] = MALLOC(NUM_VALUES_PER_LIST*sizeof(value_t));
    }

    return s_value_free_lists[s_cur_free_list] + s_alloc_values++;
}

static value_t *would_be_parent = NULL;
static value_t *find_value(unsigned long val)
{
    would_be_parent = NULL;
    value_t *trav = s_visited_values;
    while(trav) {
        would_be_parent = trav;
        if (val < trav->key)
            trav = trav->left;
        else if (val > trav->key)
            trav = trav->right;
        else if (val == trav->key) {
            return trav;
        }
    }
    return NULL;
}

static int value_visited(unsigned long val)
{
    value_t *found = find_value(val);
    if (found != NULL) {
#if COLLECT_BACKTRACES
        void *new_bt[BACKTRACE_DEPTH];
        int new_bt_depth = backtrace(new_bt, BACKTRACE_DEPTH);
        char **symbols = backtrace_symbols(new_bt, new_bt_depth);
        PRINT("NEW VISIT AT %x\n", val);
        if (symbols != NULL) {
            int cnt = 0;
            while(cnt < new_bt_depth) {
                PRINT("\t%s\n", symbols[cnt]);
                cnt++;
            }
        }
        FREE(symbols);
        PRINT("OLD VISIT AT %x\n", val);
        symbols = backtrace_symbols(found->backtrace, found->backtrace_depth);
        if (symbols != NULL) {
            int cnt = 0;
            while(cnt < new_bt_depth) {
                PRINT("\t%s\n", symbols[cnt]);
                cnt++;
            }
        }
        FREE(symbols);
#else
        ERROR("DWARF: Double update at address 0x%lx!\n", val);
#endif/*COLLECT_BACKTRACES*/
        return 1;
    }
    found = alloc_value();
    found->left = found->right = NULL;
    found->key = val;
#if COLLECT_BACKTRACES
    found->backtrace_depth = backtrace(found->backtrace, BACKTRACE_DEPTH);
#endif/*COLLECT_BACKTRACES*/
    if (would_be_parent == NULL) {
        s_visited_values = found;
    } else {
        if (val < would_be_parent->key)
            would_be_parent->left = found;
        else
            would_be_parent->right = found;
    }
    return 0;
}
#else
static int value_visited(unsigned long val __attribute__((unused)))
{
    return 0;
}
#endif /*PARANOIA*/

void value_hook(void *data, int size, int val)
{
    shdr_info_t *shdr = find_section(val);
    s_num_total_patches++;
    if(shdr == NULL) {
        PRINT("DWARF: cannot map address 0x%x to any section!\n", val);
        s_num_failed_patches++;
        return;
    }
    long delta = shdr->shdr.sh_addr - shdr->old_shdr.sh_addr;
    if(delta) {
        if (!value_visited((unsigned long)data)) {
            INFO("DWARF: adjusting %d-byte value at %p: 0x%x -> 0x%x (delta %d per section %s)\n",
                 size, data,
                 val, (int)(val + delta), (int)delta,
                 shdr->name);
            byte_set(data, size, val + delta);
        }
    }
}

void base_value_pair_hook(void *data, int size,
                          int base, int begin, int end)
{
    shdr_info_t *shdr = find_section(base + begin);
    s_num_total_patches++;

    if (begin > end) {
        PRINT("DWARF: start > end in range 0x%x:[0x%x, 0x%x)!\n",
              base,
              begin,
              end);
        s_num_failed_patches++;
        return;
    }

    if(shdr == NULL) {
        PRINT("DWARF: cannot map range 0x%x:[0x%x, 0x%x) to any section!\n",
              base,
              begin,
              end);
        s_num_failed_patches++;
        return;
    }

    if (unlikely(begin != end)) {
        shdr_info_t *end_shdr = find_section(base + end - 1);
        if (shdr != end_shdr) {
            printf("DWARF: range 0x%x:[%x, %x) maps to different sections: %s and %s!\n",
                   base,
                   begin, end,
                   shdr->name,
                   (end_shdr ? end_shdr->name : "(none)"));
            s_num_failed_patches++;
            return;
        }
    }

    long delta = shdr->shdr.sh_addr - shdr->old_shdr.sh_addr;
    if(delta) {
        if (!value_visited((unsigned long)data)) {
            INFO("DWARF: adjusting %d-byte value at %p: 0x%x -> 0x%x (delta %d per section %s)\n",
                 size, data,
                 begin, (int)(begin + delta), (int)delta,
                 shdr->name);
            byte_set(data, size, begin + delta);
            byte_set(data + size, size, end + delta);
        }
    }
}

void signed_value_hook(
    void *data,
    int pointer_size,
    int is_signed,
    int value)
{
    INFO("DWARF frame info: initial PC value: %8x (width %d), %ssigned\n",
         value, pointer_size,
         (!is_signed ? "un" : ""));

    ASSERT(s_shdr_info != NULL);

    /* Find the section to which the address belongs. */
    shdr_info_t *shdr = find_section(value);
    s_num_total_patches++;
    if(shdr == NULL) {
        PRINT("DWARF: cannot map address 0x%x to any section!\n", value);
        s_num_failed_patches++;
        return;
    }

    long delta = shdr->shdr.sh_addr - shdr->old_shdr.sh_addr;

    INFO("DWARF frame info: initial PC value: 0x%lx -> 0x%lx (delta %ld per section %s).\n",
         (long)value,
         (long)(value + delta),
         delta,
         shdr->name);

    if (delta) {
        if (!value_visited((unsigned long)data)) {
            value += delta;
            if (is_signed) {
                switch (pointer_size) {
                case 1:
                    value &= 0xFF;
                    value = (value ^ 0x80) - 0x80;
                    break;
                case 2:
                    value &= 0xFFFF;
                    value = (value ^ 0x8000) - 0x8000;
                    break;
                case 4:
                    value &= 0xFFFFFFFF;
                    value = (value ^ 0x80000000) - 0x80000000;
                    break;
                case 8:
                    break;
                default:
                    FAILIF(1, "Unsupported data size %d!\n", pointer_size);
                }
            }
            byte_set(data, pointer_size, value);
        }
    }
}

static void byte_set_little_endian (unsigned char *field, int size, dwarf_vma val)
{
    switch (size) {
    case 1:
        FAILIF(val > 0xFF,
               "Attempting to set value 0x%lx to %d-bit integer!\n",
               val, size*8);
        *((uint8_t *)field) = (uint8_t)val;
        break;
    case 2:
        FAILIF(val > 0xFFFF,
               "Attempting to set value 0x%lx to %d-bit integer!\n",
               val, size*8);
        field[1] = (uint8_t)(val >> 8);
        field[0] = (uint8_t)val;
        break;
    case 4:
#if 0
		// this will signal false negatives when running on a 64 bit system.
        FAILIF(val > 0xFFFFFFFF,
               "Attempting to set value 0x%lx to %d-bit integer!\n",
               val, size*8);
#endif
        field[3] = (uint8_t)(val >> 23);
        field[2] = (uint8_t)(val >> 16);
        field[1] = (uint8_t)(val >> 8);
        field[0] = (uint8_t)val;
        break;
    default:
        FAILIF(1, "Unhandled data length: %d\n", size);
    }
}

static void byte_set_big_endian (unsigned char *field __attribute__((unused)),
                                 int size __attribute__((unused)),
                                 dwarf_vma val __attribute__((unused)))
{
    FAILIF(1, "Not implemented.\n");
}
