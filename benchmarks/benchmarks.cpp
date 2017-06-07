// ©2013-2014 Cameron Desrochers.
// Distributed under the simplified BSD license (see the LICENSE file that
// should have come with this file).

// Benchmarks for moodycamel::ConcurrentQueue.
// Provides comparative timings of various operations under
// highly artificial circumstances. You've been warned :-)

#include <cstdio>
#include <cstring>
#include <string>
#include <cstdint>
#include <cmath>
#include <cstdarg>
#include <fstream>
#include <ctime>
#include <random>
#include <vector>
#include <map>
#include <cassert>
#include <thread>
#include <algorithm>
#include <cctype>

#include "../concurrentqueue.h"
#include "lockbasedqueue.h"
#include "simplelockfree.h"
#include "boostqueue.h"
#include "tbbqueue.h"
#include "stdqueue.h"
#include "../tests/common/simplethread.h"
#include "../tests/common/systemtime.h"
#include "cpuid.h"

using namespace moodycamel;


typedef std::minstd_rand RNG_t;

bool precise = false;


// Visual Studio 2012 is still supported, and it does not support constexpr, so
// we need to use this old way of determining an array size

template<typename T>
struct array_size;

template<typename T, std::size_t n>
struct array_size<T[n]> : std::integral_constant<std::size_t, n> {};


enum benchmark_type_t
{
	bench_balanced,
	bench_only_enqueue,
	bench_only_enqueue_prealloc,
	bench_only_enqueue_bulk,
	bench_only_enqueue_bulk_prealloc,
	bench_only_dequeue,
	bench_only_dequeue_bulk,
	bench_mostly_enqueue,
	bench_mostly_enqueue_bulk,
	bench_mostly_dequeue,
	bench_mostly_dequeue_bulk,
	bench_spmc,
	bench_spmc_preproduced,
	bench_mpsc,
	bench_empty_dequeue,
	bench_enqueue_dequeue_pairs,
	bench_heavy_concurrent,
	
	BENCHMARK_TYPE_COUNT
};

const std::string BENCHMARK_SHORT_NAMES[BENCHMARK_TYPE_COUNT] = {
	"balanced",
	"only_enqueue",
	"only_enqueue_prealloc",
	"only_enqueue_bulk",
	"only_enqueue_bulk_prealloc",
	"only_dequeue",
	"only_dequeue_bulk",
	"mostly_enqueue",
	"mostly_enqueue_bulk",
	"mostly_dequeue",
	"mostly_dequeue_bulk",
	"spmc",
	"spmc_preproduced",
	"mpsc",
	"empty_dequeue",
	"enqueue_dequeue_pairs",
	"heavy_concurrent"
};

const char BENCHMARK_NAMES[BENCHMARK_TYPE_COUNT][64] = {
	"balanced",
	"only enqueue",
	"only enqueue (pre-allocated)",
	"only enqueue bulk",
	"only enqueue bulk (pre-allocated)",
	"only dequeue",
	"only dequeue bulk",
	"mostly enqueue",
	"mostly enqueue bulk",
	"mostly dequeue",
	"mostly dequeue bulk",
	"single-producer, multi-consumer",
	"single-producer, multi-consumer (pre-produced)",
	"multi-producer, single-consumer",
	"dequeue from empty",
	"enqueue-dequeue pairs",
	"heavy concurrent"
};

const char BENCHMARK_DESCS[BENCHMARK_TYPE_COUNT][256] = {
	"Measures the average operation speed with multiple symmetrical threads\n  under reasonable load -- small random intervals between accesses",
	"Measures the average operation speed when all threads are producers",
	"Measures the average operation speed when all threads are producers,\n  and the queue has been stretched out first",
	"Measures the average speed of enqueueing an item in bulk when all threads are producers",
	"Measures the average speed of enqueueing an item in bulk when all threads are producers,\n  and the queue has been stretched out first",
	"Measures the average operation speed when all threads are consumers",
	"Measures the average speed of dequeueing an item in bulk when all threads are consumers",
	"Measures the average operation speed when most threads are enqueueing",
	"Measures the average speed of enqueueing an item in bulk under light contention",
	"Measures the average operation speed when most threads are dequeueing",
	"Measures the average speed of dequeueing an item in bulk under light contention",
	"Measures the average speed of dequeueing with only one producer, but multiple consumers",
	"Measures the average speed of dequeueing from a queue pre-filled by one thread",
	"Measures the average speed of dequeueing with only one consumer, but multiple producers",
	"Measures the average speed of attempting to dequeue from an empty queue\n  (that eight separate threads had at one point enqueued to)",
	"Measures the average operation speed with each thread doing an enqueue\n  followed by a dequeue",
	"Measures the average operation speed with many threads under heavy load"
};

const char BENCHMARK_SINGLE_THREAD_NOTES[BENCHMARK_TYPE_COUNT][256] = {
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"No contention -- measures raw failed dequeue speed on empty queue",
	"No contention -- measures speed of immediately dequeueing the item that was just enqueued",
	""
};

int BENCHMARK_THREADS_MEASURED[BENCHMARK_TYPE_COUNT] = {
	0,	// measures nthreads
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	-1,	// nthreads - 1
	0,
	1,	// 1
	0,
	0,
	0,
};

int BENCHMARK_THREADS[BENCHMARK_TYPE_COUNT][9] = {
	{ 2, 3, 4,  8, 12, 16, 32,  0, 0 },
	{ 1, 2, 4,  8, 12, 16, 32, 48, 0 },
	{ 1, 2, 4,  8, 32,  0,  0,  0, 0 },
	{ 1, 2, 4,  8, 12, 16, 32, 48, 0 },
	{ 1, 2, 4,  8, 32,  0,  0,  0, 0 },
	{ 1, 2, 4,  8, 12, 16, 32, 48, 0 },
	{ 1, 2, 4,  8, 12, 16, 32, 48, 0 },
	{ 2, 4, 8, 32,  0,  0,  0,  0, 0 },
	{ 2, 4, 8, 32,  0,  0,  0,  0, 0 },
	{ 2, 4, 8,  0,  0,  0,  0,  0, 0 },
	{ 2, 4, 8,  0,  0,  0,  0,  0, 0 },
	{ 2, 4, 8, 16,  0,  0,  0,  0, 0 },
	{ 1, 3, 7, 15,  0,  0,  0,  0, 0 },
	{ 2, 4, 8, 16,  0,  0,  0,  0, 0 },
	{ 1, 2, 8, 32,  0,  0,  0,  0, 0 },
	{ 1, 2, 4,  8, 32,  0,  0,  0, 0 },
	{ 2, 3, 4,  8, 12, 16, 32, 48, 0 },
};

enum queue_id_t
{
	queue_moodycamel_ConcurrentQueue,
	queue_boost,
	queue_tbb,
	queue_simplelockfree,
	queue_lockbased,
	queue_std,
};

struct queue_data_t {
	queue_id_t id;
	const char name[64];
	const char notes[128];
	const bool tokenSupport;
	// -1 means no limit
	const int maxThreads;
	const std::vector<benchmark_type_t> benchmarks;
};

const queue_data_t queue_info[] = {
	{
		queue_moodycamel_ConcurrentQueue,
		"moodycamel::ConcurrentQueue",
		"including bulk",
		true,
		-1,
		{
			bench_balanced,
			bench_only_enqueue,
			bench_only_enqueue_prealloc,
			bench_only_enqueue_bulk,
			bench_only_enqueue_bulk_prealloc,
			bench_only_dequeue,
			bench_only_dequeue_bulk,
			bench_mostly_enqueue,
			bench_mostly_enqueue_bulk,
			bench_mostly_dequeue,
			bench_mostly_dequeue_bulk,
			bench_spmc,
			bench_spmc_preproduced,
			bench_mpsc,
			bench_empty_dequeue,
			bench_enqueue_dequeue_pairs,
			bench_heavy_concurrent,
		},
	},
	{
		queue_boost,
		"boost::lockfree::queue",
		"",
		false,
		-1,
		{
			bench_balanced,
			bench_only_enqueue,
			bench_only_enqueue_prealloc,
			bench_only_dequeue,
			bench_mostly_enqueue,
			bench_mostly_dequeue,
			bench_spmc,
			bench_spmc_preproduced,
			bench_mpsc,
			bench_empty_dequeue,
			bench_enqueue_dequeue_pairs,
			bench_heavy_concurrent,
		},
	},
	{
		queue_tbb,
		"tbb::concurrent_queue",
		"",
		false,
		-1,
		{
			bench_balanced,
			bench_only_enqueue,
			bench_only_enqueue_prealloc,
			bench_only_dequeue,
			bench_mostly_enqueue,
			bench_mostly_dequeue,
			bench_spmc,
			bench_spmc_preproduced,
			bench_mpsc,
			bench_empty_dequeue,
			bench_enqueue_dequeue_pairs,
			bench_heavy_concurrent,
		},
	},
	{
		queue_simplelockfree,
		"SimpleLockFreeQueue",
		"",
		false,
		-1,
		{
			bench_balanced,
			bench_only_enqueue,
			bench_only_enqueue_prealloc,
			bench_only_dequeue,
			bench_mostly_enqueue,
			bench_mostly_dequeue,
			bench_spmc,
			bench_spmc_preproduced,
			bench_mpsc,
			bench_empty_dequeue,
			bench_enqueue_dequeue_pairs,
			bench_heavy_concurrent,
		},
	},
	{
		queue_lockbased,
		"LockBasedQueue",
		"",
		false,
		-1,
		{
			bench_balanced,
			bench_only_enqueue,
			bench_only_enqueue_prealloc,
			bench_only_dequeue,
			bench_mostly_enqueue,
			bench_mostly_dequeue,
			bench_spmc,
			bench_spmc_preproduced,
			bench_mpsc,
			bench_empty_dequeue,
			bench_enqueue_dequeue_pairs,
			bench_heavy_concurrent,
		},
	},
	{
		queue_std,
		"std::queue",
		"single thread only",
		false,
		1,
		{
			bench_only_enqueue,
			bench_only_enqueue_prealloc,
			bench_only_dequeue,
			bench_empty_dequeue,
			bench_enqueue_dequeue_pairs,
		},
	},
};


