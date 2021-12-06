
#include "bench_storage.h"
#include "reader.h"
#include <storage/slab/item.h>
#include <storage/slab/slab.h>
#include <cc_queue.h>

#include <cc_debug.h>
#include <cc_define.h>
#include <cc_log.h>
#include <cc_mm.h>
#include <time/cc_timer.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <sched.h>
#include <pthread.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#define N_MAX_THREAD        128
#define THREAD_PIN

volatile static bool        start = false;
volatile static bool        stop = false;
static char                 val_array[MAX_VAL_LEN];
static int                  n_thread;
static struct reader        *readers[N_MAX_THREAD];
static int                  time_speedup;
volatile int64_t            op_cnt[op_invalid];
volatile static uint64_t    n_req = 0;
volatile static uint64_t    n_get_req = 0;
volatile static uint64_t    n_miss = 0;

static delta_time_i         default_ttls[100];

#define BENCHMARK_OPTION(ACTION)                                                                    \
    ACTION(trace_path,      OPTION_TYPE_STR,    NULL,           "path to the trace")                        \
    ACTION(default_ttl_list,OPTION_TYPE_STR,    "86400:1",      "a comma separated list of ttl:percent")    \
    ACTION(n_thread,        OPTION_TYPE_UINT,   1,              "the number of threads")                    \
    ACTION(debug_logging,   OPTION_TYPE_BOOL,   true,           "turn on debug logging")                    \
    ACTION(time_speedup,    OPTION_TYPE_UINT,   1,              "speed up the time in replay")              \


struct replay_specific {
    BENCHMARK_OPTION(OPTION_DECLARE)
};

struct benchmark_options {
    struct replay_specific benchmark;
    debug_options_st debug;
    struct option engine[]; /* storage-engine specific options... */
};
typedef struct benchmark_options bench_options_st;


static rstatus_i
benchmark_create(struct benchmark *b, const char *config)
{
    cc_memset(val_array, 'A', MAX_VAL_LEN);
//    for (int i = 0; i < MAX_VAL_LEN; i++)
//        val_array[i] = (char)('A' + i % 26);

    unsigned               n_opts_all, n_opts_bench, n_opts_dbg, n_opts_storage;
    struct replay_specific bench_opts = {BENCHMARK_OPTION(OPTION_INIT)};
    debug_options_st       debug_opts = {DEBUG_OPTION(OPTION_INIT)};

    n_opts_bench   = OPTION_CARDINALITY(struct replay_specific);
    n_opts_dbg     = OPTION_CARDINALITY(debug_options_st);
    n_opts_storage = bench_storage_config_nopts();
    n_opts_all     = n_opts_bench + n_opts_dbg + n_opts_storage;

    b->options = cc_alloc(sizeof(struct option) * n_opts_all);
    ASSERT(b->options != NULL);

    option_load_default((struct option *) &bench_opts, n_opts_bench);
    option_load_default((struct option *) &debug_opts, n_opts_dbg);

    BENCH_OPTS(b)->benchmark          = bench_opts;
    BENCH_OPTS(b)->debug              = debug_opts;
    bench_storage_config_init(BENCH_OPTS(b)->engine);

    if (config != NULL) {
        FILE *fp = fopen(config, "r");
        if (fp == NULL) {
            printf("cannot open config %s\n", config);
            exit(EX_CONFIG);
        }
        option_load_file(fp, (struct option *) b->options, n_opts_all);
        fclose(fp);
    }

    if (O_BOOL(b, debug_logging)) {
        if (debug_setup(&(BENCH_OPTS(b)->debug)) != CC_OK) {
            log_stderr("debug log setup failed");
            exit(EX_CONFIG);
        }
    }
    char         *list_start   = O_STR(b, default_ttl_list);
    char         *curr         = list_start;
    char         *new_pos;
    delta_time_i ttl;
    double       perc;
    int          ttl_array_idx = 0;
    while (curr != NULL) {
        ttl     = strtol(curr, &new_pos, 10);
        curr    = new_pos;
        new_pos = strchr(curr, ':');
        ASSERT(new_pos != NULL);
        curr       = new_pos + 1;
        perc       = strtod(curr, &new_pos);
        for (int i = 0; i < (int) (perc * 100); i++) {
            default_ttls[ttl_array_idx + i] = ttl;
        }
        ttl_array_idx += (int) (perc * 100);
        printf("find TTL %"PRId32 ": perc %.4lf, ", ttl, perc);
        curr    = new_pos;
        new_pos = strchr(curr, ',');
        curr    = new_pos == NULL ? NULL : new_pos + 1;
    }
    printf("\n");

    if (ttl_array_idx != 100) {
        ASSERT(ttl_array_idx == 99);
        default_ttls[99] = default_ttls[98];
    }

    n_thread     = O_UINT(b, n_thread);
    time_speedup = O_UINT(b, time_speedup);

    if (n_thread > 1) {
        char     path[MAX_TRACE_PATH_LEN];
        for (int i = 0; i < n_thread; i++) {
            sprintf(path, "%s.%d", O_STR(b, trace_path), i);
            printf("what the path means? %s\n",path);
            readers[i] = open_trace(path, default_ttls);
            readers[i]->reader_id = i;
            if (readers[i] == NULL) {
                printf("failed to open trace %s\n", path);
                exit(EX_CONFIG);
            }
        }
    } else {
        printf("what the path means? %s\n",O_STR(b, trace_path));
        readers[0] = open_trace(O_STR(b, trace_path), default_ttls);
        readers[0]->reader_id = 0;
        if (readers[0] == NULL) {
            printf("failed to open trace %s\n", O_STR(b, trace_path));
            exit(EX_CONFIG);
        }
    }

    return CC_OK;
}

