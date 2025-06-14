#ifndef METRICS_H
#define METRICS_H

#include <iostream>
#include <fstream>
#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <vector>
#include <algorithm>

class MetricValue {
public:
    virtual ~MetricValue() = default;
    virtual void reset() = 0;
    virtual std::string toString() const = 0;
    virtual std::unique_ptr<MetricValue> clone() const = 0;
    virtual bool hasValue() const = 0;
};

// Шаблонная метафункция для проверки строк
template<typename> struct is_string : std::false_type {};
template<> struct is_string<std::string> : std::true_type {};

template<typename T>
class TypedMetricValue : public MetricValue {
public:
    TypedMetricValue(T value = T(), bool valid = false)
        : value_(value), valid_(valid) {}

    void reset() override { valid_ = false; }

    std::string toString() const override {
        return toStringImpl(typename is_string<T>::type());
    }

    bool hasValue() const override { return valid_; }

    std::unique_ptr<MetricValue> clone() const override {
        return std::make_unique<TypedMetricValue<T>>(value_, valid_);
    }

    void setValue(T value) {
        value_ = value;
        valid_ = true;
    }

private:
    std::string toStringImpl(std::true_type) const {
        return "\"" + value_ + "\"";
    }

    std::string toStringImpl(std::false_type) const {
        std::ostringstream oss;
        oss << value_;
        return oss.str();
    }

    T value_;
    bool valid_;
};

class MetricsCollector {
public:
    template<typename T>
    void record(const std::string& name, T value) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = metrics_.find(name);
        if (it == metrics_.end()) {
            metrics_[name] = std::make_unique<TypedMetricValue<T>>(value, true);
        }
        else {
            auto typed = dynamic_cast<TypedMetricValue<T>*>(it->second.get());
            if (typed) {
                typed->setValue(value);
            }
            else {
                throw std::runtime_error("Type mismatch for metric " + name);
            }
        }
    }

    void flushToFile(const std::string& filename) {
        std::vector<std::pair<std::string, std::unique_ptr<MetricValue>>> snapshot;

        // Собираем снимок метрик
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& metric_pair : metrics_) {
                if (metric_pair.second->hasValue()) {
                    snapshot.emplace_back(metric_pair.first, metric_pair.second->clone());
                    metric_pair.second->reset();
                }
            }
        }

        if (snapshot.empty()) return;

        std::ofstream file(filename, std::ios_base::app);
        if (!file.is_open()) return;

        // Форматирование времени
        auto now = std::chrono::system_clock::now();
        auto now_time = std::chrono::system_clock::to_time_t(now);
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::tm tm_struct;
        #if defined(_WIN32) || defined(_WIN64)
            localtime_s(&tm_struct, &now_time);  // Windows
        #else
            localtime_r(&now_time, &tm_struct);  // POSIX (Linux/macOS)
        #endif

        file << std::put_time(&tm_struct, "%Y-%m-%d %H:%M:%S");

        // Сортировка по имени метрики
        std::sort(snapshot.begin(), snapshot.end(),
            [](const std::pair<std::string, std::unique_ptr<MetricValue>>& a,
                const std::pair<std::string, std::unique_ptr<MetricValue>>& b) {
                    return a.first < b.first;
            });

        // Запись метрик
        for (const auto& metric_pair : snapshot) {
            file << " \"" << metric_pair.first << "\" " << metric_pair.second->toString();
        }

        file << std::endl;
    }

private:
    std::unordered_map<std::string, std::unique_ptr<MetricValue>> metrics_;
    std::mutex mutex_;
};

class AsyncMetricsWriter {
public:
    AsyncMetricsWriter(const std::string& filename, int flushIntervalMs = 1000)
        : filename_(filename),
        flushIntervalMs_(flushIntervalMs),
        running_(true),
        writerThread_(&AsyncMetricsWriter::run, this) {}

    ~AsyncMetricsWriter() {
        running_ = false;
        cv_.notify_all();
        writerThread_.join();
        collector_.flushToFile(filename_);
    }

    template<typename T>
    void record(const std::string& name, T value) {
        collector_.record(name, value);
        std::lock_guard<std::mutex> lock(mutex_);
        scheduledFlush_ = true;
        cv_.notify_one();
    }

private:
    void run() {
        auto lastFlush = std::chrono::steady_clock::now();
        while (running_) {
            std::unique_lock<std::mutex> lock(mutex_);

            cv_.wait_for(lock, std::chrono::milliseconds(flushIntervalMs_), [this] {
                return !running_ || scheduledFlush_;
                });

            auto now = std::chrono::steady_clock::now();
            if (scheduledFlush_ || now - lastFlush >= std::chrono::milliseconds(flushIntervalMs_)) {
                collector_.flushToFile(filename_);
                lastFlush = now;
                scheduledFlush_ = false;
            }
        }
    }

    MetricsCollector collector_;
    std::string filename_;
    int flushIntervalMs_;
    std::atomic<bool> running_;
    std::thread writerThread_;

    std::mutex mutex_;
    std::condition_variable cv_;
    bool scheduledFlush_ = false;
};

#endif // METRICS_H