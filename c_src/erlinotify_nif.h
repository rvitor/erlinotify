#ifndef __ERLINOTIFY_NIF_QUEUE_H
#define __ERLINOTIFY_NIF_QUEUE_H

#include "erl_nif.h"

typedef struct
{
  ErlNifThreadOpts* opts;
  ErlNifTid qthread;
  int fd;
  ErlNifPid* pid;
} state_t;

static ErlNifResourceType* erlinotify_nif_RESOURCE = NULL;

static ERL_NIF_TERM
erlinotify_nif_start(ErlNifEnv* env, int argc,
                             const ERL_NIF_TERM argv[]);

static ERL_NIF_TERM
erlinotify_nif_stop(ErlNifEnv* env, int argc,
                             const ERL_NIF_TERM argv[]);
static ERL_NIF_TERM
erlinotify_nif_add_watch(ErlNifEnv* env, int argc,
                          const ERL_NIF_TERM argv[]);

static ERL_NIF_TERM
erlinotify_nif_remove_watch(ErlNifEnv* env, int argc,
                          const ERL_NIF_TERM argv[]);

static ERL_NIF_TERM
atom_event(ErlNifEnv* env, ulong mask);

static ERL_NIF_TERM
atom_file_type(ErlNifEnv* env, ulong mask);

int
read_events(ErlNifEnv* env, void* obj);

static void*
thr_main(void* obj);


#endif
