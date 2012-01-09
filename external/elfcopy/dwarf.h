/* dwwrf.h - DWARF support header file
   Copyright 2005
   Free Software Foundation, Inc.

This file is part of GNU Binutils.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

#include "dwarf2.h"

typedef unsigned long dwarf_vma;
typedef unsigned long dwarf_size_type;

struct dwarf_section
{
  const char *name;
  unsigned char *start;
  dwarf_vma address;
  dwarf_size_type size;
};

/* A structure containing the name of a debug section
   and a pointer to a function that can decode it.  */
struct dwarf_section_display
{
  struct dwarf_section section;
  int (*display) (struct dwarf_section *, void *);
  unsigned int relocate : 1;
  unsigned int eh_frame : 1;
};

enum dwarf_section_display_enum {
  abbrev = 0,
  aranges,
  frame,
  info,
  line,
  pubnames,
  eh_frame,
  macinfo,
  str,
  loc,
  pubtypes,
  ranges,
  static_func,
  static_vars,
  types,
  weaknames,
  max
};

extern struct dwarf_section_display debug_displays [];

/* This structure records the information that
   we extract from the.debug_info section.  */
typedef struct
{
  unsigned int   pointer_size;
  unsigned long  cu_offset;
  unsigned long	 base_address;
  /* This is an array of offsets to the location list table.  */
  unsigned long *loc_offsets;
  int		*have_frame_base;
  unsigned int   num_loc_offsets;
  unsigned int   max_loc_offsets;
#ifdef SORT_LOCATION_LIST_OFFSETS
  unsigned long  last_loc_offset; /* used to determine whether loc offsets are sorted */
#endif
  unsigned long *range_lists;
  unsigned int   num_range_lists;
  unsigned int   max_range_lists;
}
debug_info;

extern dwarf_vma (*byte_get) (unsigned char *, int);
extern dwarf_vma byte_get_little_endian (unsigned char *, int);
extern dwarf_vma byte_get_big_endian (unsigned char *, int);

extern dwarf_vma eh_addr_size;
extern int is_relocatable;

extern int do_debug_info;
extern int do_debug_abbrevs;
extern int do_debug_lines;
extern int do_debug_pubnames;
extern int do_debug_aranges;
extern int do_debug_ranges;
extern int do_debug_frames;
extern int do_debug_frames_interp;
extern int do_debug_macinfo;
extern int do_debug_str;
extern int do_debug_loc;

extern int load_debug_section (enum dwarf_section_display_enum,
			       void *);
extern void free_debug_section (enum dwarf_section_display_enum);

extern void free_debug_memory (void);

extern void base_value_pair_hook(void *data, int size,
                                 int base, int begin, int end);

extern void value_hook(void *data, int size, int val);

extern void signed_value_hook(void *data,
                              int pointer_size,
                              int is_signed,
                              int val);

extern void init_dwarf_variables(void);
