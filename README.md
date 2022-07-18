# Hybrid-TFTP-Application
Software developed for URI Masters Thesis “Development of a Hybrid TFTP Application”  
Applications were designed for Windows and come with MSVC 2010 .sln files  


Server Information  
==================  
**Usage: tftp_server.exe [port] [max_num_connections] [sharepath] [readBufferSizeBytes]**  
(defaults are 69, 100, C:\share\, 4194304)  



Client Information
==================
**Usage: tftp_client.exe (No parameters, Winapi GUI Program)**  

Configurable parameters (recompile required):  
From clientMain.cpp:  
1.) Number of maximum ongoing client connections - MAX_SIMULT_CONNECTIONS  
2.) Number of connections to use for each download (should be <= MAX_SIMULT_CONNECTIONS) - DESIRED_CONNECT_PER_DL  


