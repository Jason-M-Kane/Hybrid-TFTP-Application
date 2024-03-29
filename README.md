# Hybrid-TFTP-Application
Software developed for 2010 URI Masters Thesis:  
J. Kane, ““Development of a Hybrid TFTP Application,” The University of Rhode Island, Rhode Island, 2010  

This is a proposed enhanced version of the UDP-based TFTP protocol, allowing for directory retrieval, fast multi-threaded downloads, and file verification between two computers on a local network.  By running simultaneous TFTP-like threads and re-merging the data on the client side of the connection, the Ethernet dead time imposed by TFTP’s stop and wait algorithm is effectively minimized.    
Applications were designed for Windows and come with MSVC 2010 .sln files  


Server Information  
==================  
**Usage: tftp_server.exe [port] [max_num_connections] [sharepath] [readBufferSizeBytes]**  
(defaults are 69, 100, C:\share\, 4194304)  
Note:  If running server and client on the same PC, some Winsock 10054 errors may be reported at the end of each successful transfer when the client closes the connection.  These can be ignored.  


Client Information
==================
**Usage: tftp_client.exe (No parameters, Winapi GUI Program)**  

Configurable parameters (recompile required):  
From clientMain.cpp:  
1.) Number of maximum ongoing client connections - MAX_SIMULT_CONNECTIONS  
2.) Number of connections to use for each download (should be <= MAX_SIMULT_CONNECTIONS) - DESIRED_CONNECT_PER_DL  


