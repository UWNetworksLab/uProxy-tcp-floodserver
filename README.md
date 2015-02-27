# uproxy-tcp-floodserver
A TCP server that floods the client with random data.  Useful for testing TCP clients.

Build it, and run ./server -h.  The buffer size controls how large the buffer given to write(2) is.  If you see (dt = 1.00)
very evenly, and wonder if you could get a higher transfer rate, try a larger buffer size.  If your variance in transfer rate
is too high, then try a smaller one.  The default size is reasonable.  The max is 1 Gb.

To test it, run it and then run:
```sh
  $ nc localhost 1224 >/dev/null
```
In another terminal to see your max rate.
