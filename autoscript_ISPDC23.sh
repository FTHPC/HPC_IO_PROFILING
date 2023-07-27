#!/bin/bash

mkdir -p /dev/shm/veloc
declare -a N_THREADS=(2)
declare -a N_FILES=(8 3 1)
declare -a N_NODES=(32)

tol=20
export dir_path=$HOME/"example_dir/"
export filename1=$1
for it in {0..5}
do
        for nodes in ${N_NODES[@]}
        do
                for files in ${N_FILES[@]}
                do
                        for threads in ${N_THREADS[@]}
                        do
                                rm /dev/shm/veloc/*
                                rm $dir_path/*
                                echo "./fileIO_bench $dir_path $filename1 $tol $nodes $threads $files"
                                ./fileIO_bench $dir_path $filename1 $tol $nodes $threads $files

                                rm /dev/shm/veloc/*
                                rm $dir_path/*
                                echo "./fileIO_bench $dir_path $filename1 $tol $nodes $threads $files -i true"
                                ./fileIO_bench $dir_path $filename1 $tol $nodes $threads $files -i true

                        done
                done
        done
done
