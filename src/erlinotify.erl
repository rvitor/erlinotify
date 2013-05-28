-module(erlinotify).
-behaviour(gen_server).
-define(SERVER, ?MODULE).

-define(log(T),
        error_logger:info_report(
          [process_info(self(),current_function),{line,?LINE},T])).
%% TODO
%% Docs
%% Ets table to store Dir

%% ------------------------------------------------------------------
%% API Function Exports
%% ------------------------------------------------------------------

-export([start_link/0, watch/2, unwatch/1]).

%% ------------------------------------------------------------------
%% gen_server Function Exports
%% ------------------------------------------------------------------

-export([init/1, handle_call/3, handle_cast/2, handle_info/2,
         terminate/2, code_change/3]).

%% ------------------------------------------------------------------
%% Record Definitions
%% ------------------------------------------------------------------

%% @type state() = Record :: #state{ fd = term(),
%%                                   dirnames = ets:tid(),
%%                                   watchdescriptors = ets:tid(),
%%                                   callback= term() }.

-type fd() :: non_neg_integer().
-type callback() :: term().
-record(state, {fd :: fd(), callbacks :: callback(), dirnames :: ets:tid(), watchdescriptors :: ets:tid()}).

%% ------------------------------------------------------------------
%% API Function Definitions
%% ------------------------------------------------------------------

-spec start_link() -> {ok, Pid :: pid()} | ignore | {error, {already_started, Pid :: pid()} | term()}.
start_link() ->
    gen_server:start_link({local, ?SERVER}, ?MODULE, [], []).

% TODO: CB type?
-spec watch(Dirname :: filelib:dirname(), any()) -> ok.
watch(Name, CB) ->
    gen_server:cast(?MODULE, {watch, Name, CB}).

-spec unwatch(Dirname :: filelib:dirname()) -> ok.
unwatch(Name) ->
    gen_server:cast(?MODULE, {unwatch, Name}).

%% ------------------------------------------------------------------
%% gen_server Function Definitions
%% ------------------------------------------------------------------

%%----------------------------------------------------------------------
%% Func: init/1
%% Returns: {ok, State} |
%%          {ok, State, Timeout} |
%%          ignore |
%%          {stop, Reason}
%%----------------------------------------------------------------------

-spec init(term())
    -> {ok, term()} | {ok, term(), non_neg_integer() | infinity} 
    | ignore | {stop, term()}.
