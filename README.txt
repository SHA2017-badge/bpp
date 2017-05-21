What is this?

This is the bpp update protocol suite hackery thingamajig. It's essentially a protocol encapsulating
multiple streams, sent over UDP over a WiFi network, so clients can pick it up and decode it without
having to connect to the network or transmit anything.

*** Server programs (and related) ***

The server

The server consists of a multiplexing server. It sends out packets to the IP specified on the command
line, and will listen on TCP port 2017 for clients. The server doesn't generate any packets itself;
programs that connect to it will do that and the server will make sure everything is multiplexed,
error-corrected and signed correctly.

bppsource

This is a small library for connecting to the server over port 2017. You can write applications that
talk to the server using this.

blocksend

This is an application that talks to the server and tries to synchronize a file on the host system
to a block device on the client systems. It tries to do this as intelligently as possible, sending
only the blocks that have changed where it can.

hksend

Housekeeping server. For now, indicates only when the next catalog is. A catalog is losely defined
as any protocol information the clients need to decide if they want to download more information or 
sleep until the next catalog.

lyric_test

Simple and stupid server that can send around the contents of a text file, line for line, over and 
over again.

*** Client programs ***

esp32-recv contains an example of the protocol. This app can update itself over the broadcast protocol,
display lyrics, and use deep_sleep to save power. The actual client code is in components/bpp_recv.

*** Misc ***

The keys directory contains keys and a program to generate a new private/public keypair used to sign all
packets of communication.

