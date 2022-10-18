# DirSynchronizer
Directory synchronization between 2 folders(from Source to Replica)
I've tested this only on Windows 11, but it should be working for both windows and linux platforms too.
Command line arguments: 1 - Source folder path. 2 - Replica folder path. 3 - Synchronization interval. 4 - log file path and log file name at the end.
Needed preprocessor commands for windows | _CRT_SECURE_NO_WARNINGS | it's because I've used my time function implementation from Ubuntu based solution and windows is saying that it's not correctly save.
