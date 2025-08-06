#include "watcher.h"
#include "sample_queue.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <limits.h>
#include <sys/inotify.h>
#include <sys/select.h>
#include <unistd.h>
#include <sstream>
#include <sys/types.h>

using json = nlohmann::json;

// Helper to get current system time in microseconds
static inline uint64_t currentTimeUSec() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// Read total CPU times from /proc/stat
static bool readTotalCpu(uint64_t& total, uint64_t& idle) {
    std::ifstream f("/proc/stat");
    if (!f.is_open()) {
        return false;
    }
    std::string cpu;
    uint64_t user = 0, nice = 0, system = 0, idleTime = 0, iowait = 0,
             irq = 0, softirq = 0, steal = 0;
    f >> cpu >> user >> nice >> system >> idleTime >> iowait >> irq >> softirq >> steal;
    if (!f) {
        return false;
    }
    total = user + nice + system + idleTime + iowait + irq + softirq + steal;
    idle = idleTime + iowait;
    return true;
}

// Read process CPU time (utime+stime) from /proc/<pid>/stat
static bool readProcCpu(pid_t pid, uint64_t& time) {
    std::string path = "/proc/" + std::to_string(pid) + "/stat";
    FILE* fp = fopen(path.c_str(), "r");
    if (!fp) {
        return false;
    }
    unsigned long utime = 0, stime = 0;
    if (fscanf(fp,
               "%*d %*[^)]%*c %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu",
               &utime,
               &stime) != 2) {
        fclose(fp);
        return false;
    }
    fclose(fp);
    time = utime + stime;
    return true;
}

// Read system memory usage (MemTotal-MemAvailable) in kB
static bool readSystemMem(uint64_t& usedKb) {
    std::ifstream f("/proc/meminfo");
    if (!f.is_open()) {
        return false;
    }
    std::string key;
    uint64_t memTotal = 0, memAvail = 0;
    while (f >> key) {
        if (key == "MemTotal:") {
            f >> memTotal;
        } else if (key == "MemAvailable:") {
            f >> memAvail;
        }
        std::string rest;
        std::getline(f, rest);
        if (memTotal && memAvail) {
            break;
        }
    }
    if (!memTotal) {
        return false;
    }
    usedKb = memTotal - memAvail;
    return true;
}

// Read process resident memory from /proc/<pid>/status (VmRSS)
static bool readProcMem(pid_t pid, uint64_t& memKb) {
    std::string path = "/proc/" + std::to_string(pid) + "/status";
    std::ifstream f(path);
    if (!f.is_open()) {
        return false;
    }
    std::string key;
    while (f >> key) {
        if (key == "VmRSS:") {
            f >> memKb;
            // Einheit (kB) verwerfen
            std::string unit;
            f >> unit;
            return true;
        }
        std::string rest;
        std::getline(f, rest);
    }
    return false;
}

static bool readProcMemTree(pid_t pid, uint64_t& totalKb) {
    totalKb = 0;
    uint64_t mem = 0;
    readProcMem(pid, mem);
    totalKb += mem;

    // Kinder aus /proc/<pid>/task/<pid>/children lesen
    std::string childrenPath = "/proc/" + std::to_string(pid)
                               + "/task/" + std::to_string(pid)
                               + "/children";
    std::ifstream cf(childrenPath);
    if (cf.is_open()) {
        pid_t child;
        while (cf >> child) {
            uint64_t childMem = 0;
            readProcMemTree(child, childMem);
            totalKb += childMem;
        }
    }
    return true;
}

