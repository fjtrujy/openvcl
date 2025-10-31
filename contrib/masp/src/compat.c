#include "compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "obstack.h"

static const char *g_progname = NULL;

static void die_oom(void) {
  if (g_progname && *g_progname) {
    fprintf(stderr, "%s: fatal: out of memory\n", g_progname);
  } else {
    fputs("fatal: out of memory\n", stderr);
  }
  abort();
}

void *xmalloc(size_t size) {
  void *p = malloc(size);
  if (!p) die_oom();
  return p;
}

void *xrealloc(void *ptr, size_t size) {
  void *p = realloc(ptr, size);
  if (!p) die_oom();
  return p;
}

char *xstrdup(const char *s) {
  size_t n = strlen(s) + 1;
  char *p = (char *)malloc(n);
  if (!p) die_oom();
  memcpy(p, s, n);
  return p;
}

void xmalloc_set_program_name(const char *name) {
  g_progname = name;
}

/* Minimal obstack runtime to satisfy obstack.h macros */

static struct _obstack_chunk *alloc_chunk(long size)
{
  struct _obstack_chunk *c = (struct _obstack_chunk *)xmalloc(sizeof(struct _obstack_chunk) + size);
  c->limit = (char *)c + sizeof(struct _obstack_chunk) + size;
  c->prev = NULL;
  return c;
}

int _obstack_begin(struct obstack *h, int size, int alignment,
                   void *(*chunk_alloc)(long), void (*chunk_free)(void *))
{
  if (size <= 0) size = 4096;
  h->chunk_size = size;
  h->alignment_mask = alignment ? alignment - 1 : 0;
  h->maybe_empty_object = 0;
  h->alloc_failed = 0;
  h->use_extra_arg = 0;
  /* Allocate first chunk */
  struct _obstack_chunk *c;
  if (chunk_alloc)
    c = (struct _obstack_chunk *)(*chunk_alloc)((long)(size + sizeof(struct _obstack_chunk)));
  else
    c = alloc_chunk(size);
  c->prev = NULL;
  h->chunk = c;
  h->object_base = h->next_free = c->contents;
  h->chunk_limit = c->limit;
  /* Store user fns if provided */
  h->chunkfun = (struct _obstack_chunk *(*)(void *, long))chunk_alloc;
  h->freefun = (void (*)(void *, struct _obstack_chunk *))chunk_free;
  h->extra_arg = NULL;
  return 1;
}

int _obstack_begin_1(struct obstack *h, int size, int alignment,
                     void *(*chunk_alloc)(void *, long),
                     void (*chunk_free)(void *, void *), void *arg)
{
  int ok = _obstack_begin(h, size, alignment, NULL, NULL);
  h->use_extra_arg = 1;
  h->chunkfun = (struct _obstack_chunk *(*)(void *, long))chunk_alloc;
  h->freefun = (void (*)(void *, struct _obstack_chunk *))chunk_free;
  h->extra_arg = arg;
  return ok;
}

void _obstack_newchunk(struct obstack *h, int length)
{
  long needed = h->chunk_size;
  long objlen = (long)(h->next_free - h->object_base);
  if (needed < length + objlen)
    needed = length + objlen;
  struct _obstack_chunk *newc;
  if (h->use_extra_arg && h->chunkfun)
    newc = (*h->chunkfun)(h->extra_arg, needed + sizeof(struct _obstack_chunk));
  else if (h->chunkfun)
    {
      /* When extra arg is not in use, the allocator has the signature void*(long). */
      struct _obstack_chunk *(*alloc_one_arg)(long)
        = (struct _obstack_chunk *(*)(long))(void *)h->chunkfun;
      newc = alloc_one_arg(needed + sizeof(struct _obstack_chunk));
    }
  else
    newc = alloc_chunk(needed);
  newc->prev = h->chunk;
  h->chunk = newc;
  h->object_base = h->next_free = newc->contents;
  h->chunk_limit = newc->limit;
  h->maybe_empty_object = 0;
}

void _obstack_free(struct obstack *h, void *obj)
{
  /* Free all chunks if obj is NULL; otherwise unwind until chunk containing obj. */
  if (obj == NULL)
    {
      struct _obstack_chunk *c = h->chunk;
      while (c)
        {
          struct _obstack_chunk *prev = c->prev;
          if (h->use_extra_arg && h->freefun)
            (*(void (*)(void *, void *))h->freefun)(h->extra_arg, c);
          else if (h->freefun)
            (*(void (*)(void *))h->freefun)(c);
          else
            free(c);
          c = prev;
        }
      h->chunk = NULL;
      h->object_base = h->next_free = h->chunk_limit = NULL;
      return;
    }
  /* Partial unwind not needed by current code; leave as no-op. */
}

int _obstack_memory_used(struct obstack *h)
{
  int total = 0;
  struct _obstack_chunk *c = h->chunk;
  while (c)
    {
      total += (int)(c->limit - (char *)c);
      c = c->prev;
    }
  return total;
}

/* Also provide non-underscored obstack_free symbol if needed by macros. */
/* Undefine macro to allow function definition in this translation unit. */
#undef obstack_free
void obstack_free(struct obstack *h, void *obj)
{
  _obstack_free(h, obj);
}


