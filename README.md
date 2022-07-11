# Hybrid-TFTP-Application
Software developed for URI Masters Thesis “Development of a hybrid TFTP Application”  
Applications were designed for Windows and come with MSVC 2010 .sln files  

Server Information  
==================  
Usage: tftp_server.exe [port] [max_num_connections] [sharepath]  
(defaults are 69,100,C:\share\)  

Configurable parameters (recompile required):  
1.) Disk Read Buffersize - MAX_DISKREAD_BUFFER_SIZE (in tftp_operations.c)  
This parameter is currently set to 2 MB.  The optimal value will vary based on the hard disk system in use.  


Client Information
==================
Usage: tftp_client.exe (No parameters, Winapi GUI Program)  

Configurable parameters (recompile required):  
In clientMain.cpp:  
1.) Number of maximum ongoing client connections - MAX_SIMULT_CONNECTIONS  
2.) Number of connections to use for each download (should be <= MAX_SIMULT_CONNECTIONS) - DESIRED_CONNECT_PER_DL  
In writeFctns.h:  
1.) Number of temp buffers per connection - TEMP_BUFFER_BACKLOG  
2.) Size of the circular write buffer - WRITE_BUFFER_SIZE  
3.) Size of individual disk writes - WRITE_SIZE (must be a multiple of the sector size)  

