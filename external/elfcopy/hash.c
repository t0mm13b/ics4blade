#include <common.h>
#include <debug.h>
#include <libelf.h>
#include <hash.h>
#include <string.h>

void setup_hash(Elf_Data *hash_data,
                Elf32_Word nbuckets,
                Elf32_Word nchains)
{
    hash_data->d_size  = 2;
    hash_data->d_size += nbuckets;
    hash_data->d_size += nchains;
    hash_data->d_buf   = CALLOC(hash_data->d_size, sizeof(Elf32_Word));
    hash_data->d_size *= sizeof(Elf32_Word);
    ((Elf32_Word *)hash_data->d_buf)[0] = nbuckets;
    ((Elf32_Word *)hash_data->d_buf)[1] = nchains;
}

void add_to_hash(Elf_Data *hash_data,
                 const char *symbol,
                 int symindex)
{
    Elf32_Word *buckets  = (Elf32_Word *)hash_data->d_buf;
    Elf32_Word nbuckets  = *buckets++;
    Elf32_Word *chains   = ++buckets + nbuckets;
    Elf32_Word last_chain_index;
    unsigned long bucket = elf_hash(symbol) % nbuckets;

    ASSERT(symindex != STN_UNDEF);

    if (buckets[bucket] == STN_UNDEF) {
        INFO("Adding [%s] to hash at bucket [%ld] (first add)\n",
             symbol, bucket);
        buckets[bucket] = symindex;
    }
    else {
        INFO("Collision on adding [%s] to hash at bucket [%ld]\n", 
             symbol, bucket);
        last_chain_index = buckets[bucket];
        while (chains[last_chain_index] != STN_UNDEF) {
            INFO("\ttrying at chain index [%d]...\n", last_chain_index);
            last_chain_index = chains[last_chain_index];
        }
        INFO("\tsuccess at chain index [%d]...\n", last_chain_index);
        chains[last_chain_index] = symindex;
    }
}

int hash_lookup(Elf *elf, 
                section_info_t *hash_info,
                section_info_t *symtab_info,
                const char *symname,
                GElf_Sym *sym_mem)
{
    Elf32_Word *hash_data = (Elf32_Word *)hash_info->data->d_buf;
    Elf32_Word index;
    Elf32_Word nbuckets = *hash_data++;
    Elf32_Word *buckets = ++hash_data;
    Elf32_Word *chains  = hash_data + nbuckets;

    GElf_Sym *sym;

    index = buckets[elf_hash(symname) % nbuckets];
    while(index != STN_UNDEF)
    {
        sym = gelf_getsymshndx (symtab_info->data, NULL, index, sym_mem, NULL);
        FAILIF_LIBELF(NULL == sym, gelf_getsymshndx);
        if (!strcmp(symname,
                    elf_strptr(elf, symtab_info->hdr->sh_link, sym->st_name))) 
            break;
        index = chains[index];
    }

    return index;
}
