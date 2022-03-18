// Copyright (c) 2021 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_PARALLEL_CACHE_H
#define DEFI_PARALLEL_CACHE_H

#include <sync.h>

#include <array>
#include <atomic>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <thread>
#include <queue>
#include <variant>

template<typename T>
class CParallelViewCache {
    std::list<T> views;
    CLockFreeMutex cs_queue;
    std::list<std::thread> threads;
    std::atomic_bool running{false};
    std::function<void()> createThread;
    std::queue<std::function<void(T&)>> tasks GUARDED_BY(cs_queue);

    void runner(T& view)
    {
        std::function<void(T&)> task;
        while (true) {
            {
                CLockFreeGuard lock(cs_queue);
                if (!tasks.empty()) {
                    task = tasks.front();
                    tasks.pop();
                } else if (running) {
                    continue;
                } else {
                    return;
                }
            }
            task(view);
        }
    }

public:
    CParallelViewCache(T& view, uint8_t maxThreads)
    {
        createThread = std::bind([this](T& view, uint8_t maxThreads) {
            if (threads.size() < maxThreads) {
                running = true;
                auto& cache = views.emplace_back(view);
                threads.emplace_back(&CParallelViewCache::runner, this, std::ref(cache));
            }
        }, std::ref(view), maxThreads);
    }

    ~CParallelViewCache()
    {
        if (running) {
            waitFlush();
        }
    }

    void addTask(std::function<void(T&)> task)
    {
        bool needsThread;
        {
            CLockFreeGuard lock(cs_queue);
            needsThread = !running || !tasks.empty();
            tasks.push(task);
        }
        if (needsThread) {
            createThread();
        }
    }

    void waitFlush()
    {
        running = false;
        for (auto& thread : threads) {
            thread.join();
        }
        threads.clear();
        for (auto& view : views) {
            view.Flush();
        }
        views.clear();
    }
};

template<typename V, typename T, typename... Args>
class CDataViewCache {
    V view;
    using VarT = std::variant<T, Args...>;
    static constexpr auto size = sizeof...(Args) + 1;
    using Caches = std::array<std::optional<VarT>, size>;
    Caches caches;

    template<typename DB, typename... DBs>
    void AddCache(size_t i, std::unique_ptr<DB>& db, DBs&... args)
    {
        if (db) {
            caches[i].emplace(*db);
        }
        if constexpr (sizeof...(DBs) != 0) {
            AddCache(i + 1, args...);
        }
    }

    template<size_t I>
    void AddCache(Caches& other)
    {
        if (auto& cache = other[I]) {
            caches[I].emplace(std::get<I>(*cache));
        }
        if constexpr (I + 1 < size) {
            AddCache<I + 1>(other);
        }
    }

public:
    CDataViewCache(const CDataViewCache&) = delete;
    CDataViewCache(V& view, std::unique_ptr<T>& db, Args&... args) : view(view)
    {
        AddCache(0, db, args...);
    }

    CDataViewCache(CDataViewCache& o) : view(o.view)
    {
        AddCache<0>(o.caches);
    }

    auto& GetView()
    {
        return view;
    }

    template<size_t I>
    auto Get_If() -> std::variant_alternative_t<I, VarT>*
    {
        if (auto& cache = caches[I]) {
            return std::get_if<I>(&(*cache));
        }
        return nullptr;
    }

    void Flush()
    {
        view.Flush();
        for (auto& cache : caches) {
            if (cache) {
                std::visit([](auto& c) { c.Flush(); }, *cache);
            }
        }
    }
};

#endif // DEFI_PARALLEL_CACHE_H
