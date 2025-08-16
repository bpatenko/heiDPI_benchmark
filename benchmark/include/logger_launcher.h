#ifndef LOGGER_LAUNCHER_H
#define LOGGER_LAUNCHER_H
#include <sys/types.h>
#include <string>
#include <vector>
#include <utility>

pid_t launchPythonLogger(const std::string& moduleName,
                         const std::string& host,
                         int port,
                         const std::string& configPath,
                         const std::vector<std::pair<std::string, std::string>>& eventParams);

pid_t launchBinaryLogger(const std::string& path,
                         const std::string& host,
                         int port,
                         const std::string& configPath,
                         const std::vector<std::pair<std::string, std::string>>& eventParams);

pid_t launchPythonLoggerStrace(const std::string& moduleName,
                               const std::string& host,
                               int port,
                               const std::string& configPath,
                               const std::vector<std::pair<std::string, std::string>>& eventParams);

pid_t launchBinaryLoggerStrace(const std::string& path,
                               const std::string& host,
                               int port,
                               const std::string& configPath,
                               const std::vector<std::pair<std::string, std::string>>& eventParams);

#endif // LOGGER_LAUNCHER_H