// The watcher monitors the given file via inotify. Whenever the file is modified
// new lines are read and written together with an additional timestamp into a
// companion file "<path>.watch".
void startWatcher(const std::string& path,
                  SampleQueue& queue,
                  std::atomic<bool>& running,
                  pid_t loggerPid) {
    std::cout << "Watcher started for " << path << std::endl;

    // Touch the file if it does not exist so that we can open it for reading
    if (access(path.c_str(), F_OK) != 0) {
        std::ofstream create(path);
        if (!create.is_open()) {
            std::cerr << "Watcher: unable to create log file " << path << std::endl;
            return;
        }
    }


    // Open the file for reading. We keep the stream open and only read newly
    // appended lines.
    std::ifstream in(path);
    if (!in.is_open()) {
        std::cerr << "Watcher: unable to open log file " << path << std::endl;
        return;
    }

    // Seek to the end so we only capture data appended after start.
    in.seekg(0, std::ios::end);

    std::string outPath = path + ".watch";
    std::ofstream out(outPath, std::ios::app);
    if (!out.is_open()) {
        std::cerr << "Watcher: unable to open output file " << outPath << std::endl;
        return;
    }

    uint64_t prevTotalCpu = 0, prevIdleCpu = 0, prevProcCpu = 0;
    readTotalCpu(prevTotalCpu, prevIdleCpu);
    if (loggerPid > 0) {
        readProcCpu(loggerPid, prevProcCpu);
    }

    int fd = inotify_init1(IN_NONBLOCK);
    if (fd < 0) {
        perror("Watcher: inotify_init1");
        return;
    }

    int wd = inotify_add_watch(fd, path.c_str(), IN_MODIFY);
    if (wd < 0) {
        perror("Watcher: inotify_add_watch");
        close(fd);
        return;
    }

    char buf[sizeof(struct inotify_event) + NAME_MAX + 1];

    while (running.load()) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);

        // Warte bis zu einer Sekunde, damit wir periodisch running prüfen können.
        struct timeval tv {1, 0};
        int ret = select(fd + 1, &rfds, nullptr, nullptr, &tv);

        if (ret > 0 && FD_ISSET(fd, &rfds)) {
            ssize_t len = read(fd, buf, sizeof(buf));
            if (len <= 0) {
                continue;
            }

            // Alle neu angehängten Zeilen verarbeiten
            std::string line;
            while (std::getline(in, line)) {
                uint64_t watchTs = currentTimeUSec();
                json j = json::parse(line, nullptr, false);
                if (j.is_discarded()) {
                    continue;
                }

                uint64_t pktId = j.value("packet_id", 0ULL);
                uint64_t genTs = j.value("thread_ts_usec", 0ULL);

                json outObj = {
                    {"packet_id", pktId},
                    {"generator_ts", genTs},
                    {"watcher_ts", watchTs}
                };
                out << outObj.dump() << std::endl;
                queue.enqueue(Sample{pktId, genTs, watchTs});
            }
            // EOF-Flag zurücksetzen, damit getline nach neuen Daten funktioniert
            in.clear();
        }

        uint64_t totalCpu = 0, idleCpu = 0, procCpu = 0;
        if (readTotalCpu(totalCpu, idleCpu)) {
            double totalPercent = 0.0;
            double procPercent = 0.0;
            uint64_t totalDiff = totalCpu - prevTotalCpu;
            uint64_t idleDiff = idleCpu - prevIdleCpu;
            if (totalDiff > 0) {
                totalPercent = (double)(totalDiff - idleDiff) * 100.0 / totalDiff;
            }
            prevTotalCpu = totalCpu;
            prevIdleCpu = idleCpu;

            if (loggerPid > 0 && readProcCpu(loggerPid, procCpu)) {
                uint64_t procDiff = procCpu - prevProcCpu;
                if (totalDiff > 0) {
                    procPercent = (double)procDiff * 100.0 / totalDiff;
                }
                prevProcCpu = procCpu;
            }

            uint64_t sysMem = 0;
            uint64_t procMem = 0;
            readSystemMem(sysMem);
            if (loggerPid > 0) {
                // NEU: Speicher des gesamten Prozessbaums erfassen
                readProcMemTree(loggerPid, procMem);
            }

            json statObj = {
                {"timestamp", currentTimeUSec()},
                {"total_cpu", totalPercent},
                {"total_memory", sysMem},
                {"logger_cpu", procPercent},
                {"logger_memory", procMem}
            };
            out << statObj.dump() << std::endl;
        }
    }


    out.flush();
    inotify_rm_watch(fd, wd);
    close(fd);
}