struct Traits : public moodycamel::ConcurrentQueueDefaultTraits
{
	// Use a slightly larger default block size; the default offers
	// a good trade off between speed and memory usage, but a bigger
	// block size will improve throughput (which is mostly what
	// we're after with these benchmarks).
	static const size_t BLOCK_SIZE = 64;
};


typedef std::uint64_t counter_t;

const counter_t BULK_BATCH_SIZE = 2300;

struct BenchmarkResult
{
	double elapsedTime;
	counter_t operations;
	
	inline bool operator<(BenchmarkResult const& other) const
	{
		return elapsedTime < other.elapsedTime;
	}
};


template<typename TFunc>
counter_t rampUpToMeasurableNumberOfMaxOps(TFunc const& func, counter_t startOps = 256)
{
	counter_t ops = startOps;
	double time;
	do {
		time = func(ops);
		ops *= 2;
	} while (time < (precise ? 30 : 10));
#ifdef NDEBUG
	return ops / 2;
#else
	return ops / 4;
#endif
}

counter_t adjustForThreads(counter_t suggestedOps, int nthreads)
{
	return std::max((counter_t)(suggestedOps / std::pow(2, std::sqrt((nthreads - 1) * 3))), suggestedOps / 16);
}


template<typename TQueue>
counter_t determineMaxOpsForBenchmark(benchmark_type_t benchmark, int nthreads, bool useTokens, unsigned int randSeed)
{
	switch (benchmark) {
	case bench_balanced: {
		return adjustForThreads(rampUpToMeasurableNumberOfMaxOps([&](counter_t ops) {
			TQueue q;
			RNG_t rng(randSeed * 1);
			std::uniform_int_distribution<int> rand(0, 20);
			double total = 0;
			SystemTime start;
			for (counter_t i = 0; i != ops; ++i) {
				start = getSystemTime();
				q.enqueue(i);
				total += getTimeDelta(start);
			}
			return total;
		}), nthreads);
	}
	case bench_only_enqueue:
	case bench_only_enqueue_prealloc:
	case bench_mostly_enqueue: {
		return adjustForThreads(rampUpToMeasurableNumberOfMaxOps([](counter_t ops) {
			TQueue q;
			auto start = getSystemTime();
			for (counter_t i = 0; i != ops; ++i) {
				q.enqueue(i);
			}
			return getTimeDelta(start);
		}), nthreads);
	}
	case bench_only_dequeue:
	case bench_mostly_dequeue:
	case bench_spmc:
	case bench_spmc_preproduced:
	case bench_mpsc: {
		return adjustForThreads(rampUpToMeasurableNumberOfMaxOps([](counter_t ops) {
			TQueue q;
			for (counter_t i = 0; i != ops; ++i) {
				q.enqueue(i);
			}
			int item;
			auto start = getSystemTime();
			for (counter_t i = 0; i != ops; ++i) {
				q.try_dequeue(item);
			}
			return getTimeDelta(start);
		}), nthreads);
	}
	case bench_only_enqueue_bulk:
	case bench_only_enqueue_bulk_prealloc:
	case bench_mostly_enqueue_bulk: {
		std::vector<counter_t> data;
		for (counter_t i = 0; i != BULK_BATCH_SIZE; ++i) {
			data.push_back(i);
		}
		return adjustForThreads(rampUpToMeasurableNumberOfMaxOps([&](counter_t ops) {
			TQueue q;
			auto start = getSystemTime();
			for (counter_t i = 0; i != ops; ++i) {
				q.enqueue_bulk(data.cbegin(), data.size());
			}
			return getTimeDelta(start);
		}), nthreads);
	}
	case bench_only_dequeue_bulk:
	case bench_mostly_dequeue_bulk: {
		return adjustForThreads(rampUpToMeasurableNumberOfMaxOps([](counter_t ops) {
			TQueue q;
			std::vector<int> data(BULK_BATCH_SIZE);
			for (counter_t i = 0; i != ops; ++i) {
				q.enqueue_bulk(data.cbegin(), data.size());
			}
			auto start = getSystemTime();
			for (counter_t i = 0; i != ops; ++i) {
				q.try_dequeue_bulk(data.begin(), data.size());
			}
			return getTimeDelta(start);
		}), nthreads);
		return 0;
	}
	case bench_empty_dequeue: {
		return adjustForThreads(rampUpToMeasurableNumberOfMaxOps([](counter_t ops) {
			TQueue q;
			int item;
			auto start = getSystemTime();
			for (counter_t i = 0; i != ops; ++i) {
				q.try_dequeue(item);
			}
			return getTimeDelta(start);
		}), nthreads);
	}
	case bench_enqueue_dequeue_pairs: {
		return adjustForThreads(rampUpToMeasurableNumberOfMaxOps([](counter_t ops) {
			TQueue q;
			int item;
			auto start = getSystemTime();
			for (counter_t i = 0; i != ops; ++i) {
				q.enqueue(i);
				q.try_dequeue(item);
			}
			return getTimeDelta(start);
		}), nthreads);
	}
	
	case bench_heavy_concurrent: {
		return adjustForThreads(rampUpToMeasurableNumberOfMaxOps([](counter_t ops) {
			TQueue q;
			int item;
			auto start = getSystemTime();
			for (counter_t i = 0; i != ops; ++i) {
				q.enqueue(i);
				q.try_dequeue(item);
			}
			return getTimeDelta(start);
		}), nthreads);
	}
	
	default:
		assert(false && "Every benchmark type must be handled here!");
		return 0;
	}
}

counter_t determineMaxOpsForBenchmark(queue_id_t queueID, benchmark_type_t benchmark, int nthreads, bool useTokens, unsigned int seed) {
	switch (queueID) {
	case queue_moodycamel_ConcurrentQueue:
		return determineMaxOpsForBenchmark<moodycamel::ConcurrentQueue<int, Traits>>(benchmark, nthreads, useTokens, seed);
	case queue_lockbased:
		return determineMaxOpsForBenchmark<LockBasedQueue<int>>(benchmark, nthreads, useTokens, seed);
	case queue_simplelockfree:
		return determineMaxOpsForBenchmark<SimpleLockFreeQueue<int>>(benchmark, nthreads, useTokens, seed);
	case queue_boost:
		return determineMaxOpsForBenchmark<BoostQueueWrapper<int>>(benchmark, nthreads, useTokens, seed);
	case queue_tbb:
		return determineMaxOpsForBenchmark<TbbQueueWrapper<int>>(benchmark, nthreads, useTokens, seed);
	case queue_std:
		return determineMaxOpsForBenchmark<StdQueueWrapper<int>>(benchmark, nthreads, useTokens, seed);
	default:
		assert(false && "There should be a case here for every queue in the benchmarks!");
	}
}

