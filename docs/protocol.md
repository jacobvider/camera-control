# slcamd wire protocol (v2)

`slcamd` speaks a line-oriented ASCII protocol over TCP (default port **6972**).
It is simple enough to drive by hand with `nc`, and strict enough to parse
reliably from code.

```console
$ printf 'status\n' | nc -q1 localhost 6972
OK state=idle exptime=1.000 shutter=normal gain=high temp=-60.00 setpoint=-60.00 locked=1 temp_age=0.0 progress=0.000 elapsed=0.00 frames=0 last_file=- last_error=-
```

## Framing

* One request per line, terminated by `\n` (`\r\n` is accepted). Maximum line length: 4096 bytes.
* One reply line per request, in order. Requests may be pipelined.
* Connections are persistent; send `quit` (or just close the socket) when done.
  A final request without a trailing newline is still processed if the client
  half-closes the socket.
* Idle connections are closed after `server.idle_timeout_s` (default 600 s).
* Blank lines are ignored.

## Requests

```
command [positional ...] [option=value ...]
```

* Command and option names are case-insensitive.
* The legacy form `command=value` (e.g. `exptime=5`) used by the original
  server is accepted and treated as `command value`.
* Values cannot contain whitespace.

## Replies

```
OK [payload]
ERR <code> <human-readable message>
```

Structured payloads are space-separated `key=value` pairs. Values are
**percent-encoded** (space, `%`, `=`, `,` and non-printable bytes), and an
empty value is sent as `-`. Lists (e.g. burst file names) are comma-separated,
with each element percent-encoded.

### Error codes

| Code               | Meaning                                                        | Retry? |
|--------------------|----------------------------------------------------------------|--------|
| `bad_request`      | Malformed line, wrong number of arguments, line too long       | no     |
| `unknown_command`  | No such command                                                | no     |
| `invalid_argument` | Argument has the wrong type or an unknown option was given     | no     |
| `out_of_range`     | Argument outside the configured `[limits]`                     | no     |
| `busy`             | An exposure is running (or too many clients are connected)     | yes    |
| `camera_error`     | The camera/SDK reported a failure                              | maybe  |
| `storage_error`    | The FITS file could not be written                             | maybe  |
| `unauthorized`     | Authentication required or token wrong                         | no     |
| `not_permitted`    | Command disabled by configuration (e.g. remote `shutdown`)     | no     |
| `internal_error`   | Bug in the server; please report it                            | no     |

## Commands

Units: seconds for time, degrees Celsius for temperature.

| Command | Reply payload | Notes |
|---|---|---|
| `ping` | `pong` | No authentication needed. |
| `help` | `commands=<list> docs=...` | |
| `version` | `version=0.1.0 protocol=2` | |
| `info` | `model= serial= sensor= width= height= simulated= backend= version=` | |
| `status` | `state= exptime= shutter= gain= temp= setpoint= locked= temp_age= progress= elapsed= frames= last_file= last_error=` | Never blocks. During an exposure the temperature is the cached value; `temp_age` says how old it is. `state` is `idle`, `exposing` or `writing`. |
| `exptime [S]` | `S` with 3 decimals | Get/set the default exposure time. Allowed during an exposure (applies to the next frame). |
| `shutter [normal\|closed\|open]` | mode | `1`/`2`/`3` are accepted as aliases. Alias: `shutter_mode`. |
| `gain [low\|medium\|high]` | level | `1`/`2`/`3` are accepted as aliases. Alias: `analog_gain`. The server reads the value back from the camera and reports `camera_error` if it did not change. |
| `temp [C]` | `reading= setpoint= locked=` | With an argument, sets the set point (legacy behaviour). |
| `setpoint [C]` | set point with 2 decimals | Get/set the sensor temperature set point. |
| `expose [exptime=S] [object=NAME]` | `file=<path> type=object` | Uses the current shutter mode. `exptime` overrides the default for this frame only. Blocks until the file is written. |
| `dark [exptime=S]` | `file=<path> type=dark` | Shutter forced closed. |
| `bias` | `file=<path> type=bias` | 0 s, shutter forced closed. |
| `burst N [exptime=S] [object=NAME]` | `count=N files=a,b,...` | `N` science frames back to back (max `limits.max_burst`). Stops at the first failure. |
| `auth TOKEN` | `authenticated` | Required first when `server.auth_token` is set. Per connection. |
| `quit` | `bye` | Server closes the connection. |
| `shutdown` | `shutting down` | Only if `server.allow_remote_shutdown = true`. Alias: `exit`. |

`object` names may contain letters, digits and `_ - + . :` (max 64 characters).

## Concurrency

Each connection is served by its own thread. Only one exposure can run at a
time: a second `expose`/`dark`/`bias`/`burst`, or any command that changes
hardware state (`gain`, `setpoint`), returns `ERR busy` while it is running.
`status`, `temp`, `exptime`, `shutter`, `info` and `ping` stay responsive, so a
scheduler can open a second connection to monitor progress.

## Example session

```text
> auth s3cret
< OK authenticated
> exptime 30
< OK 30.000
> gain medium
< OK medium
> expose object=GRB261105A
< OK file=/var/lib/superlotis/data/20261105/superlotis_20261105T041502_object_0001.fits type=object
> burst 3 exptime=10
< OK count=3 files=/var/lib/.../..._0002.fits,/var/lib/.../..._0003.fits,/var/lib/.../..._0004.fits
> setpoint -200
< ERR out_of_range temperature set point must be between -80.000 and 90.000 C
> quit
< OK bye
```

## Changes from the original server (protocol v1)

| v1 | v2 |
|---|---|
| `exptime=<ms>` | `exptime <seconds>` (a v1 value like `5000` is now rejected as out of range rather than silently taking 83 minutes) |
| one command per connection | persistent connections, pipelining |
| free-text replies (`Exposure complete.`) | `OK`/`ERR <code>` with machine-readable payloads |
| fixed output file `exposure_file` | unique, dated FITS file returned in the reply |
| `status`, `genfits` advertised but unimplemented | `status` implemented; FITS is written directly (no raw step) |
