#ifndef CONFIG_H
#define CONFIG_H

#include <string>

struct GeneratorParams {
    std::string host;
    int port;
    double rate;
    size_t message_size;
};

struct Config {
    std::string loggerType;      // "python" or "binary"
    std::string loggerModule;
    std::string loggerBinary;
    std::string loggerConfigPath;
    std::string outputFilePath;
    std::string scenarioPath;
    bool        straceEnabled;   // run logger via strace
    GeneratorParams generatorParams;
};

Config loadConfig(const std::string& path);
#endif // CONFIG_H