init([]) ->
    {ok, Fd} = erlinotify_nif:start(),
    {ok, Ds} = ets_manager:give_me(dirnames),
    {ok, Wds} = ets_manager:give_me(watchdescriptors),
    {ok, CBs} = ets_manager:give_me(callbacks, [bag]),
    {ok, #state{fd=Fd, callbacks=CBs, dirnames = Ds, watchdescriptors=Wds}}.

%%----------------------------------------------------------------------
%% Func: handle_call/3
%% Returns: {reply, Reply, State} |
%%          {reply, Reply, State, Timeout} |
%%          {noreply, State} |
%%          {noreply, State, Timeout} |
%%          {stop, Reason, Reply, State} | (terminate/2 is called)
%%          {stop, Reason, State} (terminate/2 is called)
%%----------------------------------------------------------------------
-spec handle_call(term(), {pid(), term()}, any()) -> {reply, ok, any()}.
handle_call(_Request, _From, State) ->
    {reply, ok, State}.

%%----------------------------------------------------------------------
%% Func: handle_cast/2
%% Returns: {noreply, State} |
%%          {noreply, State, Timeout} |
%%          {stop, Reason, State} (terminate/2 is called)
%%----------------------------------------------------------------------
-spec handle_cast(term(), #state{})
    -> {stop, normal, #state{}} | {noreply, #state{}}.
handle_cast(stop, State) ->
  {stop, normal, State};
handle_cast({watch, Watch, CB}, State) ->
  {noreply, do_watch(Watch, CB, State)};
handle_cast({unwatch, Unwatch}, State) ->
  {noreply, do_unwatch(Unwatch, State)};
handle_cast(Msg, State) ->
  ?log({unknown_message, Msg}),
  {noreply, State}.

%%----------------------------------------------------------------------
%% Func: handle_info/2
%% Returns: {noreply, State} |
%%          {noreply, State, Timeout} |
%%          {stop, Reason, State} (terminate/2 is called)
%%----------------------------------------------------------------------
% TODO: be more specific about Info in spec
-spec handle_info(Info :: term(), #state{}) -> {noreply, #state{}}.
handle_info({inotify_event, _WD, file, ignored, _Cookie, _File} = _Info, State) ->
    %% ignore unwatched messages.
    {noreply, State};
handle_info({inotify_event, Wd, Type, Event, Cookie, Name} = Info, State) ->
  case ets:lookup(State#state.watchdescriptors, Wd) of
      [] -> ?log({unknown_file_watch, Info}),
            {noreply, State};
      [{Wd, File}] ->
            [CB({File, Type, Event, Cookie, Name}) ||
                {_File, CB} <- ets:lookup(State#state.callbacks, File)],
            {noreply, State}
  end;
handle_info({'ETS-TRANSFER', _Tid, _Pid, new_table}, State) ->
    %% log at some point?
    {noreply, State};
handle_info({'ETS-TRANSFER', Tid, _Pid, reissued} = Info, State) ->
    ?log({rewatch_this, Info}),
    case ets:info(Tid, name) of
        dirnames -> case rewatch(State) of
                        {ok, State} -> ok,
                                       {noreply, State};
                        Error -> ?log({error,Error}),
                                 {noreply, State}
                    end;
        _DontCare -> ?log({ignored, Info}),
                     {noreply, State}
    end;
handle_info(Info, State) ->
  ?log({unknown_message, Info}),
  {noreply, State}.

%%----------------------------------------------------------------------
%% Func: terminate/2
%% Purpose: Shutdown the server
%% Returns: any (ignored by gen_server)
%%----------------------------------------------------------------------
-spec terminate(_, #state{}) -> {close, fd()}.
terminate(_Reason, #state{fd = Fd}) ->
    erlinotify_nif:stop(Fd),
    {close, Fd}.

-spec code_change(_, #state{}, _) -> {ok, #state{}}.
code_change(_OldVsn, State, _Extra) ->
    {ok, State}.

%% ------------------------------------------------------------------
%% Internal Function Definitions
%% ------------------------------------------------------------------

%% @spec (Dirname, State) -> state()
%%        State = state()
%%        Dirname = filelib:dirname()
%% @doc Makes a call to the nif to add a resource to
%% watch. Logs on error
-spec do_watch(Dirname :: filelib:dirname(), State :: #state{})
    -> #state{}.
do_watch(Dirname, State) ->
    case erlinotify_nif:add_watch(State#state.fd, Dirname) of
        {ok, Wd} -> ets:insert(State#state.watchdescriptors, {Wd, Dirname}),
                    State;
        Error -> ?log([Error, Dirname]),
                 State
    end.

% TODO: CB type?
-spec do_watch(Dirname :: filelib:dirname(), any(), State :: #state{})
    -> #state{}.
do_watch(Dirname, CB, State) ->
    case erlinotify_nif:add_watch(State#state.fd, Dirname) of
        {ok, Wd} -> ets:insert(State#state.dirnames, {Dirname, Wd}),
                    ets:insert(State#state.callbacks, {Dirname, CB}),
                    ets:insert(State#state.watchdescriptors, {Wd, Dirname}),
                    State;
        Error -> ?log([Error, Dirname]),
                 State
    end.

%% @spec (Dirname, State) -> state()
%%        State = state()
%%        Dirname = filelib:dirname()
%% @doc Makes a call to the nif to remove a resource to
%% watch. Logs on error
-spec do_unwatch(Dirname :: filelib:dirname(), State :: #state{})
    -> #state{}.
do_unwatch(Dirname, State) ->
    case ets:lookup(State#state.dirnames, Dirname) of
        [] -> State;
        [{Dirname,Wd}] ->
            ets:delete(State#state.dirnames, Dirname),
            ets:delete(State#state.callbacks, Dirname),
            ets:delete(State#state.watchdescriptors, Wd),
            erlinotify_nif:remove_watch(State#state.fd, Wd),
            State
    end.

%% @spec (state()) -> ok
%% @doc Rewatch everything in the ets table. Assigning a new
%% Wd as we move through.
rewatch(State) ->
    ets:delete_all_objects(State#state.watchdescriptors),
    Key = ets:first(State#state.dirnames),
    rewatch(State, Key).

rewatch(State, '$end_of_table') ->
    {ok, State};
rewatch(State, Key) ->
    do_watch(Key, State),
    NextKey = ets:next(State#state.dirnames, Key),
    rewatch(State, NextKey).

