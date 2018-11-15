#include <pthread.h>

#include <cassert>
#include <csignal>
#include <ctime>

#include <array>
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
static volatile sig_atomic_t signum = 0;

static void sig_handler(int signum) {
	::signum = signum;
}


int main(int argc, char** argv) {
	
	if (argc != 2) {
		std::cout << "usage: " << argv[0] << " <file-to-process>\n";
		std::exit(-1);
	}
	
	struct sigaction sigact;
	memset(&sigact, 0, sizeof(sigact));
	sigact.sa_handler = sig_handler;
	
	std::array<int, 7> signums{ SIGINT, SIGTERM, SIGILL, 
								SIGFPE, SIGBUS, SIGTRAP, SIGABRT };
	for (std::size_t i = 0; i < signums.size(); i++) {
		if (sigaction(signums[i], &sigact, NULL) != 0) {
			std::cerr << "couldn't install handler to signal: " << signums[i] << std::endl;
			std::exit(-1);
		}
	}	

	// test samples got here: http://pizzachili.dcc.uchile.cl/texts/nlang/
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
	// printing the status of tasks to console, untill receive a signal
	// a that one from those which were registered earlier	
	while (::signum == 0) {
		std::cout << "\n ---- state:\n";
		std::list<const Task*> tasks = TasksRegistry::GetRunningTasks();
		if (tasks.empty())
			break;
		for (const Task* task : tasks) {
			std::cout << " task " << reinterpret_cast<const void*>(task);			
			std::cout << " task TID " << task->tid() 
				<< " processed lines " << task->line_count()
				<< " elapsed seconds " << task->elapsed_time()
				<< std::endl;
		}
		std::cout << std::endl;
		sleep(4);
	}
	
	std::cout << "awaiting untill work tasks finished...\n";
	tp.wait();
	std::cout << "done\n";
	
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
