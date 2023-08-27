#include <iostream>
#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <functional>
#include <condition_variable>
#include <future>
#include <chrono>
#include <algorithm>

using namespace std;
using namespace std::chrono;

typedef function<void()> task_type;
typedef void (*FuncType) (vector<int>&, int, int);

template<class T>
class BlockedQueue {
public:
    void push(T& item) {
        lock_guard<mutex> l(m_locker);
        m_task_queue.push(item);
        m_event_holder.notify_one();
    }

    void pop(T& item) {
        unique_lock<mutex> l(m_locker);
        if (m_task_queue.empty())
            m_event_holder.wait(l, [this] {return !m_task_queue.empty(); });

        item = m_task_queue.front();
        m_task_queue.pop();
    }

    bool fast_pop(T& item) {
        unique_lock<mutex> l(m_locker);
        if (m_task_queue.empty())
            return false;
        item = m_task_queue.front();
        m_task_queue.pop();
        return true;
    }

private:
    queue<T> m_task_queue;
    mutex m_locker;
    condition_variable m_event_holder;
};

class ThreadPool {
public:
    ThreadPool();
    ~ThreadPool();
    void start();
    void stop();
    void push_task(task_type task);
    void threadFunc(int qindex);

private:
    int m_thread_count;
    vector<thread> m_threads;
    vector<BlockedQueue<task_type>> m_thread_queues;
    atomic_bool m_stop;
};

void quicksort(vector<int>& array, int left, int right) {
    if (left < right) {
        int pivot = array[right];
        int i = left - 1;
        for (int j = left; j <= right - 1; ++j) {
            if (array[j] < pivot) {
                ++i;
                swap(array[i], array[j]);
            }
        }
        swap(array[i + 1], array[right]);
        pivot = i + 1;

        quicksort(array, left, pivot - 1);
        quicksort(array, pivot + 1, right);
    }
}

class ParallelQuicksort {
public:
    ParallelQuicksort(ThreadPool& pool, vector<int>& array, int left, int right, shared_ptr<promise<void>> completionPromise)
        : pool(pool), array(array), left(left), right(right), completionPromise(completionPromise) {}

    void operator()() {
        quicksort(array, left, right);
        completionPromise->set_value();
    }

private:
    ThreadPool& pool;
    vector<int>& array;
    int left, right;
    shared_ptr<promise<void>> completionPromise;
};

class RequestHandler {
public:
    RequestHandler();
    ~RequestHandler();
    void push_task(FuncType f, vector<int>& arr, int a, int b);

private:
    ThreadPool m_tpool;
};

ThreadPool::ThreadPool()
    : m_thread_count(thread::hardware_concurrency() != 0 ? thread::hardware_concurrency() : 4),
    m_thread_queues(m_thread_count),
    m_stop(false) {
    cout << "Initialized threads: " << m_thread_count << endl;
}

ThreadPool::~ThreadPool() {
    stop();
}

void ThreadPool::start() {
    for (int i = 0; i < m_thread_count; i++) {
        m_threads.emplace_back(&ThreadPool::threadFunc, this, i);
    }
}

void ThreadPool::stop() {
    m_stop = true;
    for (int i = 0; i < m_thread_count; i++) {
        task_type empty_task;
        m_thread_queues[i].push(empty_task);
    }
    for (auto& t : m_threads) {
        t.join();
    }
}

void ThreadPool::push_task(task_type task) {
    int queue_to_push = rand() % m_thread_count;
    m_thread_queues[queue_to_push].push(task);
}

void ThreadPool::threadFunc(int qindex) {
    while (!m_stop) {
        task_type task_to_do;
        if (m_thread_queues[qindex].fast_pop(task_to_do)) {
            if (task_to_do) {
                task_to_do();
            }
        }
    }
}

RequestHandler::RequestHandler() {
    this->m_tpool.start();
}

RequestHandler::~RequestHandler() {
    // this->m_tpool.stop(); // Не нужно вызывать stop() здесь
}

void RequestHandler::push_task(FuncType f, vector<int>& arr, int a, int b) {
    if (b - a + 1 <= 100000) {
        f(arr, a, b);
        return;
    }

    auto completionPromise = make_shared<promise<void>>();

    task_type task = ParallelQuicksort(m_tpool, arr, a, b, completionPromise);

    m_tpool.push_task(task);

    future<void> completionFuture = completionPromise->get_future();

    completionFuture.wait();
}

int main() {
    const int N = 1000000;
    vector<int> array(N);
    for (int i = 0; i < N; ++i) {
        array[i] = rand() % 1000000;
    }

    RequestHandler handler;

    auto start = high_resolution_clock::now();

    handler.push_task(quicksort, array, 0, N - 1);

    auto end = high_resolution_clock::now();
    duration<double> duration = end - start;

    cout << "Parallel Quicksort took " << duration.count() << " seconds.\n";

    return 0;
}
