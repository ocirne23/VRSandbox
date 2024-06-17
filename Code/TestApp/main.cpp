
import Core;

int main()
{
	std::vector<int> vec;
	vec.push_back(1);
	std::array<glm::vec3, 3> arr;
	arr[0] = glm::vec3(1.0f, 2.0f, 3.0f);
	return 0;
}


/*float frandom(float min, float max)
{
	return min + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / (max - min)));
}
int main()
{
	JobScheduler scheduler(4);
	auto startTime = std::chrono::high_resolution_clock::now();
	std::atomic<int> numResults = 0;
	int i = 0;
	for (; i < 30000; i++)
	{
		StackAllocator<2048, 32> allocator;
		//Allocator allocator;

		std::atomic<int> counter = 0;
		//int counter = 0;

		std::vector<Job, decltype(allocator)::toStd<Job>> jobs(allocator);
		for (int j = 0; j < 100; ++j)
		{
			jobs.push_back([&counter](auto&...) -> Job
			{
				counter++;
				co_return;
			}("empty", allocator));
		}

		for (auto& j : jobs)
		{
			scheduler.add(j);
		};

		Job add1Job = [&counter](auto&...) -> Job
		{
			counter += 1;
			//std::this_thread::sleep_for(std::chrono::milliseconds(1));
			co_return;
		}("add1", allocator);

		Job add2Job = [&](auto&...) -> Job
		{
			counter++;
			//std::this_thread::sleep_for(std::chrono::milliseconds(1));
			counter++;
			co_return;
		}("add2", allocator);
		scheduler.add(add2Job);

		Job calcJob = [&](auto&...) -> Job
		{
			co_await add1Job;
			co_await add2Job;
			co_await add2Job;
			counter += 0;

			co_return;
		}("calc", allocator);

		scheduler.add(calcJob);

		Job resultJob = [&](auto&...) -> Job
		{
			co_await add2Job;
			co_await calcJob;
			co_await add1Job;

			if (counter == 3)
			{
				numResults++;
			}
			assert(numResults == i + 1);

			co_return;
		}("result", allocator);

		scheduler.add(resultJob);

		while (!scheduler.isEmpty())
		{
			scheduler.processJob();
		}

		assert(resultJob.isDone());
		assert(add1Job.isDone() && add2Job.isDone() && calcJob.isDone());
		assert(numResults == i + 1);
	}
	int num = numResults;
	assert(num == i);

	auto endTime = std::chrono::high_resolution_clock::now();
	float durationSec = std::chrono::duration<float>(endTime - startTime).count();
	printf("Successfully processed: %i jobs in %f sec", num, durationSec);

	return 0;
}*/

/*
int main()
{
	SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
	constexpr size_t stackSize = 1024 * 48;
	StackAllocator<stackSize, 32, false> allocator;
	//Allocator allocator;
	constexpr int numIterations = 10;
	constexpr int numIterationsPerJob = 1000000;
	constexpr int numAllocationsPerIteration = 20;
	constexpr int numThreads = 1;
	constexpr int numJobs = numThreads;

	srand(4242);
	int allocations[numAllocationsPerIteration];
	for (int i = 0; i < numAllocationsPerIteration; ++i)
		allocations[i] = (1 + rand() % 500);

	std::function<void()> job;
	volatile bool shutdown = false;
	volatile bool started = false;
	std::atomic<int> doneCount = 0;
	std::thread threads[numThreads];
	for (int i = 0; i < numThreads; ++i)
		threads[i] = std::thread([i, &job, &started, &shutdown, &doneCount]()
			{
				SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
				while (true)
				{
					while (!started) { if (shutdown) return; }
					job();
					doneCount++;
					while (started) { if (shutdown) return; }
				}
			});

	std::this_thread::sleep_for(std::chrono::milliseconds(500));
	{
		float totalDuration = 0.0f;
		for (int awa = 0; awa < numIterations; ++awa)
		{
			job = [&allocator, allocations]()
				{
					void* ptrs[numAllocationsPerIteration];
					for (int j = 0; j < numIterationsPerJob; ++j)
					{
						for (int i = 0; i < numAllocationsPerIteration; i++)
							ptrs[i] = allocator.allocate(allocations[i]);
						for (int i = 0; i < numAllocationsPerIteration; i++)
							allocator.deallocate(ptrs[i]);
					}
				};
			std::this_thread::sleep_for(std::chrono::milliseconds(100));

			auto startTime = std::chrono::high_resolution_clock::now();
			started = true;
			while (doneCount < numThreads) {}
			auto endTime = std::chrono::high_resolution_clock::now();
			started = false;
			doneCount = 0;

			float durationSec = std::chrono::duration<float>(endTime - startTime).count();
			totalDuration += durationSec;
			printf("Done in %f sec\n", durationSec);
			printf("Stack: %zu, Fallback: %zu\n", allocator.getMaxUsedStackSize(), allocator.getMaxUsedFallbackSize());
		}
		printf("Average duration: %f sec\n", totalDuration / numIterations);
	}
	shutdown = true;
	for (int i = 0; i < numThreads; ++i)
		threads[i].join();
	return 0;
}*/
/*
int main()
{
	constexpr size_t stackSize = 1024 * 32;
	StackAllocator<stackSize, 32, true> allocator;

	Allocator& heapAllocator = g_heapAllocator;

	JobScheduler scheduler(0);
	std::atomic<int> counter = 0;

	std::vector<Job> jobs;
	for (int i = 0; i < 1000000; ++i)
	{
		const char* jobName = "job";
		jobs.push_back([&](auto&...) -> Job
		{
			counter++;
			co_return;
		}(jobName));
	}

	const char* jobName = "halve";
	Job halveJob = [&](auto&...) -> Job
	{
		counter = counter / 2;
		co_return;
	}(jobName);

	const char* b = "batchJob";
	Job batchJob = [&](auto&...) -> Job
	{
		for (int i = 0; i < jobs.size(); ++i)
			scheduler.add(jobs[i]);
		for (int i = 0; i < jobs.size(); ++i)
			co_await jobs[i];
		co_await halveJob;
		co_return;
	}(b);
	scheduler.add(batchJob);

	auto startTime = std::chrono::high_resolution_clock::now();

	while (!scheduler.isEmpty())
	{
		scheduler.processJob();
	}


	auto endTime = std::chrono::high_resolution_clock::now();
	printf("Successfully processed in %f sec", std::chrono::duration<float>(endTime - startTime).count());

	return 0;
}*/