// Returns time elapsed, in (fractional) milliseconds
template<typename TQueue>
BenchmarkResult runBenchmark(benchmark_type_t benchmark, int nthreads, bool useTokens, unsigned int randSeed, counter_t maxOps, int maxThreads)
{
	BenchmarkResult result;
	volatile int forceNoOptimizeDummy;
	
	switch (benchmark) {
	case bench_balanced: {
		// Measures the average operation speed with multiple symmetrical threads under reasonable load
		TQueue q;
		std::vector<SimpleThread> threads(nthreads);
		std::vector<counter_t> ops(nthreads);
		std::vector<double> times(nthreads);
		std::atomic<int> ready(0);
		for (int tid = 0; tid != nthreads; ++tid) {
			threads[tid] = SimpleThread([&](int id) {
				ready.fetch_add(1, std::memory_order_relaxed);
				while (ready.load(std::memory_order_relaxed) != nthreads)
					continue;
				
				int item;
				SystemTime start;
				RNG_t rng(randSeed * (id + 1));
				std::uniform_int_distribution<int> rand(0, 20);
				ops[id] = 0;
				times[id] = 0;
				typename TQueue::consumer_token_t consTok(q);
				typename TQueue::producer_token_t prodTok(q);
			
				for (counter_t i = 0; i != maxOps; ++i) {
					if (rand(rng) == 0) {
						start = getSystemTime();
						if ((i & 1) == 0) {
							if (useTokens) {
								q.try_dequeue(consTok, item);
							}
							else {
								q.try_dequeue(item);
							}
						}
						else {
							if (useTokens) {
								q.enqueue(prodTok, i);
							}
							else {
								q.enqueue(i);
							}
						}
						times[id] += getTimeDelta(start);
						++ops[id];
					}
				}
			}, tid);
		}
		result.operations = 0;
		result.elapsedTime = 0;
		for (int tid = 0; tid != nthreads; ++tid) {
			threads[tid].join();
			result.operations += ops[tid];
			result.elapsedTime += times[tid];
		}
		int item;
		forceNoOptimizeDummy = q.try_dequeue(item) ? 1 : 0;
		break;
	}
	
	case bench_only_enqueue_prealloc: {
		result.operations = maxOps * nthreads;
		
		TQueue q;
		{
			// Enqueue opcount elements first, then dequeue them; this
			// will "stretch out" the queue, letting implementatations
			// that re-use memory internally avoid having to allocate
			// more later during the timed enqueue operations.
			std::vector<SimpleThread> threads(nthreads);
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid] = SimpleThread([&](int id) {
					if (useTokens) {
						typename TQueue::producer_token_t tok(q);
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue(tok, i);
						}
					}
					else {
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue(i);
						}
					}
				}, tid);
			}
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid].join();
			}
			
			// Now empty the queue
			int item;
			while (q.try_dequeue(item))
				continue;
		}
		
		if (nthreads == 1) {
			// No contention -- measures raw single-item enqueue speed
			auto start = getSystemTime();
			if (useTokens) {
				typename TQueue::producer_token_t tok(q);
				for (counter_t i = 0; i != maxOps; ++i) {
					q.enqueue(tok, i);
				}
			}
			else {
				for (counter_t i = 0; i != maxOps; ++i) {
					q.enqueue(i);
				}	
			}
			result.elapsedTime = getTimeDelta(start);
		}
		else {
			std::vector<SimpleThread> threads(nthreads);
			std::vector<double> timings(nthreads);
			std::atomic<int> ready(0);
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid] = SimpleThread([&](int id) {
					ready.fetch_add(1, std::memory_order_relaxed);
					while (ready.load(std::memory_order_relaxed) != nthreads)
						continue;
					
					auto start = getSystemTime();
					if (useTokens) {
						typename TQueue::producer_token_t tok(q);
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue(tok, i);
						}
					}
					else {
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue(i);
						}
					}
					timings[id] = getTimeDelta(start);
				}, tid);
			}
			result.elapsedTime = 0;
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid].join();
				result.elapsedTime += timings[tid];
			}
		}
		int item;
		forceNoOptimizeDummy = q.try_dequeue(item) ? 1 : 0;
		break;
	}
	
	case bench_only_enqueue: {
		result.operations = maxOps * nthreads;
		
		TQueue q;
		if (nthreads == 1) {
			// No contention -- measures raw single-item enqueue speed
			auto start = getSystemTime();
			if (useTokens) {
				typename TQueue::producer_token_t tok(q);
				for (counter_t i = 0; i != maxOps; ++i) {
					q.enqueue(tok, i);
				}
			}
			else {
				for (counter_t i = 0; i != maxOps; ++i) {
					q.enqueue(i);
				}	
			}
			result.elapsedTime = getTimeDelta(start);
		}
		else {
			std::vector<SimpleThread> threads(nthreads);
			std::vector<double> timings(nthreads);
			std::atomic<int> ready(0);
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid] = SimpleThread([&](int id) {
					ready.fetch_add(1, std::memory_order_relaxed);
					while (ready.load(std::memory_order_relaxed) != nthreads)
						continue;
					
					auto start = getSystemTime();
					if (useTokens) {
						typename TQueue::producer_token_t tok(q);
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue(tok, i);
						}
					}
					else {
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue(i);
						}
					}
					timings[id] = getTimeDelta(start);
				}, tid);
			}
			result.elapsedTime = 0;
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid].join();
				result.elapsedTime += timings[tid];
			}
		}
		int item;
		forceNoOptimizeDummy = q.try_dequeue(item) ? 1 : 0;
		break;
	}
	
	case bench_spmc_preproduced:
	case bench_only_dequeue: {
		result.operations = maxOps * nthreads;
		
		TQueue q;
		{
			// Fill up the queue first
			std::vector<SimpleThread> threads(benchmark == bench_spmc_preproduced ? 1 : nthreads);
			counter_t itemsPerThread = benchmark == bench_spmc_preproduced ? maxOps * nthreads : maxOps;
			for (size_t tid = 0; tid != threads.size(); ++tid) {
				threads[tid] = SimpleThread([&](size_t id) {
					if (useTokens) {
						typename TQueue::producer_token_t tok(q);
						for (counter_t i = 0; i != itemsPerThread; ++i) {
							q.enqueue(tok, i);
						}
					}
					else {
						for (counter_t i = 0; i != itemsPerThread; ++i) {
							q.enqueue(i);
						}
					}
				}, tid);
			}
			for (size_t tid = 0; tid != threads.size(); ++tid) {
				threads[tid].join();
			}
		}
		
		if (nthreads == 1) {
			// No contention -- measures raw single-item dequeue speed
			int item;
			auto start = getSystemTime();
			if (useTokens) {
				typename TQueue::consumer_token_t tok(q);
				for (counter_t i = 0; i != maxOps; ++i) {
					q.try_dequeue(tok, item);
				}
			}
			else {
				for (counter_t i = 0; i != maxOps; ++i) {
					q.try_dequeue(item);
				}	
			}
			result.elapsedTime = getTimeDelta(start);
		}
		else {
			std::vector<SimpleThread> threads(nthreads);
			std::vector<double> timings(nthreads);
			std::atomic<int> ready(0);
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid] = SimpleThread([&](int id) {
					ready.fetch_add(1, std::memory_order_relaxed);
					while (ready.load(std::memory_order_relaxed) != nthreads)
						continue;
					
					int item;
					auto start = getSystemTime();
					if (useTokens) {
						typename TQueue::consumer_token_t tok(q);
						for (counter_t i = 0; i != maxOps; ++i) {
							q.try_dequeue(tok, item);
						}
					}
					else {
						for (counter_t i = 0; i != maxOps; ++i) {
							q.try_dequeue(item);
						}
					}
					timings[id] = getTimeDelta(start);
				}, tid);
			}
			result.elapsedTime = 0;
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid].join();
				result.elapsedTime += timings[tid];
			}
		}
		int item;
		forceNoOptimizeDummy = q.try_dequeue(item) ? 1 : 0;
		break;
	}
	
	case bench_mostly_enqueue: {
		// Measures the average operation speed when most threads are enqueueing
		TQueue q;
		result.operations = maxOps * nthreads;
		std::vector<SimpleThread> threads(nthreads);
		std::vector<double> timings(nthreads);
		auto dequeueThreads = std::max(1, nthreads / 4);
		std::atomic<int> ready(0);
		for (int tid = 0; tid != nthreads - dequeueThreads; ++tid) {
			threads[tid] = SimpleThread([&](int id) {
				ready.fetch_add(1, std::memory_order_relaxed);
				while (ready.load(std::memory_order_relaxed) != nthreads)
					continue;
				
				auto start = getSystemTime();
				if (useTokens) {
					typename TQueue::producer_token_t tok(q);
					for (counter_t i = 0; i != maxOps; ++i) {
						q.enqueue(tok, i);
					}
				}
				else {
					for (counter_t i = 0; i != maxOps; ++i) {
						q.enqueue(i);
					}
				}
				timings[id] = getTimeDelta(start);
			}, tid);
		}
		for (int tid = nthreads - dequeueThreads; tid != nthreads; ++tid) {
			threads[tid] = SimpleThread([&](int id) {
				ready.fetch_add(1, std::memory_order_relaxed);
				while (ready.load(std::memory_order_relaxed) != nthreads)
					continue;
				
				int item;
				auto start = getSystemTime();
				if (useTokens) {
					typename TQueue::consumer_token_t tok(q);
					for (counter_t i = 0; i != maxOps; ++i) {
						q.try_dequeue(tok, item);
					}
				}
				else {
					for (counter_t i = 0; i != maxOps; ++i) {
						q.try_dequeue(item);
					}
				}
				timings[id] = getTimeDelta(start);
			}, tid);
		}
		result.elapsedTime = 0;
		for (int tid = 0; tid != nthreads; ++tid) {
			threads[tid].join();
			result.elapsedTime += timings[tid];
		}
		int item;
		forceNoOptimizeDummy = q.try_dequeue(item) ? 1 : 0;
		break;
	}
	
	case bench_mostly_dequeue: {
		// Measures the average operation speed when most threads are dequeueing
		TQueue q;
		result.operations = maxOps * nthreads;
		std::vector<SimpleThread> threads(nthreads);
		std::vector<double> timings(nthreads);
		auto enqueueThreads = std::max(1, nthreads / 4);
		{
			// Fill up the queue first
			std::vector<SimpleThread> threads(enqueueThreads);
			for (int tid = 0; tid != enqueueThreads; ++tid) {
				threads[tid] = SimpleThread([&](int id) {
					if (useTokens) {
						typename TQueue::producer_token_t tok(q);
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue(tok, i);
						}
					}
					else {
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue(i);
						}
					}
				}, tid);
			}
			for (int tid = 0; tid != enqueueThreads; ++tid) {
				threads[tid].join();
			}
		}
		std::atomic<int> ready(0);
		for (int tid = 0; tid != nthreads - enqueueThreads; ++tid) {
			threads[tid] = SimpleThread([&](int id) {
				ready.fetch_add(1, std::memory_order_relaxed);
				while (ready.load(std::memory_order_relaxed) != nthreads)
					continue;
				
				int item;
				auto start = getSystemTime();
				if (useTokens) {
					typename TQueue::consumer_token_t tok(q);
					for (counter_t i = 0; i != maxOps; ++i) {
						q.try_dequeue(tok, item);
					}
				}
				else {
					for (counter_t i = 0; i != maxOps; ++i) {
						q.try_dequeue(item);
					}
				}
				timings[id] = getTimeDelta(start);
			}, tid);
		}
		for (int tid = nthreads - enqueueThreads; tid != nthreads; ++tid) {
			threads[tid] = SimpleThread([&](int id) {
				ready.fetch_add(1, std::memory_order_relaxed);
				while (ready.load(std::memory_order_relaxed) != nthreads)
					continue;
				
				auto start = getSystemTime();
				if (useTokens) {
					typename TQueue::producer_token_t tok(q);
					for (counter_t i = 0; i != maxOps; ++i) {
						q.enqueue(tok, i);
					}
				}
				else {
					for (counter_t i = 0; i != maxOps; ++i) {
						q.enqueue(i);
					}
				}
				timings[id] = getTimeDelta(start);
			}, tid);
		}
		result.elapsedTime = 0;
		for (int tid = 0; tid != nthreads; ++tid) {
			threads[tid].join();
			result.elapsedTime += timings[tid];
		}
		int item;
		forceNoOptimizeDummy = q.try_dequeue(item) ? 1 : 0;
		break;
	}
	
	case bench_only_enqueue_bulk_prealloc: {
		TQueue q;
		{
			// Enqueue opcount elements first, then dequeue them; this
			// will "stretch out" the queue, letting implementatations
			// that re-use memory internally avoid having to allocate
			// more later during the timed enqueue operations.
			std::vector<SimpleThread> threads(nthreads);
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid] = SimpleThread([&](int id) {
					if (useTokens) {
						typename TQueue::producer_token_t tok(q);
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue(tok, i);
						}
					}
					else {
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue(i);
						}
					}
				}, tid);
			}
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid].join();
			}
			
			// Now empty the queue
			int item;
			while (q.try_dequeue(item))
				continue;
		}
		
		std::vector<counter_t> data;
		for (counter_t i = 0; i != BULK_BATCH_SIZE; ++i) {
			data.push_back(i);
		}
		
		result.operations = maxOps * BULK_BATCH_SIZE * nthreads;
		if (nthreads == 1) {
			auto start = getSystemTime();
			if (useTokens) {
				typename TQueue::producer_token_t tok(q);
				for (counter_t i = 0; i != maxOps; ++i) {
					q.enqueue_bulk(tok, data.cbegin(), data.size());
				}
			}
			else {
				for (counter_t i = 0; i != maxOps; ++i) {
					q.enqueue_bulk(data.cbegin(), data.size());
				}	
			}
			result.elapsedTime = getTimeDelta(start);
		}
		else {
			std::vector<SimpleThread> threads(nthreads);
			std::vector<double> timings(nthreads);
			std::atomic<int> ready(0);
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid] = SimpleThread([&](int id) {
					ready.fetch_add(1, std::memory_order_relaxed);
					while (ready.load(std::memory_order_relaxed) != nthreads)
						continue;
					
					auto start = getSystemTime();
					if (useTokens) {
						typename TQueue::producer_token_t tok(q);
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue_bulk(tok, data.cbegin(), data.size());
						}
					}
					else {
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue_bulk(data.cbegin(), data.size());
						}
					}
					timings[id] = getTimeDelta(start);
				}, tid);
			}
			result.elapsedTime = 0;
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid].join();
				result.elapsedTime += timings[tid];
			}
		}
		int item;
		forceNoOptimizeDummy = q.try_dequeue(item) ? 1 : 0;
		break;
	}
	
	case bench_only_enqueue_bulk: {
		TQueue q;
		std::vector<counter_t> data;
		for (counter_t i = 0; i != BULK_BATCH_SIZE; ++i) {
			data.push_back(i);
		}
		
		result.operations = maxOps * BULK_BATCH_SIZE * nthreads;
		if (nthreads == 1) {
			auto start = getSystemTime();
			if (useTokens) {
				typename TQueue::producer_token_t tok(q);
				for (counter_t i = 0; i != maxOps; ++i) {
					q.enqueue_bulk(tok, data.cbegin(), data.size());
				}
			}
			else {
				for (counter_t i = 0; i != maxOps; ++i) {
					q.enqueue_bulk(data.cbegin(), data.size());
				}	
			}
			result.elapsedTime = getTimeDelta(start);
		}
		else {
			std::vector<SimpleThread> threads(nthreads);
			std::vector<double> timings(nthreads);
			std::atomic<int> ready(0);
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid] = SimpleThread([&](int id) {
					ready.fetch_add(1, std::memory_order_relaxed);
					while (ready.load(std::memory_order_relaxed) != nthreads)
						continue;
					
					auto start = getSystemTime();
					if (useTokens) {
						typename TQueue::producer_token_t tok(q);
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue_bulk(tok, data.cbegin(), data.size());
						}
					}
					else {
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue_bulk(data.cbegin(), data.size());
						}
					}
					timings[id] = getTimeDelta(start);
				}, tid);
			}
			result.elapsedTime = 0;
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid].join();
				result.elapsedTime += timings[tid];
			}
		}
		int item;
		forceNoOptimizeDummy = q.try_dequeue(item) ? 1 : 0;
		break;
	}
	
	case bench_mostly_enqueue_bulk: {
		// Measures the average speed of enqueueing in bulk under light contention
		TQueue q;
		std::vector<counter_t> data;
		for (counter_t i = 0; i != BULK_BATCH_SIZE; ++i) {
			data.push_back(i);
		}
		
		std::vector<SimpleThread> threads(nthreads);
		std::vector<double> timings(nthreads);
		auto dequeueThreads = std::max(1, nthreads / 4);
		std::vector<counter_t> ops(nthreads - dequeueThreads);
		result.operations = maxOps * BULK_BATCH_SIZE * (nthreads - dequeueThreads);	// dequeue ops added after
		std::atomic<int> ready(0);
		for (int tid = 0; tid != nthreads - dequeueThreads; ++tid) {
			threads[tid] = SimpleThread([&](int id) {
				ready.fetch_add(1, std::memory_order_relaxed);
				while (ready.load(std::memory_order_relaxed) != nthreads)
					continue;
				
				auto start = getSystemTime();
				if (useTokens) {
					typename TQueue::producer_token_t tok(q);
					for (counter_t i = 0; i != maxOps; ++i) {
						q.enqueue_bulk(tok, data.cbegin(), data.size());
					}
				}
				else {
					for (counter_t i = 0; i != maxOps; ++i) {
						q.enqueue_bulk(data.cbegin(), data.size());
					}
				}
				timings[id] = getTimeDelta(start);
			}, tid);
		}
		for (int tid = nthreads - dequeueThreads; tid != nthreads; ++tid) {
			threads[tid] = SimpleThread([&](int id, int idBase0) {
				std::vector<int> items(BULK_BATCH_SIZE);
				
				ready.fetch_add(1, std::memory_order_relaxed);
				while (ready.load(std::memory_order_relaxed) != nthreads)
					continue;
				
				counter_t totalOps = 0;
				auto start = getSystemTime();
				if (useTokens) {
					typename TQueue::consumer_token_t tok(q);
					for (counter_t i = 0; i != maxOps; ++i) {
						auto actual = q.try_dequeue_bulk(tok, items.begin(), items.size());
						totalOps += actual + (actual == items.size() ? 0 : 1);
					}
				}
				else {
					for (counter_t i = 0; i != maxOps; ++i) {
						auto actual = q.try_dequeue_bulk(items.begin(), items.size());
						totalOps += actual + (actual == items.size() ? 0 : 1);
					}
				}
				timings[id] = getTimeDelta(start);
				ops[idBase0] = totalOps;
			}, tid, tid - (nthreads - dequeueThreads));
		}
		result.elapsedTime = 0;
		for (int tid = 0; tid != nthreads; ++tid) {
			threads[tid].join();
			result.elapsedTime += timings[tid];
			if (tid < dequeueThreads) {
				result.operations += ops[tid];
			}
		}
		int item;
		forceNoOptimizeDummy = q.try_dequeue(item) ? 1 : 0;
		break;
	}
	
	case bench_only_dequeue_bulk: {
		// Measures the average speed of dequeueing in bulk when all threads are consumers
		TQueue q;
		{
			// Fill up the queue first
			std::vector<int> data(BULK_BATCH_SIZE);
			for (int i = 0; i != BULK_BATCH_SIZE; ++i) {
				data[i] = i;
			}
			std::vector<SimpleThread> threads(nthreads);
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid] = SimpleThread([&](int id) {
					if (useTokens) {
						typename TQueue::producer_token_t tok(q);
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue_bulk(tok, data.cbegin(), data.size());
						}
					}
					else {
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue_bulk(data.cbegin(), data.size());
						}
					}
				}, tid);
			}
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid].join();
			}
		}
		if (nthreads == 1) {
			result.operations = maxOps * BULK_BATCH_SIZE;
			auto start = getSystemTime();
			std::vector<int> items(BULK_BATCH_SIZE);
			if (useTokens) {
				typename TQueue::consumer_token_t tok(q);
				for (counter_t i = 0; i != maxOps; ++i) {
					q.try_dequeue_bulk(tok, items.begin(), items.size());
				}
			}
			else {
				for (counter_t i = 0; i != maxOps; ++i) {
					q.try_dequeue_bulk(items.begin(), items.size());
				}
			}
			result.elapsedTime = getTimeDelta(start);
		}
		else {
			std::vector<SimpleThread> threads(nthreads);
			std::vector<double> timings(nthreads);
			std::vector<counter_t> ops(nthreads);
			std::atomic<int> ready(0);
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid] = SimpleThread([&](int id) {
					std::vector<int> items(BULK_BATCH_SIZE);
					ready.fetch_add(1, std::memory_order_relaxed);
					while (ready.load(std::memory_order_relaxed) != nthreads)
						continue;
					
					counter_t totalOps = 0;
					auto start = getSystemTime();
					if (useTokens) {
						typename TQueue::consumer_token_t tok(q);
						for (counter_t i = 0; i != maxOps; ++i) {
							auto actual = q.try_dequeue_bulk(tok, items.begin(), items.size());
							totalOps += actual + (actual == items.size() ? 0 : 1);
						}
					}
					else {
						for (counter_t i = 0; i != maxOps; ++i) {
							auto actual = q.try_dequeue_bulk(items.begin(), items.size());
							totalOps += actual + (actual == items.size() ? 0 : 1);
						}
					}
					timings[id] = getTimeDelta(start);
					ops[id] = totalOps;
				}, tid);
			}
			result.elapsedTime = 0;
			result.operations = 0;
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid].join();
				result.elapsedTime += timings[tid];
				result.operations += ops[tid];
			}
		}
		int item;
		forceNoOptimizeDummy = q.try_dequeue(item) ? 1 : 0;
		break;
	}
	
	case bench_mostly_dequeue_bulk: {
		// Measures the average speed of dequeueing in bulk under light contention
		TQueue q;
		auto enqueueThreads = std::max(1, nthreads / 4);
		result.operations = maxOps * BULK_BATCH_SIZE * enqueueThreads;
		std::vector<SimpleThread> threads(nthreads);
		std::vector<double> timings(nthreads);
		std::vector<counter_t> ops(nthreads - enqueueThreads);
		std::vector<int> enqueueData(BULK_BATCH_SIZE);
		for (int i = 0; i != BULK_BATCH_SIZE; ++i) {
			enqueueData[i] = i;
		}
		{
			// Fill up the queue first
			std::vector<SimpleThread> threads(enqueueThreads);
			for (int tid = 0; tid != enqueueThreads; ++tid) {
				threads[tid] = SimpleThread([&](int id) {
					if (useTokens) {
						typename TQueue::producer_token_t tok(q);
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue_bulk(tok, enqueueData.cbegin(), enqueueData.size());
						}
					}
					else {
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue_bulk(enqueueData.cbegin(), enqueueData.size());
						}
					}
				}, tid);
			}
			for (int tid = 0; tid != enqueueThreads; ++tid) {
				threads[tid].join();
			}
		}
		std::atomic<int> ready(0);
		for (int tid = 0; tid != nthreads - enqueueThreads; ++tid) {
			threads[tid] = SimpleThread([&](int id) {
				std::vector<int> data(BULK_BATCH_SIZE);
				ready.fetch_add(1, std::memory_order_relaxed);
				while (ready.load(std::memory_order_relaxed) != nthreads)
					continue;
				counter_t totalOps = 0;
				auto start = getSystemTime();
				if (useTokens) {
					typename TQueue::consumer_token_t tok(q);
					for (counter_t i = 0; i != maxOps; ++i) {
						auto actual = q.try_dequeue_bulk(tok, data.begin(), data.size());
						totalOps += actual + (actual == data.size() ? 0 : 1);
					}
				}
				else {
					for (counter_t i = 0; i != maxOps; ++i) {
						auto actual = q.try_dequeue_bulk(data.begin(), data.size());
						totalOps += actual + (actual == data.size() ? 0 : 1);
					}
				}
				timings[id] = getTimeDelta(start);
				ops[id] = totalOps;
			}, tid);
		}
		for (int tid = nthreads - enqueueThreads; tid != nthreads; ++tid) {
			threads[tid] = SimpleThread([&](int id) {
				ready.fetch_add(1, std::memory_order_relaxed);
				while (ready.load(std::memory_order_relaxed) != nthreads)
					continue;
				
				auto start = getSystemTime();
				if (useTokens) {
					typename TQueue::producer_token_t tok(q);
					for (counter_t i = 0; i != maxOps; ++i) {
						q.enqueue_bulk(tok, enqueueData.cbegin(), enqueueData.size());
					}
				}
				else {
					for (counter_t i = 0; i != maxOps; ++i) {
						q.enqueue_bulk(enqueueData.cbegin(), enqueueData.size());
					}
				}
				timings[id] = getTimeDelta(start);
			}, tid);
		}
		result.elapsedTime = 0;
		for (int tid = 0; tid != nthreads; ++tid) {
			threads[tid].join();
			result.elapsedTime += timings[tid];
			if (tid < nthreads - enqueueThreads) {
				result.operations += ops[tid];
			}
		}
		int item;
		forceNoOptimizeDummy = q.try_dequeue(item) ? 1 : 0;
		break;
	}
	
	case bench_spmc: {
		counter_t elementsToDequeue = maxOps * (nthreads - 1);
		
		TQueue q;
		std::vector<SimpleThread> threads(nthreads - 1);
		std::vector<double> timings(nthreads - 1);
		std::vector<counter_t> ops(nthreads - 1);
		std::atomic<bool> lynchpin(false);
		std::atomic<counter_t> totalDequeued(0);
		for (int tid = 0; tid != nthreads - 1; ++tid) {
			threads[tid] = SimpleThread([&](int id) {
				while (!lynchpin.load(std::memory_order_relaxed)) {
					continue;
				}
				
				int item;
				counter_t i = 0;
				auto start = getSystemTime();
				if (useTokens) {
					typename TQueue::consumer_token_t tok(q);
					while (true) {
						if (q.try_dequeue(tok, item)) {
							totalDequeued.fetch_add(1, std::memory_order_relaxed);
						}
						else if (totalDequeued.load(std::memory_order_relaxed) == elementsToDequeue) {
							break;
						}
						++i;
					}
				}
				else {
					while (true) {
						if (q.try_dequeue(item)) {
							totalDequeued.fetch_add(1, std::memory_order_relaxed);
						}
						else if (totalDequeued.load(std::memory_order_relaxed) == elementsToDequeue) {
							break;
						}
						++i;
					}
				}
				timings[id] = getTimeDelta(start);
				ops[id] = i;
			}, tid);
		}
		
		lynchpin.store(true, std::memory_order_seq_cst);
		for (counter_t i = 0; i != elementsToDequeue; ++i) {
			q.enqueue(i);
		}
		
		result.elapsedTime = 0;
		result.operations = 0;
		for (int tid = 0; tid != nthreads - 1; ++tid) {
			threads[tid].join();
			result.elapsedTime += timings[tid];
			result.operations += ops[tid];
		}
		int item;
		forceNoOptimizeDummy = q.try_dequeue(item) ? 1 : 0;
		break;
	}
	
	case bench_mpsc: {
		TQueue q;
		counter_t elementsToDequeue = maxOps * (nthreads - 1);
		std::vector<SimpleThread> threads(nthreads);
		std::atomic<int> ready(0);
		for (int tid = 0; tid != nthreads; ++tid) {
			if (tid == 0) {
				// Consumer thread
				threads[tid] = SimpleThread([&](int id) {
					ready.fetch_add(1, std::memory_order_seq_cst);
					while (ready.load(std::memory_order_relaxed) != nthreads)
						continue;
					
					int item;
					result.operations = 0;
					auto start = getSystemTime();
					if (useTokens) {
						typename TQueue::consumer_token_t tok(q);
						for (counter_t i = 0; i != elementsToDequeue;) {
							i += q.try_dequeue(tok, item) ? 1 : 0;
							++result.operations;
						}
					}
					else {
						for (counter_t i = 0; i != elementsToDequeue;) {
							i += q.try_dequeue(item) ? 1 : 0;
							++result.operations;
						}
					}
					result.elapsedTime = getTimeDelta(start);
				}, tid);
			}
			else {
				threads[tid] = SimpleThread([&](int id) {
					ready.fetch_add(1, std::memory_order_seq_cst);
					while (ready.load(std::memory_order_relaxed) != nthreads)
						continue;
					
					if (useTokens) {
						typename TQueue::producer_token_t tok(q);
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue(tok, i);
						}
					}
					else {
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue(i);
						}
					}
				}, tid);
			}
		}
		
		for (int tid = 0; tid != nthreads; ++tid) {
			threads[tid].join();
		}
		int item;
		forceNoOptimizeDummy = q.try_dequeue(item) ? 1 : 0;
		break;
	}
	
	case bench_empty_dequeue: {
		// Measures the average speed of attempting to dequeue from an empty queue
		TQueue q;
		// Fill up then empty the queue first
		{
			std::vector<SimpleThread> threads(maxThreads > 0 ? maxThreads : 8);
			for (size_t tid = 0; tid != threads.size(); ++tid) {
				threads[tid] = SimpleThread([&](size_t id) {
					if (useTokens) {
						typename TQueue::producer_token_t tok(q);
						for (counter_t i = 0; i != 10000; ++i) {
							q.enqueue(tok, i);
						}
					}
					else {
						for (counter_t i = 0; i != 10000; ++i) {
							q.enqueue(i);
						}
					}
				}, tid);
			}
			for (size_t tid = 0; tid != threads.size(); ++tid) {
				threads[tid].join();
			}
			
			// Empty the queue
			int item;
			while (q.try_dequeue(item))
				continue;
		}
		
		if (nthreads == 1) {
			// No contention -- measures raw failed dequeue speed on empty queue
			int item;
			result.operations = maxOps;
			auto start = getSystemTime();
			if (useTokens) {
				typename TQueue::consumer_token_t tok(q);
				for (counter_t i = 0; i != maxOps; ++i) {
					q.try_dequeue(tok, item);
				}
			}
			else {
				for (counter_t i = 0; i != maxOps; ++i) {
					q.try_dequeue(item);
				}
			}
			result.elapsedTime = getTimeDelta(start);
			forceNoOptimizeDummy = q.try_dequeue(item) ? 1 : 0;
		}
		else {
			result.operations = maxOps * nthreads;
			std::vector<SimpleThread> threads(nthreads);
			std::vector<double> timings(nthreads);
			std::atomic<int> ready(0);
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid] = SimpleThread([&](int id) {
					ready.fetch_add(1, std::memory_order_relaxed);
					while (ready.load(std::memory_order_relaxed) != nthreads)
						continue;
					
					int item;
					auto start = getSystemTime();
					if (useTokens) {
						typename TQueue::consumer_token_t tok(q);
						for (counter_t i = 0; i != maxOps; ++i) {
							q.try_dequeue(tok, item);
						}
					}
					else {
						for (counter_t i = 0; i != maxOps; ++i) {
							q.try_dequeue(item);
						}
					}
					timings[id] = getTimeDelta(start);
				}, tid);
			}
			result.elapsedTime = 0;
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid].join();
				result.elapsedTime += timings[tid];
			}
			int item;
			forceNoOptimizeDummy = q.try_dequeue(item) ? 1 : 0;
		}
		break;
	}
	
	case bench_enqueue_dequeue_pairs: {
		// Measures the average speed of attempting to dequeue from an empty queue
		// (that eight separate threads had at one point enqueued to)
		result.operations = maxOps * 2 * nthreads;
		TQueue q;
		if (nthreads == 1) {
			// No contention -- measures speed of immediately dequeueing the item that was just enqueued
			int item;
			auto start = getSystemTime();
			if (useTokens) {
				typename TQueue::producer_token_t ptok(q);
				typename TQueue::consumer_token_t ctok(q);
				for (counter_t i = 0; i != maxOps; ++i) {
					q.enqueue(ptok, i);
					q.try_dequeue(ctok, item);
				}
			}
			else {
				for (counter_t i = 0; i != maxOps; ++i) {
					q.enqueue(i);
					q.try_dequeue(item);
				}
			}
			result.elapsedTime = getTimeDelta(start);
			forceNoOptimizeDummy = q.try_dequeue(item) ? 1 : 0;
		}
		else {
			std::vector<SimpleThread> threads(nthreads);
			std::vector<double> timings(nthreads);
			std::atomic<int> ready(0);
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid] = SimpleThread([&](int id) {
					ready.fetch_add(1, std::memory_order_relaxed);
					while (ready.load(std::memory_order_relaxed) != nthreads)
						continue;
					
					int item;
					auto start = getSystemTime();
					if (useTokens) {
						typename TQueue::producer_token_t ptok(q);
						typename TQueue::consumer_token_t ctok(q);
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue(ptok, i);
							q.try_dequeue(ctok, item);
						}
					}
					else {
						for (counter_t i = 0; i != maxOps; ++i) {
							q.enqueue(i);
							q.try_dequeue(item);
						}
					}
					timings[id] = getTimeDelta(start);
				}, tid);
			}
			result.elapsedTime = 0;
			for (int tid = 0; tid != nthreads; ++tid) {
				threads[tid].join();
				result.elapsedTime += timings[tid];
			}
			int item;
			forceNoOptimizeDummy = q.try_dequeue(item) ? 1 : 0;
		}
		break;
	}
	
	case bench_heavy_concurrent: {
		// Measures the average operation speed with many threads under heavy load
		result.operations = maxOps * nthreads;
		TQueue q;
		std::vector<SimpleThread> threads(nthreads);
		std::vector<double> timings(nthreads);
		std::atomic<int> ready(0);
		for (int tid = 0; tid != nthreads; ++tid) {
			threads[tid] = SimpleThread([&](int id) {
				ready.fetch_add(1, std::memory_order_relaxed);
				while (ready.load(std::memory_order_relaxed) != nthreads)
					continue;
				
				auto start = getSystemTime();
				if (id < 2) {
					// Alternate
					int item;
					if (useTokens) {
						typename TQueue::consumer_token_t consTok(q);
						typename TQueue::producer_token_t prodTok(q);
						
						for (counter_t i = 0; i != maxOps / 2; ++i) {
							q.try_dequeue(consTok, item);
							q.enqueue(prodTok, i);
						}
					}
					else {
						for (counter_t i = 0; i != maxOps / 2; ++i) {
							q.try_dequeue(item);
							q.enqueue(i);
						}
					}
				}
				else {
					if ((id & 1) == 0) {
						// Enqueue
						if (useTokens) {
							typename TQueue::producer_token_t prodTok(q);
							for (counter_t i = 0; i != maxOps; ++i) {
								q.enqueue(prodTok, i);
							}
						}
						else {
							for (counter_t i = 0; i != maxOps; ++i) {
								q.enqueue(i);
							}
						}
					}
					else {
						// Dequeue
						int item;
						if (useTokens) {
							typename TQueue::consumer_token_t consTok(q);
							for (counter_t i = 0; i != maxOps; ++i) {
								q.try_dequeue(consTok, item);
							}
						}
						else {
							for (counter_t i = 0; i != maxOps; ++i) {
								q.try_dequeue(item);
							}
						}
					}
				}
				timings[id] = getTimeDelta(start);
			}, tid);
		}
		result.elapsedTime = 0;
		for (int tid = 0; tid != nthreads; ++tid) {
			threads[tid].join();
			result.elapsedTime += timings[tid];
		}
		int item;
		forceNoOptimizeDummy = q.try_dequeue(item) ? 1 : 0;
		break;
	}
	
	default:
		assert(false && "Every benchmark type must be handled here!");
		result.elapsedTime = 0;
		result.operations = 0;
	}
	
	(void)forceNoOptimizeDummy;
	
	return result;
}

