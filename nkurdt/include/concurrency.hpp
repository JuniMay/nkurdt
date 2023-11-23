#ifndef NKURDT_CONCURRENCY_H_
#define NKURDT_CONCURRENCY_H_

#include <atomic>
#include <chrono>
#include <concepts>
#include <condition_variable>
#include <map>
#include <mutex>
#include <optional>
#include <queue>
#include <set>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include "logging.hpp"
#include "packet.hpp"

namespace std {
/// Hasher for time point
using steady_clk_tp = std::chrono::time_point<std::chrono::steady_clock>;
template <>
struct hash<steady_clk_tp> {
  size_t operator()(const steady_clk_tp& tp) const {
    return std::hash<std::chrono::steady_clock::rep>{}(
      tp.time_since_epoch().count()
    );
  }
};

}  // namespace std

namespace nkurdt {

/// A concurrent queue.
///
/// This is used for copyable types.
template <typename T>
  requires std::copyable<T>
class ConcurrentQueue {
 private:
  /// The underlying container.
  std::set<T> container_;
  /// The mutex for concurrent access.
  std::mutex mutex_;
  /// The condition variable for waiting.
  std::condition_variable cvar_;

 public:
  /// Pushes an element to the queue.
  void push(const T& elem) {
    std::lock_guard lock(this->mutex_);
    this->container_.insert(elem);
    this->cvar_.notify_one();
  }

  /// Check some property of the front.
  template <typename UnaryPredicate>
    requires std::predicate<UnaryPredicate, T>
  bool check_front(UnaryPredicate pred) const {
    std::lock_guard lock(this->mutex_);
    return pred(*this->container_.begin());
  }

  void lock() { this->mutex_.lock(); }

  void unlock_and_notify() {
    this->mutex_.unlock();
    this->cvar_.notify_one();
  }

  /// Push element without locking.
  ///
  /// This is useful when pushing a sequence of elements.
  void unsafe_push(const T& elem) { this->container_.insert(elem); }

  /// Pop the front
  std::optional<T> try_pop() {
    std::lock_guard<std::mutex> lock(this->mutex_);

    std::optional<T> elem;

    if (this->container_.empty()) {
      elem = std::nullopt;
    } else {
      elem = *this->container_.begin();
      this->container_.erase(elem.value());
    }
    return elem;
  }

  /// Wait until the queue is non-empty and then pop the front
  std::optional<T> wait_pop(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(this->mutex_);
    std::optional<T> elem;

    if (this->cvar_.wait_for(lock, timeout, [this] {
          return !this->container_.empty();
        })) {
      elem = *this->container_.begin();
      this->container_.erase(elem.value());
    } else {
      elem = std::nullopt;
    }

    return elem;
  }

  bool empty() {
    std::lock_guard<std::mutex> lock(this->mutex_);
    return this->container_.empty();
  }
};

/// Hashable concept.
template <typename T>
concept Hashable = requires(T a) {
  { std::hash<T>{}(a) } -> std::convertible_to<std::size_t>;
};

/// A concurrent event tiemr.
///
/// An event with type T can be registered into the timer. And given a event,
/// one can query if it is timed out.
template <typename T>
  requires std::movable<T> && Hashable<T> && std::totally_ordered<T>
class ConcurrentTimer {
 private:
  /// The underlying container to record the events.
  std::multimap<std::chrono::steady_clock::time_point, T> events_;
  /// The registered events
  std::unordered_map<T, std::chrono::steady_clock::time_point>
    registered_events_;
  /// The timed out set, unordered.
  std::unordered_set<T> timed_out_events_;
  /// The mutex for concurrent access.
  std::mutex mutex_;
  /// The condition variable for waiting for events.
  std::condition_variable event_cvar_;
  /// The condition variable for waiting for timed out events.
  std::condition_variable timeout_cvar_;
  /// The timing thread.
  std::thread thread_;
  /// Running indicator
  std::atomic<bool> running_;

