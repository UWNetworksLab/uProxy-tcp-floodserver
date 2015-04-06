# uproxy-tcp-floodserver
A TCP server that floods the client with random data.  Useful for testing TCP clients.

Build it  with `./build.sh` and see the options for running it with `./server -h`.  

The buffer size controls how large the buffer given to write(2) is.  If you see (dt = 1.00) very evenly, and wonder if you could get a higher transfer rate, try a larger buffer size.  If your variance in transfer rate is too high, then try a smaller one.  The default size is reasonable.  The max is 1 Gb.

## Example usage

Start the server:   
```sh
  $ ./server -r 100000 -X 1000000000 -p 1227  
  Transmitting at a maximum rate of 100,000 bytes/sec.
  Transmitting a max of 1,000,000,000 bytes before auto-closing socket.
  Reading random data into I/O buffer of 4,096 bytes...done
  Listening to port 1227
```
And then run:
```sh
  $ nc localhost 1224 >/dev/null
```
To connect to the server. It will show you your transmision rate. It'll auto-terminate after sending a gigabyte over (as specified by the -X flag above). If you don't use the -r limit, you can see how fast your machine is!

## Language and format setup

For the commas in numbers, set `LANG` to something reasonable, like:
```sh
  $ export LANG=en_US.UTF-8
```