static void
benchmark_destroy(struct benchmark *b)
{
    cc_free(b->options);
    for (int i = 0; i < n_thread; i++) {
        close_trace(readers[i]);
    }
}

FILE* use_slab;
extern struct slab_heapinfo heapinfo; 

static void check_slab2(void)
{int cc_count = 0;uint32_t ait_count=0; uint32_t erase_count=0;
 int slab_count=0; uint64_t total_bytes=0;uint64_t wasted=0;
    
    // slab_lruq를 지속적으로 돌면서
    struct slab* slab = TAILQ_FIRST(&heapinfo.slab_lruq);
if (slab!=NULL)//(slab->initialized==1)
{
     for (; slab != NULL; slab = TAILQ_NEXT(slab, s_tqe)) {
    slab_count += 1;    
    int idx = 0;
    struct slabclass *p;
    p = &slabclass[slab->id];

        for(size_t j=31; j<1048576; j+=p->size)//slab_profile[slab->id] // 해당 slab의 item size만큼을 계속 더해서 가다가 item header init 안되어있으면 break. 그 이후로는 검사할 필요도 없음
        {struct item* it = _slab_to_item(slab,idx,p->size);//slab_profile[slab->id]);
            if (it->is_linked == 1)
            {uint64_t len_ = item_ntotal(it->klen,it->vlen,it->olen);

            if (len_ > p->size) {erase_count+=1;continue;}

            ait_count += 1; 
            total_bytes+=len_;
            if(_item_expired(it))
                {//size_t len_ = item_ntotal(it->klen,it->vlen,it->olen);
                cc_count+=1; wasted+=len_;}
                }
        idx+=1;
        }
    }
}
fprintf(use_slab,"%d ", slab_count);
fprintf(use_slab,"%u %llu ", ait_count,total_bytes);
fprintf(use_slab,"%d %llu ", cc_count,wasted);//\n", cc_count,wasted);
fprintf(use_slab,"%u\n", erase_count);
}


