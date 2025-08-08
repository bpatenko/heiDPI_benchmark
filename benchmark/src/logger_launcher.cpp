#include "logger_launcher.h"
#include <unistd.h>
#include <iostream>
#include <filesystem>

pid_t launchPythonLogger(const std::string& moduleName,
                         const std::string& host,
                         int port,
                         const std::string& configPath) {
    pid_t pid = fork();
    if (pid == 0) {
        execlp("python3", "python3",
               "-m", moduleName.c_str(),
               "--host", host.c_str(),
               "--port", std::to_string(port).c_str(),
               "--write", ".",
               "--config", configPath.c_str(),
               "--show-flow-events", "1",
               nullptr);
        std::cerr << "Failed to launch heiDPI_logger" << std::endl;
        _exit(1);
    } else if (pid < 0) {
        std::cerr << "Fork failed" << std::endl;
    }
    return pid;
}

pid_t launchBinaryLogger(const std::string& path,
                         const std::string& host,
                         int port,
                         const std::string& configPath) {
    pid_t pid = fork();
    if (pid == 0) {
        execl(path.c_str(), path.c_str(),
              "--host", host.c_str(),
              "--port", std::to_string(port).c_str(),
              "--write", ".",
              "--config", configPath.c_str(),
              "--show-flow-events", "1",
              nullptr);
        std::cerr << "Failed to launch heiDPI logger binary" << std::endl;
        _exit(1);
    } else if (pid < 0) {
        std::cerr << "Fork failed" << std::endl;
    }
    return pid;
}

pid_t launchPythonLoggerStrace(const std::string& moduleName,
                               const std::string& host,
                               int port,
                               const std::string& configPath) {
    pid_t pid = fork();
    if (pid == 0) {
        execlp("strace", "strace",
               "-f", "-c", "-o", "strace_summary.log",
               "python3", "-m", moduleName.c_str(),
               "--host", host.c_str(),
               "--port", std::to_string(port).c_str(),
               "--write", ".",
               "--config", configPath.c_str(),
               "--show-flow-events", "1",
               nullptr);
        std::cerr << "Failed to launch heiDPI_logger with strace" << std::endl;
        _exit(1);
    } else if (pid < 0) {
        std::cerr << "Fork failed" << std::endl;
    }
    return pid;
}

pid_t launchBinaryLoggerStrace(const std::string& path,
                               const std::string& host,
                               int port,
                               const std::string& configPath) {
    pid_t pid = fork();
    if (pid == 0) {
        std::string absPath = std::filesystem::absolute(path).string();
        execlp("strace", "strace",
               "-f", "-c", "-o", "strace_summary.log",
               absPath.c_str(),
               "--host", host.c_str(),
               "--port", std::to_string(port).c_str(),
               "--write", ".",
               "--config", configPath.c_str(),
               "--show-flow-events", "1",
               nullptr);
        std::cerr << "Failed to launch heiDPI logger binary with strace" << std::endl;
        _exit(1);
    } else if (pid < 0) {
        std::cerr << "Fork failed" << std::endl;
    }
    return pid;
}
