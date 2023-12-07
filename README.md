# -RupCS_OS_Final_Project

## Operating System Course Final Project

The assignment is written in C.

The assignment Describes vessels sailing from Haifa port to Eilat port, and back, passing in the suez canal.

A vessel starts its journey in Haifa port with goods on-board, and it wants to Unload its goods in Eilat port.
each vessel that comes into Eilat port, has to wait in an unloading queue for an available crane - that is reserved only to this particular vessel durin unloading.
after unloading finishes, the vessel goes back to haifa throu the suez canal.

-Each direction (e.g trip from hifa to eilat) is an Anonymous PIPE (One way).
-Eeach vessel is a Thread.
-Each crane is a thread.
-Eilat's unloading quay is in fact an ADT that the access to it, is regulated using a "Barrier".
The barrier is basically a "Synchronization point" for all independent vessel threads.
