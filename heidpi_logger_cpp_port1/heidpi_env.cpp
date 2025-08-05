//
// Created by Bogdan Patenko on 29.07.25.
//

#include "heidpi_env.h"
#include <cstdlib>
#include <stdexcept>

namespace heidpi_env {

    EnvDefault::EnvDefault(const std::string& envvar, bool required)
        : envvar(envvar), required(required), default_value(std::nullopt)
    {
        const char* val = std::getenv(envvar.c_str());
        if (val != nullptr) {
            default_value = std::string(val);
            this->required = false;
        }
    }

    std::string EnvDefault::resolve(const std::string& cli_value) const {
        if (!cli_value.empty()) {
            return cli_value;
        }
        if (default_value.has_value()) {
            return default_value.value();
        }
        // Kein CLI-Wert und keine Env-Var gesetzt
        throw std::runtime_error("Missing required argument and no environment fallback: " + envvar);
    }

    EnvDefault env_default(const std::string& envvar) {
        return EnvDefault(envvar);
    }

} // namespace heidpi_env
