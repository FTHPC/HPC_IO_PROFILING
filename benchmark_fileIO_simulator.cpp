#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sys/stat.h>
#include <string>
#include <fstream>
#include <iostream>
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <omp.h>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <vector>
#include <new>
// this examples uses asserts so they need to be activated
#undef NDEBUG
//#define DEBUG
#include <boost/program_options.hpp>
namespace po = boost::program_options;


const size_t PROC_SIZE = 1 << 30;
const size_t MAX_BUFF_SIZE = 1 << 26;
const int PPN = 8;
std::mutex output_mutex;

bool local_file_transfer_loop(int fs, ssize_t soff, int fd, ssize_t doff, ssize_t remaining) {
    bool success = true;
    unsigned char *buff = new unsigned char[MAX_BUFF_SIZE];
    while (remaining > 0) {
	ssize_t buff_rem = (doff % MAX_BUFF_SIZE) != 0 ? MAX_BUFF_SIZE - (doff % MAX_BUFF_SIZE) : MAX_BUFF_SIZE;
	ssize_t transferred = pread(fs, buff, std::min((size_t)buff_rem, (size_t)remaining), soff);
        if (transferred == -1 || pwrite(fd, buff, transferred, doff) != transferred) {
            success = false;
            break;
        }
        remaining -= transferred;
	soff += transferred;
	doff += transferred;
    }
    fsync(fd);
    delete []buff;
    return success;
}

bool recv_file_transfer_loop(void *buff, int fd, ssize_t doff, ssize_t remaining) {
    bool success = true;
    while (remaining > 0) {
        ssize_t transferred = pwrite(fd, buff, remaining, doff);
        if(transferred != remaining) {
            success = false;
            break;
        }
        remaining -= transferred;
        doff += transferred;
    }
    fsync(fd);
    return success;
}

void generate_sizes(const int N, const int t_count, const double tol, ssize_t *rank_size){
    ssize_t total = t_count * N * PROC_SIZE;
    srand48(112244L);
    for (int i = 0; i < t_count * N; i++) {
        rank_size[i] = (ssize_t)ceil(PROC_SIZE * (1 + (drand48() - 0.5) * 2 * tol));
        total -= (ssize_t)rank_size[i];
    }
}

ssize_t get_offset(int r_normalizer, int local_files_per_remote, int f_id, ssize_t *rank_size){
    ssize_t off = 0;
    #ifdef DEBUG
    std::cout << "getting offset for file id: " << f_id << "; remote beginner = " << r_normalizer * local_files_per_remote << std::endl;
    #endif
    for(int i = (r_normalizer * local_files_per_remote); i < f_id; i++)
	    off += rank_size[i];
    return off;
}

