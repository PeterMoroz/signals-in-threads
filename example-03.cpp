#include <pthread.h>

#include <cassert>
#include <csignal>
#include <ctime>

#include <array>
#include <atomic>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include <boost/thread.hpp>
#include <boost/threadpool.hpp>

/* 
 * The class emulates task, which is running during long term of time.
 * Its job is:
 * 1. open text file
 * 2. read file line by line
 * 3. count the frequency of occurency of each word
 * 
 * The job is performed by method operator()(), invoked
 * in scope of separate thread from the pool of threads.
 * */
class Task final {

public:
	explicit Task(const char* fname);
	~Task();
	
	void operator()();
	
	// unsafe acccess to internal variables.
	// not serious mistake in given context, 
	// because they are used just for logging of task's state
	
	pthread_t tid() const { return _tid; }
	
	double elapsed_time() const {
		clock_t t = clock();
		return static_cast<double>(t - _start) / CLOCKS_PER_SEC;
	}
	
	std::size_t line_count() const { return _line_count; }
	
private:
	const char* _fname;
	pthread_t _tid;
	clock_t _start;
	std::size_t _line_count;	
	std::map<std::string, std::uint32_t> _word_counters;
};


class TasksRegistry {
	TasksRegistry(const TasksRegistry&) = delete;
	const TasksRegistry& operator=(const TasksRegistry&) = delete;
	
public:
	explicit TasksRegistry(const Task* task);
	~TasksRegistry();
	
	static std::list<const Task*> GetRunningTasks();
		
private:
	static std::map<pthread_t, const Task*> _tasks;
	static boost::mutex _mutex;
};


////////////////////////////////////////////////////////////////////////
// 

static sigset_t sig_set;

static std::atomic<int> sig_num{ 0 }; // number of the latest received signal
static std::atomic<bool> running{ true };

static void* sig_handle_worker_routine(void* arg) {
	int sig_num = 0;
	bool emergency = false;
	while (::running.load()) {
		if (sigwait(&::sig_set, &sig_num) < 0) {
			perror("sigwait()");
			emergency = true;
		} else {
			::sig_num.store(sig_num);
			std::cout << "received signal: " << sig_num << std::endl;
		}
		
		if (emergency || 
			sig_num == SIGINT || sig_num == SIGABRT || sig_num == SIGTERM) {
			std::cout << "cancel running tasks (if any)\n";
			// cancel tasks which are running
			std::list<const Task*> tasks = TasksRegistry::GetRunningTasks();
			for (const Task* task : tasks) {
				if (pthread_cancel(task->tid()) != 0) {
					perror("pthread_cancel()");
					std::cerr << "canceliing of task TID " 
						<< task->tid() << " failed\n";
				}
			}
			break;
		}
	}
	/*
	::running = 0;
	* */
	::running.store(false);
	return NULL;
}

