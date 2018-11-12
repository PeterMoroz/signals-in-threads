#include <csignal>
#include <cstring>

#include <pthread.h>
#include <unistd.h>

#include <array>
#include <iostream>

static volatile sig_atomic_t signum = 0;

void sig_action(int signum, siginfo_t* info, void* uctx) {
	std::cout << "got signal: " << signum << " thread ID " << pthread_self() << std::endl;
	::signum = signum;
}

void* worker_thread(void* arg) {
	std::cout << " worker thread ID " << pthread_self() << std::endl;
	while (::signum == 0) {
		usleep(10000);		
	}
	return NULL;
}



int main(int argc, char** argv) {
	std::cout << "PID: " << getpid() << " main thread ID " << pthread_self() <<  std::endl;
	
	struct sigaction sigact;
	memset(&sigact, 0, sizeof(sigact));
	
	sigact.sa_sigaction = sig_action;
	
	std::array<int, 7> signums{ SIGINT, SIGTERM, SIGILL, 
								SIGFPE, SIGBUS, SIGTRAP, SIGABRT };
	
	for (std::size_t i = 0; i < signums.size(); i++) {
		if (sigaction(signums[i], &sigact, NULL) != 0) {
			std::cerr << "couldn't setup handler to signal " << signums[i] << std::endl;
			std::exit(-1);
		}
	}
	std::cout << std::endl;
	
	static const std::size_t WORK_THREADS_NUM = 5;

	std::size_t thread_count = 0;
	pthread_t work_threads[WORK_THREADS_NUM];
	for (std::size_t i = 0; i < WORK_THREADS_NUM; i++) {
		if (pthread_create(&work_threads[i], NULL, worker_thread, NULL)) {
			std::cerr << "pthread_create() failed.\n";
			break;
		}
		thread_count++;
	}

	std::cout << "started " << thread_count << " threads.\n";

	while (::signum == 0) {
		usleep(100000);
	}

	std::cout << "join worker threads.\n";
	for (std::size_t i = 0; i < thread_count; i++) {
		pthread_join(work_threads[i], NULL);
	}

	std::cout << "finishing...\n";

	return 0;
}