int main(int argc, char *argv[]) {
	std::string dir_path = argv[1];
    std::string filename = argv[2];
    double tol = (double)atoi(argv[3])/100;
    int N = atoi(argv[4]);
    int t_count = atoi(argv[5]);
    int N_FILES = atoi(argv[6]);
    bool interleaved = false;
    int threads_per_file = std::ceil((double)t_count / N_FILES);
    int remote_files_per_thread = std::ceil((double) N_FILES / t_count);
    po::options_description desc("options");
    desc.add_options()
            ("interleaved,i", po::value<bool>(&interleaved), "set interleave flag");
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);
    if(vm.count("interleaved"))
            interleaved = true;

    ssize_t rank_size[N * PPN];
    generate_sizes(N, PPN, tol, rank_size);
    ssize_t local_total = 0, recv_total = 0;
    for(int i = 0; i < PPN; i++)
        local_total += rank_size[i];
    for(int i = PPN; i < N * PPN; i ++)
        recv_total += rank_size[i];
    ssize_t ckpt_total = local_total + recv_total;
    ssize_t recv_total_per_file = std::ceil((double)recv_total / N_FILES);
	int local_files_per_thread = std::ceil((double)PPN/t_count);

    std::cout << "starting threads, # local files per thread = " << local_files_per_thread << "; # aggregated files  = " << N_FILES << " # threads per aggregated file = " << threads_per_file <<  std::endl;
    #pragma omp parallel for num_threads(PPN)
    for (int i = 0; i < PPN; i++) {
        unsigned char *buff = new unsigned char[rank_size[omp_get_thread_num()] * sizeof(unsigned char)];
        assert(buff != NULL);
        memset(buff, 0, static_cast<size_t>(rank_size[omp_get_thread_num()]));
        std::string fs = "/dev/shm/veloc/"+filename+std::to_string(omp_get_thread_num())+".dat";
        int fi = open(fs.c_str(), O_CREAT | O_WRONLY, 0644);
        if (fi == -1) {
            close(fi);
            delete[] buff;
            exit(-1);
        }
        ssize_t remaining = rank_size[omp_get_thread_num()];
        ssize_t off = 0;
        bool success = true;
        while(remaining > 0){
            size_t transfer = std::min(MAX_BUFF_SIZE, (size_t)remaining);
            if(pwrite(fi, buff, transfer, (off_t)off) != (ssize_t)transfer) {
                success = false;                    
                break;
            }
            remaining -= (ssize_t)transfer;
            off += (ssize_t)transfer;
        }
        close(fi);
        if(!success)
        exit(-1);
    }
    #pragma omp barrier


    std::vector<double> local_thruputs(t_count);
    std::vector<double> remote_thruputs(t_count);
    double local_timer = 0, recv_timer = 0, max_agg_timer = 0, min_agg_timer = 0;
    #pragma omp parallel for num_threads(t_count) reduction(max:local_timer, recv_timer, max_agg_timer) reduction(min: min_agg_timer)
    for (int i = 0; i < t_count; i++) {
	    int t_id = omp_get_thread_num();
        int fi[PPN] = {-1};
        for(int i = 0; i < PPN; i++) {
            fi[i] = open(("/dev/shm/veloc/"+filename+std::to_string(i)+".dat").c_str(), O_CREAT | O_RDONLY, 0644);
            if (fi[i] == -1) {
            std::cout << "failed to open local file" << std::endl;
                exit(-1);
                for(int j = 0; j < i; j++)
                    close(fi[j]);
            }
        }
        int fo[N_FILES] = {-1};
        for(int i = 0; i < N_FILES; i++) {
            fo[i] = open((dir_path+filename+std::to_string(i)+".dat").c_str(), O_CREAT | O_WRONLY, 0644);
            if (fo[i]== -1) {
                std::cout << "failed to open remote file" << std::endl;
                for(int j = 0; j < PPN; j++)
                    close(fi[j]);
                for(int j = 0; j < i; j++)
                    close(fo[j]);
                exit(-1);
            }
        }

        //SIMULATE RECVS
        char *buff = new char[MAX_BUFF_SIZE];
        assert(buff != NULL);
        memset(buff, (char)omp_get_thread_num(), MAX_BUFF_SIZE);
        int num_passes = 0;
        bool success = true;
        const ssize_t t_recv_total_per_file = std::ceil((double)recv_total_per_file / threads_per_file);
        ssize_t off = t_recv_total_per_file * (omp_get_thread_num() % threads_per_file);
        int remote_f_id = std::floor(omp_get_thread_num() / threads_per_file) * remote_files_per_thread;
	double start = omp_get_wtime();
	ssize_t recv_rem = t_recv_total_per_file * remote_files_per_thread;
	while(recv_rem > 0 && success != false) {
            if(interleaved)
                off = MAX_BUFF_SIZE * ((omp_get_thread_num() % threads_per_file) + (num_passes * threads_per_file));
			#ifdef DEBUG
            if(num_passes >= 0 && num_passes <= 2) {
                std::unique_lock<std::mutex> cout_lck(output_mutex);
                std::cout << "tid " << omp_get_thread_num() << ": writing recv data at offset: " << off << " to remote file " << remote_f_id << std::endl;
                cout_lck.unlock();
            }
			#endif
            size_t transfer = std::min(MAX_BUFF_SIZE, (size_t)recv_rem);
            success = recv_file_transfer_loop(buff, fo[remote_f_id], off, transfer);
            recv_rem -= transfer;
            off += transfer;
	    	num_passes++;
            if(off > recv_total_per_file) {
                remote_f_id++;
                num_passes = 0;
				off = t_recv_total_per_file * (omp_get_thread_num() % threads_per_file);
            }
		}
        recv_timer = omp_get_wtime() - start;
        remote_thruputs[t_id] = (double)((std::ceil((double)recv_total_per_file / threads_per_file)/PROC_SIZE)/recv_timer);


        int local_files_per_remote_file = std::ceil((double)PPN / N_FILES);
		int local_starting_fid = t_id * local_files_per_thread;
		start = omp_get_wtime();
	    for(int i = local_starting_fid; i < local_starting_fid + local_files_per_thread && i < PPN; i++) {
            int remote_f_id = std::floor(i / local_files_per_remote_file);
			#ifdef DEBUG
			std::unique_lock<std::mutex> cout_lck(output_mutex);
			std::cout << "tid " << omp_get_thread_num() << ": i =  " << i << "; local_files_per_remote_file = " <<local_files_per_remote_file <<  
				"; remote_files_per_thread = " << remote_files_per_thread << "; remote_f_id = " << remote_f_id << std::endl;
			cout_lck.unlock();
			#endif
			ssize_t d_off = recv_total_per_file + get_offset(remote_f_id, local_files_per_remote_file, i, rank_size);
			success = local_file_transfer_loop(fi[i], 0, fo[remote_f_id], d_off, rank_size[i]);
			#ifdef DEBUG
            cout_lck.lock();
            std::cout << "transferred local file " << i << " to remote file " << remote_f_id << "at offset: " << d_off << std::endl;
            cout_lck.unlock();
			#endif
        }

		local_timer = omp_get_wtime() - start;
		local_thruputs[t_id] = (double)((double)rank_size[i]/PROC_SIZE/local_timer);
		max_agg_timer = recv_timer + local_timer;
		min_agg_timer = recv_timer + local_timer;
        for(auto &i : fi) 
            close(i);
        for(auto &i : fo) 
            close(i);  
    }

    std::ofstream outputfile;
	outputfile.open(filename+".txt", std::ofstream::out | std::ofstream::app);
	if (outputfile.tellp() == 0) 
		outputfile << "STRATEGY,NODES,PROCS PER NODE,NUM_THREADS,N_FILES,TOLERANCE,CKPT SIZE [GB],MIN TIME [s],MAX TIME [s],LOCAL THROUGHPUT [GB/s],REMOTE THROUGHPUT [GB/s],MAX AGG THRUPUT [GB/s]" << std::endl;
	std::string strat = "contiguous";
	if(interleaved)
		strat = "interleaved";
	outputfile <<  strat << "," << N << "," << PPN << "," << t_count << "," << N_FILES << "," << tol*100 << "," << (double)ckpt_total/PROC_SIZE << 
		"," << min_agg_timer << "," << max_agg_timer << "," << (double)((double)ckpt_total/PROC_SIZE/max_agg_timer) << std::endl;
	outputfile.close();
    return 0;
}