BenchmarkResult runBenchmark(queue_id_t queueID, benchmark_type_t benchmark, int nthreads, bool useTokens, unsigned int seed, counter_t maxOps, int maxThreads) {
	switch (queueID) {
	case queue_moodycamel_ConcurrentQueue:
		return runBenchmark<moodycamel::ConcurrentQueue<int, Traits>>(benchmark, nthreads, useTokens, seed, maxOps, maxThreads);
	case queue_lockbased:
		return runBenchmark<LockBasedQueue<int>>(benchmark, nthreads, useTokens, seed, maxOps, maxThreads);
	case queue_simplelockfree:
		return runBenchmark<SimpleLockFreeQueue<int>>(benchmark, nthreads, useTokens, seed, maxOps, maxThreads);
	case queue_boost:
		return runBenchmark<BoostQueueWrapper<int>>(benchmark, nthreads, useTokens, seed, maxOps, maxThreads);
	case queue_tbb:
		return runBenchmark<TbbQueueWrapper<int>>(benchmark, nthreads, useTokens, seed, maxOps, maxThreads);
	case queue_std:
		return runBenchmark<StdQueueWrapper<int>>(benchmark, nthreads, useTokens, seed, maxOps, maxThreads);
	default:
		assert(false && "There should be a case here for every queue in the benchmarks!");
	}
}

