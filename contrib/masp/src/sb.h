/* sb.h - header file for string buffer manipulation routines
   Copyright 1994, 1995, 2000 Free Software Foundation, Inc.
   Copyright 2003 Johann Gunnar Oskarsson

   Written by Steve and Judy Chamberlain of Cygnus Support,
      sac@cygnus.com

   Maintained by Johann Gunnar Oskarsson
      <myrkraverk@users.sourceforge.net>

   This file is part of MASP, the Assembly Preprocessor.

   MASP is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   MASP is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with MASP; see the file COPYING.  If not, write to the Free
   Software Foundation, 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA.  */

#ifndef SB_H

#define SB_H

#include <stdio.h>

/* string blocks

   I had a couple of choices when deciding upon this data structure.
   gas uses null terminated strings for all its internal work.  This
   often means that parts of the program that want to examine
   substrings have to manipulate the data in the string to do the
   right thing (a common operation is to single out a bit of text by
   saving away the character after it, nulling it out, operating on
   the substring and then replacing the character which was under the
   null).  This is a pain and I remember a load of problems that I had with
   code in gas which almost got this right.  Also, it's harder to grow and
   allocate null terminated strings efficiently.

   Obstacks provide all the functionality needed, but are too
   complicated, hence the sb.

   An sb is allocated by the caller, and is initialzed to point to an
   sb_element.  sb_elements are kept on a free lists, and used when
   needed, replaced onto the free list when unused.
 */

#define sb_max_power_two    30	/* don't allow strings more than
			           2^sb_max_power_two long */
/* structure of an sb */
typedef struct sb
  {
    char *ptr;			/* points to the current block.  */
    int len;			/* how much is used.  */
    int pot;			/* the maximum length is 1<<pot */
    struct le *item;
  }
sb;

/* Structure of the free list object of an sb */
typedef struct le
  {
    struct le *next;
    int size;
    char data[1];
  }
sb_element;

/* The free list */
typedef struct
  {
    sb_element *size[sb_max_power_two];
  } sb_list_vector;

extern int string_count[sb_max_power_two];

extern void sb_build(sb *ptr, int size);
extern void sb_new(sb *ptr);
extern void sb_kill(sb *ptr);
extern void sb_add_sb(sb *ptr, const sb *s);
extern void sb_reset(sb *ptr);
extern void sb_add_char(sb *ptr, int c);
extern void sb_add_string(sb *ptr, const char *s);
extern void sb_add_buffer(sb *ptr, const char *s, int len);
extern void sb_print(FILE *outfile, sb *ptr);
extern void sb_print_at(FILE *outfile, int idx, sb *ptr);
extern char *sb_name(sb *in);
extern char *sb_terminate(sb *in);
extern int sb_skip_white(int idx, const sb *ptr);
extern int sb_skip_comma(int idx, const sb *ptr);

// new functions, myrkraverk
extern int sb_eat_literal( int idx, sb *out, const sb *in ); // index, out, in

/* Actually in input-scrub.c.  */
extern void input_scrub_include_sb(sb *ptr, const char *name, int again);

#endif /* SB_H */
