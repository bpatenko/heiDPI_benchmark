//
// Created by Bogdan Patenko on 29.07.25.
//

#ifndef HEIDPI_CPP_HEIDPI_ENV_H
#define HEIDPI_CPP_HEIDPI_ENV_H

#include <string>
#include <optional>
#include <unordered_map>

namespace heidpi_env {

    struct EnvDefault {
        std::string envvar;
        bool required;
        std::optional<std::string> default_value;

        EnvDefault(const std::string& envvar, bool required = true);

        std::string resolve(const std::string& cli_value) const;
    };

    // Optional Wrapper für syntaktischen Komfort wie `env_default("ENV_VAR_NAME")`
    EnvDefault env_default(const std::string& envvar);

} // namespace heidpi_env


#endif //HEIDPI_CPP_HEIDPI_ENV_H