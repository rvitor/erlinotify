#include "erl_nif.h"
#include "erlinotify_nif.h"
#include <sys/inotify.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

/* NOTE: the spawned thread will terminate "automagically" because the
outer calling thread close()s the file descriptor the spawned thread
select()s. Thus the spawned thread will automatically come to an end
without the need to explicitly join. */

#define MAXBUFLEN 1024
#define EVENT_SIZE  ( sizeof (struct inotify_event) )
#define EVENT_BUF_LEN     ( MAXBUFLEN * ( EVENT_SIZE + 16 ) )

static ErlNifFunc nif_funcs[] =
{
    {"start", 0, erlinotify_nif_start},
    {"stop", 1, erlinotify_nif_stop},
    {"add_watch", 2, erlinotify_nif_add_watch},
    {"remove_watch", 2, erlinotify_nif_remove_watch}
};

static ERL_NIF_TERM
erlinotify_nif_start(ErlNifEnv* env,
                     int argc,
                     const ERL_NIF_TERM argv[])
{
  state_t* state =
    enif_alloc_resource(erlinotify_nif_RESOURCE,
                        sizeof(state_t));
  if (NULL == state) return enif_make_tuple2(env,
    enif_make_atom(env, "error"),
    enif_make_string(env, "unable to allocate state", ERL_NIF_LATIN1)
  );
  enif_self(env, &(state->pid));
  state->fd = inotify_init();
  ERL_NIF_TERM result = enif_make_resource(env, state);

  if (0 != enif_thread_create("", &(state->qthread), thr_main, state, NULL)) {
    enif_release_resource(state);
    return enif_make_tuple2(env, 
      enif_make_atom(env, "error"),
      enif_make_string(env, "unable to create thread", ERL_NIF_LATIN1)
    );
  }

  return enif_make_tuple2(env, enif_make_atom(env, "ok"), result);
}

static ERL_NIF_TERM
erlinotify_nif_stop(ErlNifEnv* env,
                    int argc,
                    const ERL_NIF_TERM argv[])
{
  ERL_NIF_TERM ret;
  state_t* state;
  int res;

  if (argc != 1) {
    return enif_make_badarg(env);
  }

  if (!enif_get_resource(
                         env, argv[0],
                         erlinotify_nif_RESOURCE,
                         (void**) &state)) {
    return enif_make_badarg(env);
  }
  /* let the gc do the work. NOTE: enif_free wil segfault, 
  MUST use enif_release_resource here */
  enif_release_resource(state);

  /* implicitly terminates the loop in thr_main() because select()
  will see a closed fd after this call */
  res = close(state->fd);

  if (res < 0) return ret = enif_make_tuple2(env,
    enif_make_atom(env, "error"),
    enif_make_string(env, "unable to stop watching", ERL_NIF_LATIN1)
  );

  return enif_make_atom(env, "ok");
}

static void
erlinotify_nif_resource_cleanup(ErlNifEnv* env, void* arg)
{
    /* empty as for now */
}

static ERL_NIF_TERM
erlinotify_nif_add_watch(ErlNifEnv* env,
                         int argc,
                         const ERL_NIF_TERM argv[])
{
  ERL_NIF_TERM ret;
  state_t* state;
  char dirname[MAXBUFLEN];
  (void)memset(&dirname, '\0', sizeof(dirname));
  int wd;

  if(argc != 2) {
    return enif_make_badarg(env);
  }

  if( !enif_get_resource(
                         env, argv[0],
                         erlinotify_nif_RESOURCE,
                         (void**) &state)) {
    return enif_make_badarg(env);
  }

  if (enif_get_string(env, argv[1], dirname, sizeof(dirname),
                      ERL_NIF_LATIN1) < 1) {
      return enif_make_badarg(env);
  }

  wd = inotify_add_watch(state->fd, dirname, IN_ALL_EVENTS);
  if (wd < 0) {
    ret = enif_make_tuple2(env, enif_make_atom(env, "error"),
                           enif_make_string(env, "unable to add watch",
                                            ERL_NIF_LATIN1)
                           );
  }
  else {
    ret = enif_make_tuple2(env, enif_make_atom(env, "ok"),
                           enif_make_int(env, wd) );
  }
  return ret;
}

static ERL_NIF_TERM
erlinotify_nif_remove_watch(ErlNifEnv* env,
                         int argc,
                         const ERL_NIF_TERM argv[])
{
  ERL_NIF_TERM ret;
  state_t* state;
  int wd;
  int r;

  if(argc != 2) {
    return enif_make_badarg(env);
  }

  if( !enif_get_resource(
                         env, argv[0],
                         erlinotify_nif_RESOURCE,
                         (void**) &state)) {
    return enif_make_badarg(env);
  }

  if (!enif_get_int(env, argv[1], &wd)) {
      return enif_make_badarg(env);
  }

  r = inotify_rm_watch (state->fd, wd);
  if (r < 0) {
    ret = enif_make_tuple2(env, enif_make_atom(env, "error"),
                           enif_make_string(env, "unable to remove watch",
                                            ERL_NIF_LATIN1)
                           );
  }
  else {
    ret = enif_make_atom(env, "ok");
  }
  return ret;
}

