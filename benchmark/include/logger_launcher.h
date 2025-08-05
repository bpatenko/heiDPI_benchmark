#ifndef LOGGER_LAUNCHER_H
#define LOGGER_LAUNCHER_H
#include <sys/types.h>
#include <string>

pid_t launchPythonLogger(const std::string& moduleName,
                         const std::string& host,
                         int port,
                         const std::string& configPath);

pid_t launchBinaryLogger(const std::string& path,
                         const std::string& host,
                         int port,
                         const std::string& configPath);

#endif // LOGGER_LAUNCHER_H