const char* LOG_FILE = "benchmarks.log";
std::ofstream* logOut;
bool logErrorReported = false;

void sayf(int indent, const char* fmt, ...)
{
	static char indentBuffer[] = "                        ";
	static char buf[2048];
	
	indentBuffer[indent] = '\0';
	
	va_list arglist;
	va_start(arglist, fmt);
	vsprintf(buf, fmt, arglist);
	va_end(arglist);
	
	if (*logOut) {
		(*logOut) << indentBuffer << buf;
	}
	else if (!logErrorReported) {
		std::printf("Note: Error writing to log file. Future output will appear only on stdout\n");
		logErrorReported = true;
	}
	std::printf("%s%s", indentBuffer, buf);
	
	indentBuffer[indent] = ' ';
}


// Returns a formatted timestamp.
// Returned buffer is only valid until the next call.
// Not thread-safe.
static const char* timestamp()
{
	static char buf[32];
	time_t time = std::time(NULL);
	strcpy(buf, std::asctime(std::localtime(&time)));
	buf[strlen(buf) - 1] = '\0';	// Remove trailing newline
	return buf;
}

static inline bool isvowel(char ch)
{
	ch = std::tolower(ch);
	for (const char* v = "aeiou"; *v != '\0'; ++v) {
		if (*v == ch) {
			return true;
		}
	}
	return false;
}