  void thread_func() {
    while (this->running_) {
      std::unique_lock<std::mutex> lock(this->mutex_);
      if (this->events_.empty()) {
        this->event_cvar_.wait(lock);
      } else {
        auto now = std::chrono::steady_clock::now();
        auto it = this->events_.begin();
        auto tp = it->first;
        if (tp > now) {
          this->event_cvar_.wait_until(lock, tp);
        } else {
          using namespace std::chrono_literals;
          // this will time out a range of event approximately.
          while (it != this->events_.end() && (it->first <= now + 100ms)) {
            this->timed_out_events_.insert(it->second);
            this->registered_events_.erase(it->second);
            it = this->events_.erase(it);
            now = std::chrono::steady_clock::now();
          }
          this->timeout_cvar_.notify_one();
        }
      }
    }
  }

 public:
  /// The state of an event.
  enum class State {
    /// The event is not registered.
    Unregistered,
    /// The event is registered and not timed out.
    Normal,
    /// The event is registered and timed out.
    TimedOut,
  };

  void register_event(T&& event, std::chrono::milliseconds timeout) {
    std::lock_guard<std::mutex> lock(this->mutex_);
    if (this->registered_events_.contains(event)) {
      // remove the old event
      // find the timepoint of the given event in the `registered_event_`
      // and then find in the range.
      auto old_tp = this->registered_events_.at(event);
      using TimeoutIter =
        std::multimap<std::chrono::steady_clock::time_point, T>::iterator;
      std::pair<TimeoutIter, TimeoutIter> range =
        this->events_.equal_range(old_tp);
      for (auto it = range.first; it != range.second; ++it) {
        if (it->second == event) {
          this->events_.erase(it);
          break;
        }
      }
      this->registered_events_.erase(event);
    }
    // new time point.
    auto tp = std::chrono::steady_clock::now() + timeout;
    this->events_.emplace(tp, std::move(event));
    if constexpr (std::is_same_v<T, SequenceNumber>) {
      // debug for sequence number
      global_logger.debug(std::format(
        "register seq: {}, timepoint: {}", event.get(),
        tp.time_since_epoch().count()
      ));
    }
    this->registered_events_.emplace(event, tp);
    this->event_cvar_.notify_one();
  }

  void register_event(const T& event, std::chrono::milliseconds timeout) {
    std::lock_guard<std::mutex> lock(this->mutex_);
    if (this->registered_events_.contains(event)) {
      // remove the old event
      auto old_tp = this->registered_events_.at(event);

      using TimeoutIter =
        std::multimap<std::chrono::steady_clock::time_point, T>::iterator;

      std::pair<TimeoutIter, TimeoutIter> range =
        this->events_.equal_range(old_tp);
      for (auto it = range.first; it != range.second; ++it) {
        if (it->second == event) {
          this->events_.erase(it);
          break;
        }
      }
      this->registered_events_.erase(event);
    }
    auto tp = std::chrono::steady_clock::now() + timeout;
    this->events_.emplace(tp, event);
    if constexpr (std::is_same_v<T, SequenceNumber>) {
      global_logger.debug(std::format(
        "register seq: {}, timepoint: {}", event.get(),
        tp.time_since_epoch().count()
      ));
    }
    this->registered_events_.emplace(event, tp);
    this->event_cvar_.notify_one();
  }

  State query_event(const T& event) {
    std::lock_guard<std::mutex> lock(this->mutex_);
    if (this->timed_out_events_.contains(event)) {
      return State::TimedOut;
    }
    if (this->registered_events_.contains(event)) {
      return State::Normal;
    }
    return State::Unregistered;
  }

  void remove_event(const T& event) {
    std::lock_guard<std::mutex> lock(this->mutex_);

    if (!this->registered_events_.contains(event)) {
      return;
    }

    auto tp = this->registered_events_.at(event);

    this->registered_events_.erase(event);
    using TimeoutIter =
      std::multimap<std::chrono::steady_clock::time_point, T>::iterator;

    std::pair<TimeoutIter, TimeoutIter> range =
      this->events_.equal_range(tp);

    for (auto it = range.first; it != range.second; ++it) {
      if (it->second == event) {
        this->events_.erase(it);
        break;
      }
    }
    this->timed_out_events_.erase(event);
  }

