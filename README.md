erlinotify
==========

Notification of inotify events

## Description

The library interfaces with inotify using erlang NIF. The NIF spawns
off a thread which then polls the inotify file descriptor for any
changes to a watched descriptor.

## Installation

Add the library to your rebar config

```
{
  deps,
  [
   {erlinotify, ".*", {git, "git://github.com/dipthegeezer/erlinotify.git", "HEAD"}},
   ....
  ]
}.

```

Then just use rebar to get the deps and use in your code.

## Usage

### API