static struct duration
trace_replay_run(void)
{
    struct reader *reader = readers[0];
    reader->update_time = false;
    struct benchmark_entry *e = reader->e;

FILE* fp3;
fp3 = fopen("./1000_log/print_log.txt", "a");
use_slab = fopen("./1000_log/1000_1125_log_nsbin.txt","a"); 
uint32_t aka_put_count = 2;uint32_t aka_del_count = 1;

    struct duration d;
    duration_start(&d);
    rstatus_i status;

    read_trace(reader);
    e->op = op_set;
    run_op(e);
    e->op = op_get;
    run_op(e);
    e->op = op_delete;
    run_op(e);
    e->op = op_get;
    run_op(e);

    int32_t last_print = 0;
    while (read_trace(reader) == 0) {
        proc_sec = reader->curr_ts * time_speedup;
        if (time_proc_sec() % 3600 == 0 && time_proc_sec() != last_print) {
            last_print = time_proc_sec();
//            printf("curr sec %d\n", time_proc_sec());
        }
        if (e->op == op_incr || e->op == op_decr) {
            e->op = op_get;
        } else if (e->op == op_add || e->op == op_replace || e->op == op_cas) {
            ;aka_put_count+=1;
        }
        else if (e->op == op_delete){aka_del_count+=1;}
        else if (e->op == op_set){aka_put_count+=1;}

        if(n_req % 500000 == 0){check_slab2();
        }

        status = run_op(e);
        op_cnt[e->op] += 1;

        if (e->op == op_get) {
            n_get_req += 1;

            if (status == CC_EEMPTY) {
                n_miss += 1;
                if (e->val_len != 0) {
                    op_cnt[op_set] += 1;
                    e->op = op_set;
                    run_op(e);
                    n_req += 1;aka_put_count+=1;
                }
            }
        }

        n_req += 1;
    }

    duration_stop(&d);
    fprintf(fp3,"this is log for 1000 1125 final slab program n.sbin\n"); //2000, 4000
    fprintf(fp3,"total_request: %llu ",n_req);
    fprintf(fp3,"cache_miss_count: %llu\n",n_miss);
    fprintf(fp3,"put_request: %u ",aka_put_count);
    fprintf(fp3,"get_request: %llu\n",n_get_req);
    fclose(fp3);
    fclose(use_slab);
//        printf("metrics evict %ld merge %ld\n",
//                seg_metrics->seg_evict.gauge,
//                seg_metrics->seg_merge.gauge);

//    for (int i = 0; i < 1000; i++)
//        seg_print_warn(i);

    return d;
}

static void *
_time_update_thread(void *arg)
{
#ifdef __APPLE__
    pthread_setname_np("time");
#else
    pthread_setname_np(pthread_self(), "time");
#endif

    proc_sec = 0;
    bool stop_local = __atomic_load_n(&stop, __ATOMIC_RELAXED);
    while (!stop_local) {
        int32_t min_ts = readers[0]->curr_ts;
        for (int i = 0; i < n_thread; i++) {
            if (readers[i]->curr_ts < min_ts) {
                min_ts = readers[i]->curr_ts;
            }
        }
        if (proc_sec < min_ts) {
            __atomic_store_n(&proc_sec, min_ts, __ATOMIC_RELAXED);
            if (min_ts % 3600 == 0) {
                printf("curr sec %d\n", min_ts);
            }
        }
        proc_sec = min_ts;
        usleep(2000);
        stop_local = __atomic_load_n(&stop, __ATOMIC_RELAXED);
    }

    printf("end time %d\n", proc_sec);
    return NULL;
}


static void *
_trace_replay_thread(void *arg)
{
    static __thread uint64_t local_n_miss = 0;
    static __thread uint64_t local_n_get_req = 0;
    static __thread uint64_t local_n_req = 0;
    static __thread uint64_t local_op_cnt[op_invalid] = {0};

    uint64_t idx = (uint64_t) arg;

#if defined(THREAD_PIN) && !defined(__APPLE__)
      /* bind worker to the core */
      cpu_set_t cpuset;
      pthread_t thread = pthread_self();

      CPU_ZERO(&cpuset);
      CPU_SET(idx, &cpuset);

      if (pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset) != 0) {
          printf("fail to bind worker thread to core %lu: %s\n",
                 idx, strerror(errno));
      } else {
        printf("binding worker thread to core %lu\n", idx);
      }

