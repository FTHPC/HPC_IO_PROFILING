# HPC_IO_PROFILING
This benchmark was developed as a low cost effort to evaluate different I/O parameters for the purpose of aggregated file I/O for different file systems.

In this benchmark we assume a producer-consumer style data exchange, where-in "non-leaders" (or simulated nodes) will send a specified amount of data to the leader (the compute node). Our simulation assumes that (1) "received" data would be put onto a queue, where writer threads will pull from the top and write chunks of a given size to the aggregated file. And (2) that "receiving" data will always be faster than writing to the PFS, and unihibited by other network congestion. Thus, we do not simulate scenarios in which writer threads may need to wait for data from receiver threads.

The benchmark is designed to use 1 compute node, representing and I/O leader. The user is able to tune various parameters, such as the number of files to aggregate to, the number of writer threads to spawn, and can control the contiguity of the data assigned to each thread (e.g. interleaved or contiguous)--the benchmark defaults to a contiguous write pattern (e.g. each writer threads writes to a contiguous region of the aggregated file), to change this to an interleaved pattern, pass a flag -i in the command line. The user may assign any number of follower nodes to "send" data to the I/O leader, and may alter the size of chunks to "receive" from non-leaders.

For more information on our benchmark, please look for our short paper in the International Symposium on Parallel and Distributed Computing 2023 (ISPDC23) proceedings

Use of this benchmark requires the following libraries: <br />
Cmake <br />
Boost <br />
OpenMP <br />

To build the benchmark, simply run the following commands:
mkdir build <br />
cd build <br />
cmake .. <br />
make <br />

To execute the benchmark, we provide an example bash script for reference, the first execution runs a contiguous write pattern and the second execution uses an interleaved write pattern. 