static inline double safe_divide(double a, double b)
{
	return b == 0 ? 0 : a / b;
}

// Returns a positive number formatted in a string in a human-readable way.
// The string is always 7 characters or less (excluding null byte).
// Returned buffer is only valid until the sixteenth next call.
// Not thread safe.
static const char* pretty(double num)
{
	assert(num >= 0);
	
#if defined(_MSC_VER) && _MSC_VER < 1800
	if (!_finite(num)) {
		return "inf";
	}
	if (_isnan(num)) {
		return "nan";
	}
#else
	if (std::isinf(num)) {
		return "inf";
	}
	if (std::isnan(num)) {
		return "nan";
	}
#endif
	
	static char bufs[16][8];
	static int nextBuf = 0;
	char* buf = bufs[nextBuf++];
	nextBuf &= 15;
	
	int suffix = 0;
	if (num < 1) {
		static const char minisufs[] = "\0munpfazy";
		while (num < 0.01) {
			++suffix;
			num *= 1000;
		}
		sprintf(buf, "%1.4f%c", num, minisufs[suffix]);
	}
	else {
		static const char megasufs[] = "\0kMGTPEZY";
		while (num >= 1000) {
			++suffix;
			num /= 1000;
		}
		sprintf(buf, "%.2f%c", num, megasufs[suffix]);
	}
	
	return buf;
}