#endif

      char thread_name[16];
      snprintf(thread_name, 16, "replay_%lu", (unsigned long) idx);

#ifdef __APPLE__
    pthread_setname_np(thread_name);
#else
    pthread_setname_np(pthread_self(), thread_name);
#endif

    struct reader *reader = readers[idx];
    struct benchmark_entry *e = reader->e;

    rstatus_i status;


    while (!start) {
        ;
    }

    while (read_trace(reader) == 0) {
        if (e->op == op_incr || e->op == op_decr || e->op == op_add ||
                e->op == op_replace || e->op == op_cas) {
            e->op = op_set;
        }

        // if (e->op != op_get)
        //     e->op = op_get;

        status = run_op(e);
        local_op_cnt[e->op] += 1;

        if (e->op == op_get) {
            local_n_get_req += 1;

            if (status == CC_EEMPTY) {
                local_n_miss += 1;

                if (e->val_len != 0) {
                    local_op_cnt[op_set] += 1;
                    e->op = op_set;
                    run_op(e);
                    local_n_req += 1;
                }
            }
        }

        local_n_req += 1;
    }

    __atomic_add_fetch(&n_req, local_n_req, __ATOMIC_RELAXED);
    __atomic_add_fetch(&n_get_req, local_n_get_req, __ATOMIC_RELAXED);
    __atomic_add_fetch(&n_miss, local_n_miss, __ATOMIC_RELAXED);
    for (int i = 0; i < op_invalid; i++) {
        __atomic_add_fetch(&op_cnt[i], local_op_cnt[i], __ATOMIC_RELAXED);
    }

    return NULL;
}

static struct duration
trace_replay_run_mt(struct benchmark *b)
{
    pthread_t time_update_tid;
    pthread_t pids[N_MAX_THREAD];

    pthread_create(&time_update_tid, NULL, _time_update_thread, NULL);

    for (int i = 0; i < n_thread; i++) {
        readers[i]->update_time = false;
        pthread_create(&pids[i], NULL, _trace_replay_thread,
            (void *) (unsigned long) i);
    }

    /* wait for eval thread ready */
    sleep(1);
    start = true;

    struct duration d;
    duration_start(&d);

    for (int i = 0; i < n_thread; i++) {
        pthread_join(pids[i], NULL);
    }
    duration_stop(&d);

    stop = true;
    pthread_join(time_update_tid, NULL);

    return d;
}


int
main(int argc, char *argv[])
{
#ifdef __APPLE__
    pthread_setname_np("main");
#else
    pthread_setname_np(pthread_self(), "main");
#endif

    struct benchmark b;
    struct duration d;
    if (benchmark_create(&b, argv[1]) != 0) {
        loga("failed to create benchmark instance");
        return -1;
    }

    bench_storage_init(BENCH_OPTS(&b)->engine, 0, 0);

    if (n_thread == 1) {
        d = trace_replay_run();
    } else {
        d = trace_replay_run_mt(&b);
    }

    printf("%s total benchmark runtime: %.2lf s, throughput %.2lf M QPS\n",
            argv[1], duration_sec(&d), n_req / duration_sec(&d) / 1000000);
    printf("average operation latency: %.2lf ns, miss ratio %.4lf\n",
            duration_ns(&d) / n_req, (double)n_miss / n_get_req);

    for (op_e op = op_get; op < op_invalid; op++) {
        if (op_cnt[op] == 0)
            continue;
        printf("op %16s %16"PRIu64 "(%.4lf)\n", op_names[op], op_cnt[op],
                (double)op_cnt[op]/n_req);
    }

    benchmark_destroy(&b);
    bench_storage_deinit();

    return 0;
}

