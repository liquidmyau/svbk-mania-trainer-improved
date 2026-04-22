#include <maniac/maniac.h>

namespace maniac {
    void reset_keys() {
        auto keys = osu::Osu::get_key_subset(config.keys, 9);
        for (auto key : keys) {
            Process::send_keypress(key, false);
        }
    }

        void block_until_playing() {
                while (true) {
                        if (osu->is_playing()) {
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(250));
                }
        }

        void play(const std::vector<Action> &actions) {
                reset_keys();

                size_t cur_i = 0;
                auto cur_time = 0;
                auto raw_actions = actions.data();
                auto total_actions = actions.size();

                while (cur_i < total_actions) {
                        if (!osu->is_playing())
                                return;

                        cur_time = osu->get_game_time();
                        while (cur_i < total_actions && (raw_actions + cur_i)->time <= cur_time) {
                                (raw_actions + cur_i)->execute();

                                cur_i++;
                        }

                        std::this_thread::sleep_for(std::chrono::nanoseconds(100));
                }
        }

    void trigger_retry() {
        debug("auto-retry: triggering retry by holding quick-retry key");

        // Get the virtual key code for the grave accent key (backtick `)
        // This is the physical key that osu! binds to Quick Retry by default.
        // VK_OEM_3 (0xC0) is the standard VK code for `~ on US keyboard layouts.
        // We use VkKeyScanEx for locale compatibility, falling back to VK_OEM_3.
        static auto layout = GetKeyboardLayout(0);
        short vk_result = VkKeyScanEx('`', layout);
        short retry_vk = (vk_result != -1) ? (vk_result & 0xFF) : 0xC0;

        // Brief delay before retry to avoid conflicting with manual key presses
        // and to allow osu! to transition out of the playing state cleanly.
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // Check if the map is already playing again (e.g. user manually retried)
        if (osu && osu->is_playing()) {
            debug("auto-retry: map already playing, skipping retry");
            return;
        }

        // Hold the quick-retry key down
        Process::send_scan_code(retry_vk, true);

        // Use try-catch to guarantee the key is released even if an error occurs
        // during the hold period. A stuck key would break osu! input.
        try {
            // Hold for 1.5 seconds — osu!'s quick retry requires a long press
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        } catch (...) {
            // Ensure key release on any exception
            Process::send_scan_code(retry_vk, false);
            throw;
        }

        // Release the key
        Process::send_scan_code(retry_vk, false);

        debug("auto-retry: quick-retry key released, waiting for map restart");
    }

    std::vector<Action> to_actions(std::vector<osu::HitObject> &hit_objects, int32_t min_time) {
        if (hit_objects.empty()) {
            return {};
        }

        const auto columns = std::max_element(hit_objects.begin(),
                                              hit_objects.end(), [](auto a, auto b) {
                    return a.column < b.column; })->column + 1;
        auto keys = osu::Osu::get_key_subset(config.keys, columns);

        if (config.mirror_mod)
            std::reverse(keys.begin(), keys.end());

        std::vector<Action> actions;
        actions.reserve(hit_objects.size() * 2);

        for (auto &hit_object : hit_objects) {
            if (hit_object.start_time < min_time)
                continue;

            if (!hit_object.is_slider)
                hit_object.end_time = hit_object.start_time + config.tap_time;

            actions.emplace_back(keys[hit_object.column], true,
                hit_object.start_time + config.compensation_offset);
            actions.emplace_back(keys[hit_object.column], false,
                hit_object.end_time + config.compensation_offset);
        }

        debug("converted %d hit objects to %d actions", hit_objects.size(), actions.size());

        std::sort(actions.begin(), actions.end());
        actions.erase(std::unique(actions.begin(), actions.end()), actions.end());

        return actions;
    }
}
