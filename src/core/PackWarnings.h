// SPDX-License-Identifier: MIT
#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace SkyEmote
{
    struct PackWarning
    {
        std::string pack;
        std::string message;
    };
    void LoadPackWarnings(const std::filesystem::path& data);
    void SetPackWarningsReady(bool ready);
    void QueuePackWarningsUpdate();
    std::shared_ptr<const std::vector<PackWarning>> GetPackWarnings();
}
