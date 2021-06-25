# PintOS

Welcome! This repository contains code for UC Berkeley's Operating Systems Course (CS 162) group project (Summer 2020). The course website for the academic term in which this project was implemented is available [here](https://inst.eecs.berkeley.edu/~cs162/su20/info/).
  
PintOS is a fully functional, educating operating system written in C. This repository was refactored and expanded from a skeleton code starter with very basic functionality. Our task was to improve and add to the functionality of the OS via three stages.

## Part 1
Our main objective for the first part of the project was to implement support for user programs. We did this by:
1.  Enabling argument passing to user-level processes. This allowed user processes to recognize the separate parts of a command. For example, running a command like ```ls -al``` passed ```ls``` as the first argument, and ```-al``` as the second to the program. 

2.  Implementing process control and file operation syscalls. There were a total of 13 syscalls that we had to implement, to be used in both kernel and user-level programs. These syscalls included ones like ```exec```, ```halt```, ```wait``` for process control, and syscalls like ```create```, ```open```, ```filesize```, ```read```, and ```write``` for file operations.

If you want to see the full spec for this stage, you can do so [here]().

## Part 2
The second task was mainly concerned with thread scheduling via priority donation. During this stage, synchronization was one of the chief concerns. We had to implement our own version of a lock, a semaphore, and a condition variable, and configure the OS so that accessing shared resources would not result in race conditions. We also had to implement priority donation for threads, which allows a high-priority thread to temporarily give or donate its priority to a lower-priority thread to avoid deadlock. 

View the full spec for this part [here]().

In addition to the main task, we had to submit a mathematical exercise concerning various scheduling algorithms in order to examine how different approaches produce different results under various workloads. You can see the guidelines for this sub-task [here]().

## Part 3