int
read_events(ErlNifEnv* env, void* obj)
{
  state_t* state = (state_t*) obj;
  char buffer[EVENT_BUF_LEN];
  int buffer_i = 0, length;
  int count = 0;
  ERL_NIF_TERM msg;

  length = read (state->fd, buffer, EVENT_BUF_LEN);
  if (length <= 0) return length;

  while (buffer_i < length) {
    struct inotify_event *pevent = (struct inotify_event *) &buffer[buffer_i];
    buffer_i +=  EVENT_SIZE + pevent->len;
    /* fprintf(stderr, */
    /*         "inotify_erlang:note_read  len: %d idx: %d event %d %x %x %s %d\r\n", */
    /*         length, buffer_i, pevent->wd, pevent->mask, pevent->cookie, pevent->name, pevent->len); */
    ERL_NIF_TERM name;
    if (pevent->len) {
      name = enif_make_string(env, pevent->name, ERL_NIF_LATIN1);
    }
    else {
      name = enif_make_string(env, "", ERL_NIF_LATIN1);
    }
    msg = enif_make_tuple6(env,
                           enif_make_atom(env, "inotify_event"),
                           enif_make_int(env, pevent->wd),
                           atom_file_type(env, pevent->mask),
                           atom_event(env, pevent->mask),
                           enif_make_int(env, pevent->cookie),
                           name
                           );
    enif_send(NULL, &(state->pid), env, msg);
    enif_clear_env(env);
    count++;
  }
  return count;

}

static void*
thr_main(void* obj)
{
    state_t* state = (state_t*) obj;
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(state->fd, &rfds);
    ErlNifEnv* env = enif_alloc_env();
    /* loop will terminate as soon as main thread calls close(state->fd) */
    while (0 < select(FD_SETSIZE, &rfds, NULL, NULL, NULL)) {
      read_events(env, state);
    }
    enif_free_env(env);
    /* tell the VM that we are done now. */
    enif_thread_exit(NULL);
    return NULL;
}

static ERL_NIF_TERM
atom_file_type(ErlNifEnv* env, ulong mask) {
  if ( mask & IN_ISDIR ) {
    return enif_make_atom(env, "dir");
  }
  else {
    return enif_make_atom(env, "file");
  }
}


static ERL_NIF_TERM
atom_event(ErlNifEnv* env, ulong mask) {
  if (mask & IN_ACCESS) {
    return enif_make_atom(env, "access");
  }
  if (mask & IN_ATTRIB) {
    return enif_make_atom(env, "attribute");
  }
  if (mask & IN_CLOSE_WRITE) {
    return enif_make_atom(env, "close_write");
  }
  if (mask & IN_CLOSE_NOWRITE) {
    return  enif_make_atom(env, "close_nowrite");
  }
  if (mask & IN_CREATE) {
    return enif_make_atom(env, "create");
  }
  if (mask & IN_DELETE) {
    return enif_make_atom(env, "delete");
  }
  if (mask & IN_DELETE_SELF) {
    return enif_make_atom(env, "delete_self");
  }
  if (mask & IN_MODIFY) {
    return enif_make_atom(env, "modify");
  }
  if (mask & IN_MOVE_SELF) {
    return enif_make_atom(env, "move_self");
  }
  if (mask & IN_MOVED_FROM) {
    return enif_make_atom(env, "move_from");
  }
  if (mask & IN_MOVED_TO) {
    return enif_make_atom(env, "move_to");
  }
  if (mask & IN_OPEN) {
    return enif_make_atom(env, "open");
  }
  if (mask & IN_IGNORED) {
    return enif_make_atom(env, "ignored");
  }
  if (mask & IN_Q_OVERFLOW) {
    return enif_make_atom(env, "q_overflow");
  }
  if (mask & IN_UNMOUNT) {
    return enif_make_atom(env, "unmount");
  }
  return enif_make_atom(env, "unknown_event");
};

static int
on_load(ErlNifEnv* env, void** priv_data, ERL_NIF_TERM load_info)
{
    ErlNifResourceFlags flags =
      ERL_NIF_RT_CREATE | ERL_NIF_RT_TAKEOVER;
    ErlNifResourceType* rt =
      enif_open_resource_type(env, NULL,
                              "erlinotify_nif_resource",
                              &erlinotify_nif_resource_cleanup,
                              flags, NULL);
    if (rt == NULL)
      return -1;

    erlinotify_nif_RESOURCE = rt;

    return 0;
}
static int
on_upgrade(ErlNifEnv* env, void** priv_data, void** old_priv_data, ERL_NIF_TERM load_info)
{
    return 0;
}

static void
on_unload(ErlNifEnv* env, void* priv_data)
{
}

ERL_NIF_INIT(erlinotify_nif, nif_funcs, &on_load, NULL, &on_upgrade, &on_unload);
