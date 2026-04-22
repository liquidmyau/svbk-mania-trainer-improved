#include "window.h"
#include "config.h"
#include <maniac/maniac.h>

static void help_marker(const char *desc) {
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

static void set_priority_class(int priority) {
    const auto proc = GetCurrentProcess();
    const auto old_priority = GetPriorityClass(proc);

    SetPriorityClass(proc, priority);

    debug("changed priority class from 0x%lx to 0x%lx", old_priority,
            GetPriorityClass(proc));
}

int main(int, char **) {
    std::string message;

    config::read_from_file(maniac::config);

    auto run = [&message](osu::Osu &osu) {
        maniac::osu = &osu;

        message = "waiting for beatmap...";

        maniac::block_until_playing();

        message = "found beatmap";

        std::vector<osu::HitObject> hit_objects;

        for (int i = 0; i < 10; i++) {
            try {
                hit_objects = osu.get_hit_objects();

                break;
            } catch (std::exception &err) {
                debug("get hit objects attempt %d failed: %s", i + 1, err.what());

                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        }

        if (hit_objects.empty()) {
            throw std::runtime_error("failed getting hit objects");
        }

        set_priority_class(HIGH_PRIORITY_CLASS);

        maniac::randomize(hit_objects, maniac::config.randomization_mean, maniac::config.randomization_stddev);

        if (maniac::config.humanization_type == maniac::config::STATIC_HUMANIZATION) {
            maniac::humanize_static(hit_objects, maniac::config.humanization_modifier);
        }

        if (maniac::config.humanization_type == maniac::config::DYNAMIC_HUMANIZATION) {
            maniac::humanize_dynamic(hit_objects, maniac::config.humanization_modifier);
        }

        maniac::apply_ur_jitter(hit_objects, maniac::config.ur_jitter_stddev);

        auto actions = maniac::to_actions(hit_objects, osu.get_game_time());

        message = "playing";

        maniac::play(actions);

        set_priority_class(NORMAL_PRIORITY_CLASS);
    };

    auto thread = std::jthread([&message, &run](const std::stop_token& token) {
        int retries_done = 0;
        bool was_auto_retry = false;

        while (!token.stop_requested()) {
            try {
                auto osu = osu::Osu();

                while (!token.stop_requested()) {
                    // At the start of each run: if the previous play was NOT
                    // triggered by our auto-retry, this is a new map — reset
                    // the retry counter. If it WAS our retry, keep the counter
                    // so we can track total retries for this map session.
                    if (!was_auto_retry) {
                        retries_done = 0;
                    }
                    was_auto_retry = false;

                    run(osu);

                    // After run() returns (map ended), check auto-retry
                    if (maniac::config.auto_retry && !token.stop_requested()) {
                        bool infinite = (maniac::config.auto_retry_count == 0);
                        bool has_retries_left = infinite || (retries_done < maniac::config.auto_retry_count);

                        if (has_retries_left) {
                            retries_done++;

                            // Build status message showing retry progress
                            if (infinite) {
                                message = "auto-retrying (#" + std::to_string(retries_done) + "/inf)";
                            } else {
                                message = "auto-retrying (#" + std::to_string(retries_done) + "/" + std::to_string(maniac::config.auto_retry_count) + ")";
                            }

                            // Trigger the retry: holds '~' key for 1.5 seconds
                            // This uses osu!'s built-in Quick Retry mechanism.
                            // Requires the '~' key to be bound as Quick Retry in osu! settings.
                            maniac::trigger_retry();

                            // Wait for the map to restart after retry.
                            // Timeout after 5 seconds — if the map doesn't restart,
                            // the user probably exited to song select manually.
                            auto retry_start = std::chrono::steady_clock::now();
                            bool map_restarted = false;

                            while (!token.stop_requested()) {
                                if (osu.is_playing()) {
                                    map_restarted = true;
                                    break;
                                }
                                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                                    std::chrono::steady_clock::now() - retry_start).count();
                                if (elapsed > 5) {
                                    debug("auto-retry: map did not restart within 5s, user likely left the map");
                                    break;
                                }
                                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                            }

                            if (map_restarted) {
                                was_auto_retry = true;
                                // Continue the inner loop → run(osu) will be called again.
                                // block_until_playing() inside run() will return immediately
                                // since the map is already playing.
                                continue;
                            } else {
                                // Map didn't restart — user left the map.
                                // Reset retry state and wait for a new map naturally.
                                retries_done = 0;
                                was_auto_retry = false;
                            }
                        }
                    }

                    // No auto-retry triggered (either disabled or retry count exhausted).
                    // Reset retry state for the next map.
                    retries_done = 0;
                }
            } catch (std::exception &err) {
                (message = err.what()).append(" (retrying in 2 seconds)");

                // Reset retry state on error — the Osu object will be recreated
                retries_done = 0;
                was_auto_retry = false;

                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        }
    });

    window::start([&message] {
        ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Always);
        ImGui::Begin("maniac", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize);

        ImGui::Separator();
        ImGui::Text("Status");

        ImVec4 status_color = ImVec4(0.76f, 0.78f, 0.80f, 1.0f);
        if (message.find("playing") != std::string::npos) {
            status_color = ImVec4(0.34f, 0.76f, 0.60f, 1.0f);
        } else if (message.find("auto-retrying") != std::string::npos) {
            status_color = ImVec4(0.90f, 0.65f, 0.20f, 1.0f); // amber for auto-retry
        } else if (message.find("failed") != std::string::npos || message.find("retrying") != std::string::npos) {
            status_color = ImVec4(0.95f, 0.55f, 0.35f, 1.0f);
        }
        ImGui::TextColored(status_color, "%s", message.c_str());
        ImGui::TextWrapped("Keep osu! open and load a beatmap. The trainer will auto-start when the map begins.");

        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::Separator();
        ImGui::Text("Timing behaviour");

        ImGui::BeginChild("##timing", ImVec2(0, 165), true);
        ImGui::TextDisabled("Humanization");
        ImGui::Combo("Type", &maniac::config.humanization_type, "Static\0Dynamic (new)\0\0");
        ImGui::SameLine();
        help_marker("Static: Density calculated per 1s chunk and applied to all hit objects in that chunk. Dynamic: Density 1s 'in front' of each hit object, applied individually.");

        ImGui::DragInt("Strength", &maniac::config.humanization_modifier, 1.0f, 0, 2000, "%d");
        ImGui::SameLine();
        help_marker("Advanced hit-time randomization based on hit density.");

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGui::TextDisabled("Base randomness");
        ImGui::DragInt("Mean", &maniac::config.randomization_mean, 0.2f, -20, 20, "%d ms");
        ImGui::DragInt("Stddev", &maniac::config.randomization_stddev, 0.2f, 0, 50, "%d ms");
        ImGui::EndChild();

        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::Separator();
        ImGui::Text("UR smoothing");
        ImGui::BeginChild("##ur", ImVec2(0, 85), true);
        ImGui::TextWrapped("Applies subtle per-note jitter so your unstable rate looks like a natural player instead of perfectly robotic inputs.");
        ImGui::SliderInt("Jitter stddev", &maniac::config.ur_jitter_stddev, 0, 20, "%d ms");
        ImGui::EndChild();

        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::Separator();
        ImGui::Text("Input shaping");
        ImGui::BeginChild("##input", ImVec2(0, 155), true);
        ImGui::DragInt("Latency compensation", &maniac::config.compensation_offset, 0.5f, -50, 50, "%d ms");
        ImGui::SameLine();
        help_marker("Adds constant value to all hit-times to compensate for input latency, slower processors, etc.");

        ImGui::Checkbox("Mirror mod", &maniac::config.mirror_mod);

        ImGui::DragInt("Tap time", &maniac::config.tap_time, 0.2f, 5, 120, "%d ms");
        ImGui::SameLine();
        help_marker("How long a key is held down for a single keypress, in milliseconds.");
        ImGui::EndChild();

        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::Separator();
        ImGui::Text("Auto-retry");
        ImGui::BeginChild("##retry", ImVec2(0, 100), true);
        ImGui::Checkbox("Enable auto-retry", &maniac::config.auto_retry);
        ImGui::SameLine();
        help_marker("Automatically retries the beatmap after failing or passing. Holds the '`' (tilde) key for 1.5s to trigger osu!'s Quick Retry. Make sure Quick Retry is bound to '~' in osu! settings.");

        ImGui::DragInt("Max retries", &maniac::config.auto_retry_count, 1.0f, 0, 999, "%d");
        ImGui::SameLine();
        help_marker("Maximum number of auto-retries per beatmap session. Set to 0 for infinite retries.");

        if (maniac::config.auto_retry_count == 0) {
            ImGui::TextDisabled("Mode: infinite retries");
        } else {
            ImGui::TextDisabled("Mode: up to %d retries", maniac::config.auto_retry_count);
        }
        ImGui::EndChild();

        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        ImGui::TextDisabled("maniac by fs-c, https://github.com/fs-c/maniac");

        ImGui::End();
    });

    config::write_to_file(maniac::config);

    thread.request_stop();

    return EXIT_SUCCESS;
}