  void start() {
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->events_.clear();
    this->registered_events_.clear();
    this->timed_out_events_.clear();
    this->running_ = true;
    this->thread_ = std::thread(&ConcurrentTimer::thread_func, this);
  }

  void stop() {
    {
      std::lock_guard<std::mutex> lock(this->mutex_);
      this->running_ = false;
    }
    global_logger.debug("timer notifying thread");
    this->event_cvar_.notify_one();
    this->thread_.join();
  }

  bool wait_for_timed_out(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(this->mutex_);
    return this->timeout_cvar_.wait_for(lock, timeout, [this] {
      return !this->timed_out_events_.empty();
    });
  }

  /// Map
  template <typename UnaryFunction>
    requires std::invocable<UnaryFunction, T>
  void map_timed_out_events(UnaryFunction f, bool clear = true) {
    std::lock_guard<std::mutex> lock(this->mutex_);
    for (const auto& event : this->timed_out_events_) {
      f(event);
    }

    if (clear) {
      this->timed_out_events_.clear();
    }
  }
};

/// A concurrent sliding window.
///
/// This supports waiting for the window to be available, slide, query the
/// bound, etc.
class ConcurrentWindow {
 private:
  /// The window.
  std::pair<SequenceNumber, SequenceNumber> window_;
  /// The mutex
  std::mutex mutex_;
  /// The condition variable for waiting.
  std::condition_variable cvar_;

  /// The maximum window size.
  ///
  /// This is used to determine if the window is available.
  size_t max_window_size_;

 public:
  void set_window(SequenceNumber lower, SequenceNumber upper) {
    std::lock_guard<std::mutex> lock(this->mutex_);
    global_logger.debug(
      std::format("set window: lower: {}, upper: {}", lower.get(), upper.get())
    );
    this->window_ = std::make_pair(lower, upper);
    this->cvar_.notify_all();
  }

  void lock() { this->mutex_.lock(); }
  void unlock() { this->mutex_.unlock(); }

  size_t max_window_size() {
    std::lock_guard<std::mutex> lock(this->mutex_);
    return this->max_window_size_;
  }

  void set_lower_bound(SequenceNumber lower) {
    std::lock_guard<std::mutex> lock(this->mutex_);
    global_logger.debug(std::format("set lower bound: {}", lower.get()));
    this->window_.first = lower;
    this->cvar_.notify_all();
  }

  /// Waiting for window to be available
  bool wait_window_available(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(this->mutex_);
    global_logger.debug(std::format(
      "lower: {}, upper: {}, max_window_size: {}", this->window_.first.get(),
      this->window_.second.get(), this->max_window_size_
    ));
    bool available = this->cvar_.wait_for(lock, timeout, [this] {
      return this->window_.second - this->window_.first <
             (uint32_t)this->max_window_size_;
    });
    return available;
  }

  bool wait_window_zero(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(this->mutex_);
    return this->cvar_.wait_for(lock, timeout, [this] {
      return this->window_.second == this->window_.first;
    });
  }

  SequenceNumber lower_bound() {
    std::lock_guard<std::mutex> lock(this->mutex_);
    // global_logger.debug(
    //   std::format("get lower bound: {}", this->window_.first.get())
    // );
    return this->window_.first;
  }

  SequenceNumber upper_bound() {
    std::lock_guard<std::mutex> lock(this->mutex_);
    // global_logger.debug(
    //   std::format("get upper bound: {}", this->window_.second.get())
    // );
    return this->window_.second;
  }

  void update_lower_bound() {
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->window_.first.update();
    global_logger.debug(
      std::format("update lower bound: {}", this->window_.first.get())
    );
    this->cvar_.notify_all();
  }

  void update_upper_bound() {
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->window_.second.update();
    global_logger.debug(
      std::format("update upper bound: {}", this->window_.second.get())
    );
    this->cvar_.notify_all();
  }

  void set_max_window_size(size_t max_window_size) {
    std::lock_guard<std::mutex> lock(this->mutex_);
    global_logger.debug(std::format("set max window size: {}", max_window_size)
    );
    this->max_window_size_ = max_window_size;
  }
};

}  // namespace nkurdt

#endif  // NKURDT_CONCURRENCY_H_
