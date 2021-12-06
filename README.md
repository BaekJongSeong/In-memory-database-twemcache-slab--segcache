Twemcache is efficient in memory management because it allocates memory in units of slab, 

which is different  the structure of allocating memory whenever an object is entered. 

However, we would like to analyze memory issues in the case  memory waste occurs due to problems 

such as TTL expiration for each item and the data size is smaller than the specified item space per slab. 

When analyzing, it records the log by uting multiple trace files and records the number of slabs in use for a certain period, 

the number of bytes occupied by the data ed into the slab, the number of expired items.

this code is based on Segcache https://github.com/Thesys-lab/Segcache
---
i modify some code(to print log and to analyze the fragmentation of slab memory) for my research

## research topic: Analysis of wasted memory size after twemcache's slab memory accumulation

i actually understand the thesis(A large scale analysis of hundreds of in-memory cache clusters at Twitter, USENIX OSDI 2020 && Segcache: a memory-efficient and scalable in-memory key-value cache for small objects, USENIX NSDI 2021)

and then i got some question for this twemcache.(description below)
---
---
### first, Internal fragmentation caused by item size of specified size

The item size is the same inside the slab.

At this time, if item with a size smaller than the corresponding item size is inserted, internal fragmentation occurs.

![image](https://user-images.githubusercontent.com/79182947/144772672-c8a1cbc1-1a53-44b3-a14b-8fe28ecdd10e.png)
![image](https://user-images.githubusercontent.com/79182947/144772702-3fde08e3-cb21-442c-a1d9-2b0b17ee4836.png)


### second, Irregularity in handling expired items

Evict is processed immediately in a situation where slab is insufficient

Expireed items are not processed immediately and continue to consume memory in the slab.

This is an attempt to access an object whose TTL has expired and is treated as a cache miss.

![image](https://user-images.githubusercontent.com/79182947/144772731-7909954c-12e2-471a-af96-4f3419289ff3.png)

So I analyzed how much memory wasted in slab(twemcache)
---
i tested three trace file provided by organizer (n.sbin, c.sbin, u2.sbin)


![image](https://user-images.githubusercontent.com/79182947/144752537-2f4dbf88-c072-44a5-84e2-8c50a2df902b.png)


my presentation is available here https://softcon.ajou.ac.kr/works/works.asp?uid=479&category=M
