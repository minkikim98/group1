# PintOS

Welcome! This repository contains code for UC Berkeley's Operating Systems Course (CS 162) group project (Summer 2020). The course website for the academic term in which this project was implemented is available [here](https://inst.eecs.berkeley.edu/~cs162/su20/info/).
  
PintOS is a fully functional, educating operating system written in C. This repository was refactored and expanded from a skeleton code starter with very basic functionality. Our task was to improve and add to the functionality of the OS via three stages.

## Part 1
Our main objective for the first part of the project was to implement support for user programs. We did this by:
1.  Enabling argument passing to user-level processes. This allowed user processes to recognize the separate parts of a command. For example, running a command like ```ls -al``` passed ```ls``` as the first argument, and ```-al``` as the second to the program. 

2.  Implementing process control and file operation syscalls. There were a total of 13 syscalls that we had to implement, to be used in both kernel and user-level programs. These syscalls included ones like ```exec```, ```halt```, ```wait``` for process control, and syscalls like ```create```, ```open```, ```filesize```, ```read```, and ```write``` for file operations.

If you want to see the full spec for this stage, you can do so [here](https://inst.eecs.berkeley.edu/~cs162/su20/static/projects/proj1-userprog.pdf).

## Part 2
The second task was mainly concerned with thread scheduling via priority donation. During this stage, synchronization was one of the chief concerns. We had to implement our own version of a lock, a semaphore, and a condition variable, and configure the OS so that accessing shared resources would not result in race conditions. We also had to implement priority donation for threads, which allows a high-priority thread to temporarily give or donate its priority to a lower-priority thread to avoid deadlock. 

View the full spec for this part [here](https://inst.eecs.berkeley.edu/~cs162/su20/static/projects/proj2-scheduling.pdf).

In addition to the main task, we had to submit a mathematical exercise concerning various scheduling algorithms in order to examine how different approaches produce different results under various workloads. You can see the guidelines for this sub-task [here](https://inst.eecs.berkeley.edu/~cs162/su20/static/projects/proj2-schedlab.pdf).

## Part 3
The third part was mainly concerned with file systems. Our main tasks were:
1. Implement a buffer cache. PintOS uses an inode system to keep track of files. After studying various caching strategies, we were left to implement our own buffer cache with an algorithm of our choice that would speed up read-write access to commonly used file locations. The buffer cache stored individual disk blocks and had to be lower-bounded in runtime. In addition, we had to make sure that all of our file system operations utilized our cache.

2. Enable automatic file extension. If a file grew too large, we had to manually allocate extra memory and assign it to the relevant file descriptor. We used a logarithmic system to determine file extension sizes, increasing the extension size by a factor of 2 every time in order to avoid frequent extensions. We were also required to implement the ```inumber``` syscall, which returns the unique inode number of a particular file descriptor.

3. Add support for subdirectories. Prior to this point, all files in PintOS were stored in the root directory. We had to implement new syscalls such as ```chdir```, ```mkdir```, and ```readdir``` to enable creation of directories, as well as update existing syscalls to work with subdirectories. We also had to ensure that programs could use both absolute and relative pathing when referencing files. 

To view the full spec for this part, click [here](https://inst.eecs.berkeley.edu/~cs162/su20/static/projects/proj3-filesys.pdf).  
  
&nbsp;  
  
# Final Notes
This was probably one of the most challenging projects I have ever worked on. The complexity of the code and the intricacy of the principles involved gave our team a substantial challenge in understanding, reafactoring, and implementing the required features.  
  
However, although it was grueling, this project was also a phenomenal learning experience, and I was able to learn and grow so much as a coder and an engineer. I gained confidence and fluency coding in C, and got comfortable using tools like GDB to debug. I also gained experience working in a technical group setting. We learned how to manage and divide up our workload and how to communicate effectively to work together.  

Overall, I'm very proud of what we were able to accomplish and build over the course of the term. And of course, full credit goes to my other three team members who all contributed to help make this project a success. 
- @Zackoric
- @mabel
- @yishan