#pragma once

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include "Core/Utils/Path.hpp"
#include "Domain/PrototypeBackend.hpp"

namespace SST::Prototypes::WindowChromeLab
{
    inline void ValidateInput(const PrototypeInput &input)
    {
        if (!std::isfinite(input.voltage) || input.voltage < 0.1 || input.voltage > 5.0 ||
            !std::isfinite(input.temperatureKelvin) || input.temperatureKelvin < 50 || input.temperatureKelvin > 900)
            throw std::runtime_error("Voltage must be 0.1–5 V and temperature 50–900 K.");
    }

    inline std::string SerializeInput(const PrototypeInput &input)
    {
        ValidateInput(input);
        return nlohmann::json{{"schema_version", 1}, {"voltage", input.voltage},
            {"temperature_kelvin", input.temperatureKelvin}}.dump(2);
    }

    inline PrototypeInput ParseInput(const std::string &text)
    {
        const auto json = nlohmann::json::parse(text);
        if (json.at("schema_version").get<int>() != 1)
            throw std::runtime_error("Unsupported document schema.");
        PrototypeInput input{json.at("voltage").get<double>(), json.at("temperature_kelvin").get<double>()};
        ValidateInput(input);
        return input;
    }

    struct PrototypeDocument
    {
        PrototypeInput input;
        PrototypeResult result = PrototypeBackend::Evaluate(input);
        Path path;
        bool dirty = false;
        std::uint64_t revision = 0;

        void Edit(PrototypeInput value)
        {
            ValidateInput(value);
            input = value;
            ++revision;
            dirty = true;
        }

        void Saved(const Path &target, std::uint64_t savedRevision)
        {
            path = target;
            if (revision == savedRevision)
                dirty = false;
        }

        bool Opened(PrototypeInput value, const Path &source, std::uint64_t expectedRevision)
        {
            if (revision != expectedRevision)
                return false;
            Edit(value);
            result = PrototypeBackend::Evaluate(input);
            path = source;
            dirty = false;
            return true;
        }
    };
}