int main(int argc, char** argv) {
	
	if (argc != 2) {
		std::cout << "usage: " << argv[0] << " <file-to-process>\n";
		std::exit(-1);
	}
	
	if (sigemptyset(&::sig_set) < 0) {
		perror("sigemptyset()");
		std::exit(-1);
	}
	
	// fill the signal's mask and block signals, which we are going to handle
	std::array<int, 7> sig_nums{ SIGINT, SIGTERM, SIGILL, 
								SIGFPE, SIGBUS, SIGTRAP, SIGABRT };
	for (std::size_t i = 0; i < sig_nums.size(); i++) {
		if (sigaddset(&::sig_set, sig_nums[i]) < 0) {
			perror("sigaddset()");
			std::cerr << "couldn't add signal " << sig_nums[i] << " to mask\n";
			std::exit(-1);
		}
	}
	
	sigset_t original_sig_set;	
	if (sigprocmask(SIG_BLOCK, &sig_set, &original_sig_set) != 0) {
		perror("sigprocmask()");
		std::cerr << "couldn't block signals\n";
		std::exit(-1);
	}
		
	// start thread, responsible for receiving and handling of signals
	pthread_t sig_handle_worker;
	if (pthread_create(&sig_handle_worker, NULL, sig_handle_worker_routine, NULL) != 0) {
		perror("pthread_create()");
		std::cerr << "couldn't create thread to handle signals\n";
		std::exit(-1);
	}
	

	const char* fname = argv[1];
	
	unsigned int num_of_threads = std::thread::hardware_concurrency();
	std::cout << "able to run " << num_of_threads << " concurrent threads\n";
	
	boost::threadpool::pool tp(num_of_threads);	

	Task task1(fname);
	Task task2(fname);
	Task task3(fname);
	Task task4(fname);
	
	tp.schedule(task1);
	tp.schedule(task2);
	tp.schedule(task3);
	tp.schedule(task4);	

	std::cout << " PID = " << getpid() 
		<< " main thread TID " << pthread_self() << std::endl;
		
	// printing the status of tasks to console
	while (1) {
		// wait little bit:
		// initially, untill tasks will be scheduled
		// and then - for some delay between outputs
		sleep(4);		
		std::list<const Task*> tasks = TasksRegistry::GetRunningTasks();
		if (tasks.empty())
			break;
		std::cout << "\n ---- state:\n";
		for (const Task* task : tasks) {
			std::cout << " task " << reinterpret_cast<const void*>(task);			
			std::cout << " task TID " << task->tid() 
				<< " processed lines " << task->line_count()
				<< " elapsed seconds " << task->elapsed_time()
				<< std::endl;
		}
		std::cout << std::endl;
	}
	
	std::cout << "awaiting untill work tasks finished...\n";
	tp.wait();

	if (::running.load()) {
		// signals handler thread still working
		if (pthread_kill(sig_handle_worker, SIGTERM) != 0) {
			perror("pthread_kill()");
			std::cerr << "couldn't send signal to terminate thread\n";
		}
	}
	
	if (pthread_join(sig_handle_worker, NULL) != 0) {
		perror("pthread_join()");
		std::cerr << "couldn't join signals handler thread\n";
	}
	std::cout << "done\n";
	
	// restore original signals mask
	if (sigprocmask(SIG_SETMASK, &original_sig_set, NULL) != 0) {
		perror("sigprocmask()");
		std::cerr << "couldn't restore signals mask\n";
		std::exit(-1);
	}
	
	return 0;
}


////////////////////////////////////////////////////////////////////////
// Task implementation
Task::Task(const char* fname) 
	: _fname(fname) 
	{
	}
	
Task::~Task()
{
}

void Task::operator()()
{
	TasksRegistry registry_entry(this);
	
	_tid = pthread_self();
	assert(_fname != NULL);	
	std::ifstream in_file(_fname);
	
	if (!in_file) {
		std::cerr << "couldn't open file " << _fname
			<< " premature finishing of task, TID = " << _tid << std::endl;
		return;
	}
		
	std::function<void (const std::string&)> count_word = [this](const std::string& w) {
		std::uint32_t n = _word_counters[w];
		_word_counters[w] = n + 1;
	};
	
	std::function<void (const std::string&)> split_line_and_count_words =
			[this, &count_word](const std::string& line) {
				std::stringstream ss(line);
				std::string word;
				while (std::getline(ss, word, ' ')) {
					count_word(word);
				}				
			};
	
	_line_count = 0;
	_start = clock();
	
	std::string line;
	while (!in_file.eof()) {
		if (!std::getline(in_file, line) || line.empty())
			continue;
		_line_count++;
		split_line_and_count_words(line);
	}

	std::uint32_t words_total = 0;
	for (const std::pair<std::string, std::uint32_t>& p : _word_counters) {
		words_total += p.second;
	}	
	std::cout << "task finished, TID = " << tid()
		<< " lines processed " << line_count()
		<< " number of words " << words_total
		<< " elapsed time " << elapsed_time() << " sec\n";
}

////////////////////////////////////////////////////////////////////////
// TasksRegistry implementation
TasksRegistry::TasksRegistry(const Task* task) {
	TasksRegistry::_mutex.lock();
	pthread_t tid = pthread_self();
	std::cout << "register thread " << tid << std::endl;
	_tasks[tid] = task;
	TasksRegistry::_mutex.unlock();
}

TasksRegistry::~TasksRegistry() {
	TasksRegistry::_mutex.lock();
	pthread_t tid = pthread_self();
	std::cout << "unregister thread " << tid << std::endl;
	_tasks.erase(tid);
	TasksRegistry::_mutex.unlock();	
}

std::list<const Task*> TasksRegistry::GetRunningTasks() {
	std::list<const Task*> tasks;
	TasksRegistry::_mutex.lock();
	for (const std::pair<pthread_t, const Task*>& p : TasksRegistry::_tasks) {
		if (p.second != NULL)
			tasks.push_back(p.second);
	}
	TasksRegistry::_mutex.unlock();
	return tasks;
}

std::map<pthread_t, const Task*> TasksRegistry::_tasks;
boost::mutex TasksRegistry::_mutex;