void printBenchmarkNames()
{
	std::printf("   Supported benchmarks are:\n");
	
	for (int i = 0; i != BENCHMARK_TYPE_COUNT; ++i) {
		std::printf("      %s\n", BENCHMARK_SHORT_NAMES[i].c_str());
	}
}


int main(int argc, char** argv)
{
	// Disable buffering (so that when run in, e.g., Sublime Text, the output appears as it is written)
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	
	// Isolate the executable name
	std::string progName = argv[0];
	auto slash = progName.find_last_of("/\\");
	if (slash != std::string::npos) {
		progName = progName.substr(slash + 1);
	}
	
	std::map<std::string, benchmark_type_t> benchmarkMap;
	for (int i = 0; i != BENCHMARK_TYPE_COUNT; ++i) {
		benchmarkMap.emplace(BENCHMARK_SHORT_NAMES[i], (benchmark_type_t)i);
	}
	std::vector<benchmark_type_t> selectedBenchmarks;
	
	bool showHelp = false;
	bool error = false;
	bool printedBenchmarks = false;
	for (int i = 1; i < argc; ++i) {
		if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
			showHelp = true;
		}
		else if (std::strcmp(argv[i], "-p") == 0 || std::strcmp(argv[i], "--precise") == 0) {
			precise = true;
		}
		else if (std::strcmp(argv[i], "--run") == 0) {
			if (i + 1 == argc || argv[i + 1][0] == '-') {
				std::printf("Expected benchmark name argument for --run option.\n");
				if (!printedBenchmarks) {
					printBenchmarkNames();
					printedBenchmarks = true;
				}
				error = true;
				continue;
			}
			
			auto it = benchmarkMap.find(argv[++i]);
			if (it == benchmarkMap.end()) {
				std::printf("Unrecognized benchmark name '%s'.\n", argv[i]);
				if (!printedBenchmarks) {
					printBenchmarkNames();
					printedBenchmarks = true;
				}
				error = true;
				continue;
			}
			
			selectedBenchmarks.push_back(it->second);
		}
		else {
			std::printf("Unrecognized option '%s'\n", argv[i]);
			error = true;
		}
	}
	if (showHelp || error) {
		if (error) {
			std::printf("\n");
		}
		std::printf("%s\n    Description: Runs benchmarks for moodycamel::ConcurrentQueue\n", progName.c_str());
		std::printf("    --help            Prints this help blurb\n");
		std::printf("    --precise         Generate more precise benchmark results (slower)\n");
		std::printf("    --run benchmark   Runs only the selected benchmark (can be used multiple times)\n");
		return error ? 1 : 0;
	}
	
	bool logExists = true;
	{
		std::ifstream fin(LOG_FILE);
		if (!fin) {
			logExists = false;
		}
	}
	
	std::ofstream fout(LOG_FILE, std::ios::app);
	logOut = &fout;
	if (fout) {
		if (logExists) {
			fout << "\n\n\n";
		}
		fout << "--- New run (" << timestamp() << ") ---\n";
	}
	else {
		std::printf("Note: Error opening log file '%s'. Output will appear only on stdout.\n\n", LOG_FILE);
		logErrorReported = true;
	}
	
	const char* bitStr = "";
	if (sizeof(void*) == 4 || sizeof(void*) == 8) {
		bitStr = sizeof(void*) == 4 ? " 32-bit" : " 64-bit";
	}
	
	const char* cpuStr = getCPUString();
	sayf(0, "Running%s benchmarks on a%s %s\n", bitStr, isvowel(cpuStr[0]) ? "n" : "", cpuStr);
	if (precise) {
		sayf(4, "(precise mode)\n");
	}
	if (selectedBenchmarks.size() > 0) {
		sayf(4, "(selected benchmarks only)\n");
	}
	sayf(0, "Note that these are synthetic benchmarks. Take them with a grain of salt.\n\n");
	
	sayf(0, "Legend:\n");
	sayf(4, "'Avg':     Average time taken per operation, normalized to be per thread\n");
	sayf(4, "'Range':   The minimum and maximum times taken per operation (per thread)\n");
	sayf(4, "'Ops/s':   Overall operations per second\n");
	sayf(4, "'Ops/s/t': Operations per second per thread (inverse of 'Avg')\n");
	sayf(4, "Operations include those that fail (e.g. because the queue is empty).\n");
	sayf(4, "Each logical enqueue/dequeue counts as an individual operation when in bulk.\n");
	sayf(0, "\n");
	
	
