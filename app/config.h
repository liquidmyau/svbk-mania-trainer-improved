#pragma once

#include <maniac/maniac.h>
#include <nlohmann/json.hpp>

namespace config {
    using json = nlohmann::json;

    constexpr auto format_version = 3;
    constexpr auto file_name = "maniac-config.json";

    void read_from_file(struct maniac::config &c);
    void write_to_file(struct maniac::config &c);
}

void config::read_from_file(struct maniac::config &c) {
    std::ifstream file(file_name);

    try {
        const auto data = json::parse(file);

        c.tap_time = data.value("tap_time", c.tap_time);
        c.mirror_mod = data.value("mirror_mod", c.mirror_mod);
        c.compensation_offset = data.value("compensation_offset", c.compensation_offset);
        c.randomization_mean = data.value("randomization_mean", c.randomization_mean);
        c.randomization_stddev = data.value("randomization_stddev", c.randomization_stddev);
        c.humanization_type = data.value("humanization_type", c.humanization_type);
        c.humanization_modifier = data.value("humanization_modifier", c.humanization_modifier);
        c.ur_jitter_stddev = data.value("ur_jitter_stddev", c.ur_jitter_stddev);
        c.auto_retry = data.value("auto_retry", c.auto_retry);
        c.auto_retry_count = data.value("auto_retry_count", c.auto_retry_count);
        c.keys = data.value("keys", c.keys);

        debug("loaded config from file");
    } catch (json::parse_error &err) {
        debug("failed parsing config: '%s'", err.what());
    }
}

void config::write_to_file(struct maniac::config &c) {
    json data = {
            {"format_version", format_version},
            {"tap_time", c.tap_time},
            {"mirror_mod", c.mirror_mod},
            {"compensation_offset", c.compensation_offset},
            {"randomization_mean", c.randomization_mean},
            {"randomization_stddev", c.randomization_stddev},
            {"humanization_type", c.humanization_type},
            {"humanization_modifier", c.humanization_modifier},
            {"ur_jitter_stddev", c.ur_jitter_stddev},
            {"auto_retry", c.auto_retry},
            {"auto_retry_count", c.auto_retry_count},
            {"keys", c.keys}
    };

    std::ofstream file(file_name);

    file << data.dump(4) << std::endl;

    debug("wrote config to file");
}
