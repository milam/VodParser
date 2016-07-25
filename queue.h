#pragma once
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
#include <map>

template<class Input, class Output>
class JobQueue {
public:
  void push(Input const& input) {
    std::lock_guard<std::mutex> guard(mutex);
    inputs_.push_back(input);
    cv.notify_all();
  }
  void finish() {
    std::lock_guard<std::mutex> guard(mutex);
    finished_ = true;
    cv.notify_all();
  }

  void stop() {
    {
      std::lock_guard<std::mutex> guard(mutex);
      finished_ = true;
      inputs_.clear();
      cv.notify_all();
    }
    join();
  }

  bool pop(Output& output) {
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [this]{
      return outputs_.count(out_index_) || (out_index_ >= in_index_ && inputs_.empty() && finished_);
    });
    auto it = outputs_.find(out_index_);
    if (it == outputs_.end()) return false;
    output = it->second;
    outputs_.erase(it);
    ++out_index_;
    return true;
  }

  void start(size_t threads) {
    for (size_t i = 0; i < threads; ++i) {
      threads_.emplace_back(thread_proc, this);
    }
  }
  void join() {
    for (std::thread& t : threads_) {
      t.join();
    }
    threads_.clear();
  }

protected:
  virtual void process(Input const& input, Output& output) = 0;
private:
  std::vector<std::thread> threads_;
  bool finished_ = false;
  std::deque<Input> inputs_;
  std::map<size_t, Output> outputs_;
  size_t out_index_ = 0;
  size_t in_index_ = 0;
  std::mutex mutex;
  std::condition_variable_any cv;

  static void thread_proc(JobQueue<Input, Output>* queue) {
    while (true) {
      Input input;
      size_t index;
      {
        std::unique_lock<std::mutex> lock(queue->mutex);
        queue->cv.wait(lock, [queue]{
          return queue->inputs_.size() || queue->finished_;
        });
        if (!queue->inputs_.size()) {
          break;
        }
        input = queue->inputs_.front();
        queue->inputs_.pop_front();
        index = queue->in_index_++;
      }
      Output output;
      queue->process(input, output);

      std::lock_guard<std::mutex> guard(queue->mutex);
      queue->outputs_[index] = output;
      queue->cv.notify_all();
    }
  }
};
