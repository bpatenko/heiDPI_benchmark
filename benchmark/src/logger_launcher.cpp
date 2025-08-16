#include "logger_launcher.h"
#include <unistd.h>
#include <iostream>
#include <filesystem>
#include <vector>

pid_t launchPythonLogger(const std::string& moduleName,
                         const std::string& host,
                         int port,
                         const std::string& configPath,
                         const std::vector<std::pair<std::string, std::string>>& eventParams) {
    pid_t pid = fork();
    if (pid == 0) {
        std::vector<std::string> args = {
            "python3",
            "-m", moduleName,
            "--host", host,
            "--port", std::to_string(port),
            "--write", ".",
            "--config", configPath
        };
        for (const auto& [k, v] : eventParams) {
            args.push_back(k);
            args.push_back(v);
        }
        std::vector<char*> cargs;
        for (auto& a : args) cargs.push_back(const_cast<char*>(a.c_str()));
        cargs.push_back(nullptr);
        execvp("python3", cargs.data());
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
                         const std::string& configPath,
                         const std::vector<std::pair<std::string, std::string>>& eventParams) {
    pid_t pid = fork();
    if (pid == 0) {
        std::vector<std::string> args = {
            path,
            "--host", host,
            "--port", std::to_string(port),
            "--write", ".",
            "--config", configPath
        };
        for (const auto& [k, v] : eventParams) {
            args.push_back(k);
            args.push_back(v);
        }
        std::vector<char*> cargs;
        for (auto& a : args) cargs.push_back(const_cast<char*>(a.c_str()));
        cargs.push_back(nullptr);
        execvp(path.c_str(), cargs.data());
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
                               const std::string& configPath,
                               const std::vector<std::pair<std::string, std::string>>& eventParams) {
    pid_t pid = fork();
    if (pid == 0) {
        std::vector<std::string> args = {
            "strace",
            "-f", "-c", "-o", "strace_summary.log",
            "python3", "-m", moduleName,
            "--host", host,
            "--port", std::to_string(port),
            "--write", ".",
            "--config", configPath
        };
        for (const auto& [k, v] : eventParams) {
            args.push_back(k);
            args.push_back(v);
        }
        std::vector<char*> cargs;
        for (auto& a : args) cargs.push_back(const_cast<char*>(a.c_str()));
        cargs.push_back(nullptr);
        execvp("strace", cargs.data());
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
                               const std::string& configPath,
                               const std::vector<std::pair<std::string, std::string>>& eventParams) {
    pid_t pid = fork();
    if (pid == 0) {
        std::string absPath = std::filesystem::absolute(path).string();
        std::vector<std::string> args = {
            "strace",
            "-f", "-c", "-o", "strace_summary.log",
            absPath,
            "--host", host,
            "--port", std::to_string(port),
            "--write", ".",
            "--config", configPath
        };
        for (const auto& [k, v] : eventParams) {
            args.push_back(k);
            args.push_back(v);
        }
        std::vector<char*> cargs;
        for (auto& a : args) cargs.push_back(const_cast<char*>(a.c_str()));
        cargs.push_back(nullptr);
        execvp("strace", cargs.data());
        std::cerr << "Failed to launch heiDPI logger binary with strace" << std::endl;
        _exit(1);
    } else if (pid < 0) {
        std::cerr << "Fork failed" << std::endl;
    }
    return pid;
}