#ifdef NDEBUG
	const int ITERATIONS = precise ? 100 : 10;
#else
	const int ITERATIONS = precise ? 20 : 2;
#endif
	
	
	const double FASTEST_PERCENT_CONSIDERED = precise ? 8 : 50;	// Only consider the top % of runs
	
	// Make sure each run of a given benchmark has the same seed (otherwise different runs are not comparable)
	std::srand(std::time(NULL));
	unsigned int randSeeds[BENCHMARK_TYPE_COUNT];
	for (unsigned int i = 0; i != BENCHMARK_TYPE_COUNT; ++i) {
		randSeeds[i] = std::rand() * (i + 1) + 1;
	}
	
	struct weighted_t {
		double opsPerSecondPerThread;
		double total;
	};
	weighted_t weights[array_size<decltype(queue_info)>::value] = {};
	
	auto logicalCores = std::thread::hardware_concurrency();
	
	if (selectedBenchmarks.size() == 0) {
		for (int i = 0; i != BENCHMARK_TYPE_COUNT; ++i) {
			selectedBenchmarks.push_back((benchmark_type_t)i);
		}
	}
	
	int indent = 0;
	for (auto const benchmark : selectedBenchmarks) {
		auto seed = randSeeds[benchmark];
		
		bool anyQueueSupportsBenchmark = false;
		for (auto const & queue : queue_info) {
			auto const it = std::find(queue.benchmarks.begin(), queue.benchmarks.end(), benchmark);
			if (it != queue.benchmarks.end()) {
				anyQueueSupportsBenchmark = true;
				break;
			}
		}
		if (!anyQueueSupportsBenchmark) {
			continue;
		}
		
		sayf(0, "%s", BENCHMARK_NAMES[benchmark]);
		if (BENCHMARK_THREADS_MEASURED[benchmark] != 0) {
			if (BENCHMARK_THREADS_MEASURED[benchmark] < 0) {
				sayf(0, " (measuring all but %d %s)", -BENCHMARK_THREADS_MEASURED[benchmark], BENCHMARK_THREADS_MEASURED[benchmark] == -1 ? "thread" : "threads");
			}
			else {
				sayf(0, " (measuring %d %s)", BENCHMARK_THREADS_MEASURED[benchmark], BENCHMARK_THREADS_MEASURED[benchmark] == 1 ? "thread" : "threads");
			}
		}
		sayf(0, ":\n");
		indent += 2;
		sayf(indent, "(%s)\n", BENCHMARK_DESCS[benchmark]);
		
		for (auto const & queue : queue_info) {
			sayf(indent, "> %s\n", queue.name);
			
			if (std::find(queue.benchmarks.begin(), queue.benchmarks.end(), benchmark) == queue.benchmarks.end()) {
				sayf(indent + 3, "(skipping, benchmark not supported...)\n\n");
				continue;
			}
			
			if (queue.tokenSupport) {
				indent += 4;
			}
			for (bool useTokens : { false, true }) {
				if (queue.tokenSupport) {
					sayf(indent, "%s tokens\n", useTokens ? "With" : "Without");
				}
				if (useTokens && !queue.tokenSupport) {
					continue;
				}
				indent += 3;
				
				std::vector<double> opssts;
				std::vector<int> threadCounts;
				for (int nthreadIndex = 0; BENCHMARK_THREADS[benchmark][nthreadIndex] != 0; ++nthreadIndex) {
					int nthreads = BENCHMARK_THREADS[benchmark][nthreadIndex];
					int measuredThreads = nthreads;
					if (BENCHMARK_THREADS_MEASURED[benchmark] != 0) {
						measuredThreads = BENCHMARK_THREADS_MEASURED[benchmark] < 0 ? nthreads + BENCHMARK_THREADS_MEASURED[benchmark] : BENCHMARK_THREADS_MEASURED[benchmark];
					}
					
					if (logicalCores > 0 && (unsigned int)nthreads > 3 * logicalCores) {
						continue;
					}
					if (queue.maxThreads >= 0 && queue.maxThreads < nthreads) {
						continue;
					}
					
					const counter_t maxOps = determineMaxOpsForBenchmark(queue.id, benchmark, nthreads, useTokens, seed);
					//std::printf("maxOps: %llu\n", maxOps);
					
					int maxThreads = queue.maxThreads;
					std::vector<BenchmarkResult> results(ITERATIONS);
					for (auto & result : results) {
						result = runBenchmark(queue.id, benchmark, nthreads, useTokens, seed, maxOps, maxThreads);
					}
					std::sort(results.begin(), results.end());

					int consideredCount = std::max(2, (int)(ITERATIONS * FASTEST_PERCENT_CONSIDERED / 100));
					
					auto min = std::numeric_limits<double>::max();
					auto max = std::numeric_limits<double>::lowest();
					double ops = 0;
					double time = 0;
					for (int i = 0; i != consideredCount; ++i) {
						const auto & result = results[i];
						const double msPerOperation = safe_divide(result.elapsedTime / 1000.0, (double)result.operations / measuredThreads);
						min = std::min(msPerOperation, min);
						max = std::max(msPerOperation, max);
						time += result.elapsedTime;
						ops += result.operations;
					}
					
					double avg = safe_divide(time / 1000.0, ops / measuredThreads);
					double opsPerSecond = safe_divide(ops, time / 1000.0);
					const double opsPerSecondPerThread = opsPerSecond / (double)measuredThreads;
					
					opssts.push_back(opsPerSecondPerThread);
					threadCounts.push_back(measuredThreads);
					
					sayf(indent, "%-3d %7s:  Avg: %7ss  Range: [%7ss, %7ss]  Ops/s: %7s  Ops/s/t: %7s\n", nthreads, nthreads != 1 ? "threads" : "thread", pretty(avg), pretty(min), pretty(max), pretty(opsPerSecond), pretty(opsPerSecondPerThread));
					if (nthreads == 1 && BENCHMARK_SINGLE_THREAD_NOTES[benchmark][0] != '\0') {
						sayf(indent + 7, "^ Note: %s\n", BENCHMARK_SINGLE_THREAD_NOTES[benchmark]);
					}
				}
				
				double opsPerSecondPerThread = 0;
				double divisor = 0;
				for (size_t i = 0; i != opssts.size(); ++i) {
					auto & w = weights[queue.id];
					opsPerSecondPerThread += opssts[i] * std::sqrt(threadCounts[i]);
					w.opsPerSecondPerThread += opssts[i] * std::sqrt(threadCounts[i]);
					divisor += std::sqrt(threadCounts[i]);
					w.total += std::sqrt(threadCounts[i]);
				}
				opsPerSecondPerThread /= divisor;
				sayf(indent, "Operations per second per thread (weighted average): %7s\n\n", opsPerSecondPerThread == 0 ? "(n/a)" : pretty(opsPerSecondPerThread));
				
				indent -= 3;
			}
			if (queue.tokenSupport) {
				indent -= 4;
			}
		}
		indent -= 2;
	}
	
	sayf(0, "Overall average operations per second per thread (where higher-concurrency runs have more weight):\n");
	sayf(0, "(Take this summary with a grain of salt -- look at the individual benchmark results for a much\nbetter idea of how the queues measure up to each other):\n");
	for (auto const & queue : queue_info) {
		auto const w = weights[queue.id];
		const double opsPerSecondPerThread = safe_divide(w.opsPerSecondPerThread, w.total);
		if (queue.notes[0] != '\0') {
			sayf(4, "%s (%s): %7s\n", queue.name, queue.notes, opsPerSecondPerThread == 0 ? "(n/a)" : pretty(opsPerSecondPerThread));
		}
		else {
			sayf(4, "%s: %7s\n", queue.name, opsPerSecondPerThread == 0 ? "(n/a)" : pretty(opsPerSecondPerThread));
		}
	}
	
	return 0;
}
