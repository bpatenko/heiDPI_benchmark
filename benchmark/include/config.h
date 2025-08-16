#ifndef CONFIG_H
#define CONFIG_H

#include <string>

struct GeneratorParams {
    std::string host;
    int port;
    double rate;
    size_t message_size;
};

struct EventProbabilities {
    double flow{0.25};
    double daemon{0.25};
    double error{0.25};
    double packet{0.25};
};

struct Config {
    std::string loggerType;      // "python" or "binary"
    std::string loggerModule;
    std::string loggerBinary;
    std::string loggerConfigPath;
    std::string outputFilePath;
    std::string scenarioPath;
    bool                straceEnabled;   // run logger via strace
    GeneratorParams     generatorParams;
    EventProbabilities  eventProbabilities;
};

Config loadConfig(const std::string& path);
#endif // CONFIG_H
