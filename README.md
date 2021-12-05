this code is based on Segcache https://github.com/Thesys-lab/Segcache
---
i modify some code for my research

research topic: Analysis of wasted memory size after twemcache's slab memory accumulation

i actually understand the thesis(A large scale analysis of hundreds of in-memory cache clusters at Twitter, USENIX OSDI 2020 && Segcache: a memory-efficient and scalable in-memory key-value cache for small objects, USENIX NSDI 2021)

and then i got some question for this twemcache.(description below)
---
---
Twemcache is efficient in memory management because it allocates memory in units of slab, 

which is different  the structure of allocating memory whenever an object is entered. 

However, we would like to analyze memory issues in the case  memory waste occurs due to problems 

such as TTL expiration for each item and the data size is smaller than the specified item space per slab. 

When analyzing, it records the log by uting multiple trace files and records the number of slabs in use for a certain period, 

the number of bytes occupied by the data ed into the slab, the number of expired items.
