#pragma once

#include <span>
#include "base/party.h"
#include "secure_type/secure_unsigned_integer.h"
#include "statistics/run_time_statistics.h"

encrypto::motion::RunTimeStatistics EvaluateSum(
    encrypto::motion::PartyPointer& party, encrypto::motion::MpcProtocol protocol,
    std::span<const std::uint32_t> input_command_line, const std::string& input_file_path,
    bool print_output);

encrypto::motion::RunTimeStatistics EvaluateCount(
    encrypto::motion::PartyPointer& party, encrypto::motion::MpcProtocol protocol,
    std::span<const std::uint32_t> input_command_line, const std::string& input_file_path,
    uint32_t threshold, bool print_output);

encrypto::motion::RunTimeStatistics EvaluateReLU(
    encrypto::motion::PartyPointer& party, encrypto::motion::MpcProtocol protocol,
    std::span<const std::uint32_t> input_command_line, const std::string& input_file_path,
    bool print_output);

std::vector<std::uint32_t> GetFileInput(const std::string& path);
