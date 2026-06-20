// ============================================================
// Epoll 抽象层 — Windows 上基于 select() 实现 epoll 语义
// 在 Linux 上可替换为 <sys/epoll.h> 获得真正 epoll 性能
// ============================================================
#pragma once

#include <vector>
#include <map>
#include <algorithm>
#include <cstring>
#include <cerrno>
#include <chrono>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
#else
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#endif

// ---- Epoll 常量 ----
#define EPOLLIN 0x001
#define EPOLLOUT 0x004
#define EPOLLERR 0x008
#define EPOLLET 0 // 禁用边缘触发 (使用水平触发)

#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_MOD 2
#define EPOLL_CTL_DEL 3

// epoll_data 联合体 (必须在 epoll_event 之前定义)
typedef union epoll_data
{
    void *ptr;
    int fd;
} epoll_data_t;

struct epoll_event
{
    uint32_t events; // EPOLLIN | EPOLLOUT | ...
    epoll_data_t data;
};

// ---- Epoll 管理器 ----
class EpollInstance
{
public:
    EpollInstance()
    {
#ifdef _WIN32
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    }

    ~EpollInstance()
    {
#ifdef _WIN32
        WSACleanup();
#endif
    }

    // 添加/修改/删除监听的 fd
    int ctl(int op, int fd, epoll_event *event)
    {
        if (op == EPOLL_CTL_DEL)
        {
            watched_.erase(fd);
            return 0;
        }

        Watched w;
        w.events = event ? event->events : 0;
        w.data = event ? event->data : epoll_data_t{};
        w.active = true;
        watched_[fd] = w;
        return 0;
    }

    // 等待事件, 返回就绪事件数量
    int wait(epoll_event *events, int maxevents, int timeout_ms)
    {
        if (watched_.empty())
        {
            // 没有监听的 fd, 休眠后返回 0
            if (timeout_ms > 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
            }
            return 0;
        }

        // 构建 fd_set
        fd_set read_fds, write_fds;
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
        int max_fd = 0;

        for (auto &[fd, w] : watched_)
        {
            if (!w.active)
                continue;
            if (w.events & EPOLLIN)
                FD_SET(fd, &read_fds);
            if (w.events & EPOLLOUT)
                FD_SET(fd, &write_fds);
            if (fd > max_fd)
                max_fd = fd;
        }

        struct timeval tv;
        struct timeval *ptv = nullptr;
        if (timeout_ms >= 0)
        {
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
            ptv = &tv;
        }

        int ret = select(max_fd + 1, &read_fds, &write_fds, nullptr, ptv);
        if (ret <= 0)
            return ret; // 0=timeout, -1=error

        // 收集就绪事件
        int count = 0;
        for (auto &[fd, w] : watched_)
        {
            if (!w.active || count >= maxevents)
                break;
            uint32_t revents = 0;
            if (FD_ISSET(fd, &read_fds))
                revents |= EPOLLIN;
            if (FD_ISSET(fd, &write_fds))
                revents |= EPOLLOUT;

            if (revents)
            {
                events[count].events = revents;
                events[count].data = w.data;
                ++count;

                // 边缘触发: 就绪后暂时移除监听, 等待用户重新注册
                if (w.events & EPOLLET)
                {
                    w.active = false;
                }
            }
        }
        return count;
    }

    // 重新激活边缘触发的 fd
    void rearm(int fd)
    {
        auto it = watched_.find(fd);
        if (it != watched_.end())
        {
            it->second.active = true;
        }
    }

private:
    struct Watched
    {
        uint32_t events = 0;
        epoll_data_t data{};
        bool active = true;
    };
    std::map<int, Watched> watched_;
};

// ---- 全局便捷函数 (兼容 epoll API) ----
inline EpollInstance *epoll_create1(int /*flags*/)
{
    return new EpollInstance();
}

inline int epoll_ctl(EpollInstance *ep, int op, int fd, epoll_event *event)
{
    return ep->ctl(op, fd, event);
}

inline int epoll_wait(EpollInstance *ep, epoll_event *events, int maxevents, int timeout)
{
    return ep->wait(events, maxevents, timeout);
}

inline void epoll_rearm(EpollInstance *ep, int fd)
{
    ep->rearm(fd);
}
