# Utuputki

## 1. What is it ?

Utuputki is a communal LAN-party Screen management software. Users may queue youtube-videos to playlist, which the player then shows on the screen in order.


## 2. Installation

### Dependencies:

The following libraries are needed:
Python3 development libraries
PyParsing
Sqlite3
youtube-dl
VLC

On Debian you can install them like this:
```
apt install python3-dev python3-pyparsing libsqlite3-dev libvlc-dev
```

There's a Debian package of youtube-dl. Don't use it, it's out of date and will fail to download most videos. Use pip3:
```
pip3 install youtube-dl
```


### Compiling

```
cd binaries
make
```

Add -j <n> to taste. Do not use -j when compiling on Raspberry Pi, it uses too much memory.


## 3. Configuration

Utuputki looks for configuration file `utuputki.conf` in current directory. Defaults are used if it's not found. There's an example in `utuputki.conf.dist`.


## 4. Running

Make sure the cache directory exists.

Just run the compiled binary. It's entirely self-contained.

Send SIGINT (Ctrl-C) to terminate. Once terminates at the end of current video, twice terminates immediately.
Use SIGHUP to re-execute instead after update or config change.
Alt-F4 will kill the VLC but doesn't terminate the player.


## 5. Using nginx proxy

DO NOT RUN UTUPUTKI AS ROOT! If you want to run on port 80 use an nginx proxy. There's an example config in `nginx.conf.dist`. It sets the proxy headers so users are counted correctly.
Make sure your forwarders IP address is in utuputki.conf.
The default 127.0.0.1 should be enough


## 6. License

Code is under MIT. See LICENSE in the repository root.

Code copyright by Turo Lamminen 2019

Standby image copyright by [Tuomas Virtanen](https://github.com/katajakasa/) 2015

Foreign code under `foreign` may have different license. Consult their documentation.
