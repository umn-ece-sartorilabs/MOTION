// SPDX-License-Identifier: MIT
//
// Enhanced MOTION benchmark: SUM, COUNT (>50), ReLU, BILLIONAIRE.
// Added debug/reveal instrumentation. Uses ShareWrapper + ArithmeticGmw only.
// FIXED: Corrected multi-party input sharing for all benchmarks.

#include "base/party.h"
#include "communication/communication_layer.h"
#include "communication/tcp_transport.h"
#include "statistics/run_time_statistics.h"
#include "protocols/share_wrapper.h"
#include "utility/typedefs.h"

#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <stdexcept>
#include <random>
#include <string>
#include <algorithm>
#include <tuple>

using namespace encrypto::motion;

constexpr int kMaxRetries = 10;
constexpr int kRetryDelayMs = 500;

enum class OperationType { SUM, COUNT, RELU, BILLIONAIRE };

struct BenchmarkConfig {
    size_t my_id;
    std::vector<std::tuple<size_t, std::string, uint16_t>> all_parties;
    size_t vector_size;
    OperationType operation;
    size_t repetitions;
    bool debug = false;
};

std::tuple<size_t, std::string, uint16_t> ParsePartyInfo(const std::string& party_info) {
    auto pos1 = party_info.find(',');
    auto pos2 = party_info.rfind(',');
    if (pos1 == std::string::npos || pos2 == std::string::npos || pos1 == pos2) {
        throw std::runtime_error("Invalid party info format. Expected: party-id,IP,port");
    }
    size_t party_id = std::stoul(party_info.substr(0, pos1));
    std::string host = party_info.substr(pos1 + 1, pos2 - pos1 - 1);
    uint16_t port = static_cast<uint16_t>(std::stoi(party_info.substr(pos2 + 1)));
    return {party_id, host, port};
}

OperationType ParseOperation(const std::string& op_str) {
    std::string s = op_str;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    if (s == "sum") return OperationType::SUM;
    if (s == "count") return OperationType::COUNT;
    if (s == "relu") return OperationType::RELU;
    if (s == "billionaire") return OperationType::BILLIONAIRE;
    throw std::runtime_error("Invalid operation. Use: sum, count, relu, billionaire");
}

std::string OperationToString(OperationType op) {
    switch (op) {
        case OperationType::SUM: return "Sum";
        case OperationType::COUNT: return "Count";
        case OperationType::RELU: return "ReLU";
        case OperationType::BILLIONAIRE: return "Billionaire";
        default: return "Unknown";
    }
}

BenchmarkConfig ParseArguments(int argc, char** argv) {
    if (argc < 6) {
        std::cerr << "Usage: " << argv[0]
                  << " <my-id> <party0-info> <party1-info> <operation> <vector-size> [repetitions] [--debug|-d]\n";
        throw std::runtime_error("Invalid arguments");
    }
    BenchmarkConfig config;
    config.my_id = std::stoul(argv[1]);
    config.all_parties.push_back(ParsePartyInfo(argv[2]));
    config.all_parties.push_back(ParsePartyInfo(argv[3]));
    if (config.all_parties.size() != 2) throw std::runtime_error("Exactly 2 parties required");
    bool has0 = false, has1 = false;
    for (auto& [id, host, port] : config.all_parties) {
        if (id == 0) has0 = true;
        if (id == 1) has1 = true;
    }
    if (!has0 || !has1) throw std::runtime_error("Need parties 0 and 1");
    if (config.my_id > 1) throw std::runtime_error("My ID must be 0 or 1");
    config.operation = ParseOperation(argv[4]);
    config.vector_size = std::stoul(argv[5]);
    config.repetitions = 1;
    
    for (int i = 6; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--debug" || arg == "-d") {
            config.debug = true;
        } else {
            try { 
                config.repetitions = std::stoul(arg); 
            } catch (...) {
                std::cerr << "Warning: Ignoring invalid argument: " << arg << std::endl;
            }
        }
    }
    
    return config;
}

std::vector<std::uint8_t> RandomUnsigned(size_t size, size_t party_id, uint8_t min_val, uint8_t max_val) {
    uint32_t seed = 1000 + party_id * 12345;
    std::mt19937 gen(seed);
    std::uniform_int_distribution<std::uint8_t> dis(min_val, max_val);
    std::vector<std::uint8_t> v(size);
    for (size_t i = 0; i < size; ++i) v[i] = dis(gen);
    return v;
}

std::vector<std::int8_t> RandomSigned(size_t size, size_t party_id, int8_t min_val, int8_t max_val) {
    uint32_t seed = 1000 + party_id * 12345;
    std::mt19937 gen(seed);
    std::uniform_int_distribution<std::int8_t> dis(min_val, max_val);
    std::vector<std::int8_t> v(size);
    for (size_t i = 0; i < size; ++i) v[i] = dis(gen);
    return v;
}

std::unique_ptr<communication::CommunicationLayer> SetupCommunication(const BenchmarkConfig& config) {
    communication::TcpPartiesConfiguration tcp_config(2);
    for (const auto& [party_id, host, port] : config.all_parties) {
        tcp_config.at(party_id) = {host, port};
    }
    communication::TcpSetupHelper helper(config.my_id, tcp_config);
    std::unique_ptr<communication::CommunicationLayer> comm_layer;
    for (int attempt = 1; attempt <= kMaxRetries; ++attempt) {
        try {
            std::cout << "Setting up connections (attempt " << attempt << ")..." << std::endl;
            comm_layer = std::make_unique<communication::CommunicationLayer>(
                config.my_id, helper.SetupConnections());
            std::cout << "Connection setup successful!" << std::endl;
            break;
        } catch (const std::exception& e) {
            std::cerr << "Connection attempt " << attempt << " failed: " << e.what() << std::endl;
            if (attempt == kMaxRetries) throw std::runtime_error("Max connection retries reached");
            std::this_thread::sleep_for(std::chrono::milliseconds(kRetryDelayMs));
        }
    }
    return comm_layer;
}

RunTimeStatistics RunSumBenchmark(PartyPointer& party, size_t vector_size, bool debug) {
    auto start = std::chrono::high_resolution_clock::now();
    size_t my_party_id = party->GetConfiguration()->GetMyId();

    if (debug) {
        std::cout << "\n=== ENHANCED DEBUG INFORMATION ===" << std::endl;
        std::cout << "[DEBUG] My party ID: " << my_party_id << std::endl;
        std::cout << "[DEBUG] Vector size: " << vector_size << std::endl;
    }
    
    // Generate local input for this party
    auto my_local_input = RandomUnsigned(vector_size, my_party_id, 1, 100);
    
    uint64_t expected_local_sum = 0;
    for (auto v : my_local_input) expected_local_sum += v;
    
    if (debug) {
        std::cout << "[DEBUG] Party " << my_party_id << " local input (first 16): ";
        for (size_t i = 0; i < std::min(vector_size, size_t(16)); ++i) {
            std::cout << static_cast<int>(my_local_input[i]) << " ";
        }
        std::cout << std::endl;
        std::cout << "[DEBUG] Party " << my_party_id << " expected local sum = " << expected_local_sum << std::endl;
        
        uint64_t verify_sum = 0;
        for (size_t i = 0; i < vector_size; ++i) verify_sum += my_local_input[i];
        std::cout << "[DEBUG] Party " << my_party_id << " verified total local sum = " << verify_sum << std::endl;
    }

    std::cout << "Party " << my_party_id << ": SUM-REDUCE on " << vector_size 
              << " integers (1-100), both parties contribute" << std::endl;

    // CORRECT INPUT SHARING: Each party provides its own input or a dummy vector.
    auto input_p0 = (my_party_id == 0) ? my_local_input : std::vector<std::uint8_t>(vector_size, 0);
    auto input_p1 = (my_party_id == 1) ? my_local_input : std::vector<std::uint8_t>(vector_size, 0);

    // Create shares for each party's input. Both parties must execute this.
    ShareWrapper share_p0 = party->In<MpcProtocol::kArithmeticGmw>(input_p0, 0);
    ShareWrapper share_p1 = party->In<MpcProtocol::kArithmeticGmw>(input_p1, 1);

    if (debug) {
        std::cout << "[DEBUG] Created arithmetic shares for both parties" << std::endl;
    }

    // Combine the inputs from both parties element-wise
    ShareWrapper combined_simd = share_p0 + share_p1;
    
    auto elems = combined_simd.Unsimdify();
    if (elems.empty()) throw std::runtime_error("Empty input in SUM");
    
    if (debug) {
        std::cout << "[DEBUG] Unsimdified into " << elems.size() << " elements" << std::endl;
    }

    ShareWrapper total = elems[0];
    for (size_t i = 1; i < elems.size(); ++i) {
        total = total + elems[i];
    }

    auto out = total.Out();
    
    if (debug) std::cout << "[DEBUG] About to call party->Run()..." << std::endl;
    party->Run();
    if (debug) std::cout << "[DEBUG] party->Run() completed, now reconstructing..." << std::endl;
    
    auto result = out.As<std::vector<std::uint8_t>>();
    
    if (debug) {
        std::cout << "[DEBUG] Reconstruction completed. Result vector size: " << result.size() << std::endl;
        if (!result.empty()) {
            std::cout << "[DEBUG] Raw reconstructed value: " << static_cast<int>(result[0]) << std::endl;
            std::cout << "[DEBUG] Party " << my_party_id << " local sum: " << expected_local_sum << std::endl;
            std::cout << "[DEBUG] MPC result (sum of both parties): " << static_cast<int>(result[0]) << std::endl;
        }
        std::cout << "[DEBUG] =========================" << std::endl;
    }
    
    std::cout << "Sum result = " << static_cast<int>(result[0]) << std::endl;
    
    party->Finish();
    auto end = std::chrono::high_resolution_clock::now();
    std::cout << "Sum execution time: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()
              << " ms" << std::endl;
    return party->GetBackend()->GetRunTimeStatistics().front();
}

RunTimeStatistics RunCountBenchmark(PartyPointer& party, size_t vector_size, bool debug) {
    auto start = std::chrono::high_resolution_clock::now();
    size_t my_id = party->GetConfiguration()->GetMyId();
    
    auto my_local_input = RandomUnsigned(vector_size, my_id, 0, 100);
    
    uint8_t local_clear_count = 0;
    for (auto v : my_local_input) if (v > 50) local_clear_count++;
    
    std::cout << "Party " << my_id << ": COUNT elements > 50 from " << vector_size << " integers (0-100)" << std::endl;
    
    if (debug) {
        std::cout << "[DEBUG] Party " << my_id << " local input (first up to 8): ";
        for (size_t i = 0; i < std::min(vector_size, size_t(8)); ++i) {
            std::cout << static_cast<int>(my_local_input[i]) << " ";
        }
        std::cout << "... local count >50 = " << static_cast<int>(local_clear_count) << "\n";
    }
    
    // CORRECT INPUT SHARING
    auto input_p0 = (my_id == 0) ? my_local_input : std::vector<std::uint8_t>(vector_size, 0);
    auto input_p1 = (my_id == 1) ? my_local_input : std::vector<std::uint8_t>(vector_size, 0);
    ShareWrapper share_p0 = party->In<MpcProtocol::kArithmeticGmw>(input_p0, 0);
    ShareWrapper share_p1 = party->In<MpcProtocol::kArithmeticGmw>(input_p1, 1);
    ShareWrapper combined_inputs = share_p0 + share_p1;

    // Threshold is correctly input by party 0
    std::vector<std::uint8_t> threshold_vals(vector_size, (my_id == 0) ? 50 : 0);
    ShareWrapper threshold = party->In<MpcProtocol::kArithmeticGmw>(threshold_vals, 0);

    auto in_elems = combined_inputs.Unsimdify();
    auto th_elems = threshold.Unsimdify();

    ShareWrapper count = party->In<MpcProtocol::kArithmeticGmw>(std::vector<std::uint8_t>{0}, 0);
    ShareWrapper arithmetic_one = party->In<MpcProtocol::kArithmeticGmw>(std::vector<std::uint8_t>{1}, 0);

    for (size_t i = 0; i < in_elems.size(); ++i) {
        ShareWrapper gt = in_elems[i] > th_elems[i];
        ShareWrapper gt_arithmetic = gt * arithmetic_one;
        count = count + gt_arithmetic;
    }

    auto out = count.Out();
    party->Run();
    
    auto result = out.As<std::vector<std::uint8_t>>();
    std::cout << "Count result = " << static_cast<int>(result[0]) << std::endl;
    
    if (debug) {
        std::cout << "[DEBUG] Party " << my_id << " contributed " << static_cast<int>(local_clear_count) 
                  << " to the total count of " << static_cast<int>(result[0]) << "\n";
    }

    party->Finish();
    auto end = std::chrono::high_resolution_clock::now();
    std::cout << "Count execution time: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()
              << " ms" << std::endl;
    return party->GetBackend()->GetRunTimeStatistics().front();
}

RunTimeStatistics RunReLUBenchmark(PartyPointer& party, size_t vector_size, bool debug) {
    auto start = std::chrono::high_resolution_clock::now();
    size_t my_id = party->GetConfiguration()->GetMyId();
    auto my_local_input_signed = RandomSigned(vector_size, my_id, -50, 50);

    if (debug) {
        std::cout << "[DEBUG] Party " << my_id << " local signed input (first up to 8): ";
        for (size_t i = 0; i < std::min(vector_size, size_t(8)); ++i) {
            std::cout << static_cast<int>(my_local_input_signed[i]) << " ";
        }
        std::cout << "\n";
        
        int64_t local_relu_sum = 0;
        for (auto v : my_local_input_signed) {
            if (v > 0) local_relu_sum += v;
        }
        std::cout << "[DEBUG] Expected local ReLU sum = " << local_relu_sum << "\n";
    }

    std::cout << "Party " << my_id << ": ReLU max(0,x) on " << vector_size << " signed integers (-50 to +50)" << std::endl;

    // Cast signed local input to unsigned for bitwise-compatible sharing
    std::vector<std::uint8_t> my_local_input_unsigned(vector_size);
    for (size_t i = 0; i < vector_size; ++i) {
        my_local_input_unsigned[i] = static_cast<std::uint8_t>(my_local_input_signed[i]);
    }

    // CORRECT INPUT SHARING
    auto input_p0 = (my_id == 0) ? my_local_input_unsigned : std::vector<std::uint8_t>(vector_size, 0);
    auto input_p1 = (my_id == 1) ? my_local_input_unsigned : std::vector<std::uint8_t>(vector_size, 0);
    ShareWrapper share_p0 = party->In<MpcProtocol::kArithmeticGmw>(input_p0, 0);
    ShareWrapper share_p1 = party->In<MpcProtocol::kArithmeticGmw>(input_p1, 1);
    ShareWrapper combined_inputs = share_p0 + share_p1;

    // Test for non-negativity in two's complement. This value should be public.
    // We input it from party 0.
    std::vector<std::uint8_t> sign_thresh_vals(vector_size, (my_id == 0) ? (1u << 7) : 0);
    ShareWrapper sign_thresh = party->In<MpcProtocol::kArithmeticGmw>(sign_thresh_vals, 0);

    auto elems = combined_inputs.Unsimdify();
    auto thresh_elems = sign_thresh.Unsimdify();

    std::vector<ShareWrapper> relu_results;
    for (size_t i = 0; i < elems.size(); ++i) {
        // non_negative = 1 if combined_input is not negative, 0 otherwise
        ShareWrapper non_negative = thresh_elems[i] > elems[i];
        // ReLU = non_negative * combined_input
        ShareWrapper relu_i = non_negative * elems[i];
        relu_results.push_back(relu_i);
    }

    if (relu_results.empty()) throw std::runtime_error("No ReLU elements");

    ShareWrapper total = relu_results[0];
    for (size_t i = 1; i < relu_results.size(); ++i) {
        total = total + relu_results[i];
    }

    auto out = total.Out();
    party->Run();
    auto result = out.As<std::vector<std::uint8_t>>();
    // The result is a sum of unsigned values, which needs to be cast back to signed for interpretation
    std::cout << "ReLU sum result = " << static_cast<int8_t>(result[0]) << std::endl;

    party->Finish();
    auto end = std::chrono::high_resolution_clock::now();
    std::cout << "ReLU execution time: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()
              << " ms" << std::endl;
    return party->GetBackend()->GetRunTimeStatistics().front();
}

RunTimeStatistics RunBillionaireBenchmark(PartyPointer& party, size_t vector_size, bool debug) {
    auto start = std::chrono::high_resolution_clock::now();
    size_t my_id = party->GetConfiguration()->GetMyId();

    // 1. Generate three separate input vectors for this party
    auto my_cash = RandomUnsigned(vector_size, my_id, 10, 100);
    auto my_prop = RandomUnsigned(vector_size, my_id, 10, 100);
    auto my_stock = RandomUnsigned(vector_size, my_id, 10, 100);
    
    // A dummy vector of zeros for the input sharing pattern
    auto dummy_vec = std::vector<std::uint8_t>(vector_size, 0);

    std::cout << "Party " << my_id
              << ": BILLIONAIRE wealth comparison on " << vector_size << " values" << std::endl;

    // 2. Share all six input vectors correctly
    // Party 0's shares
    ShareWrapper p0_cash_share = party->In<MpcProtocol::kArithmeticGmw>((my_id == 0) ? my_cash : dummy_vec, 0);
    ShareWrapper p0_prop_share = party->In<MpcProtocol::kArithmeticGmw>((my_id == 0) ? my_prop : dummy_vec, 0);
    ShareWrapper p0_stock_share = party->In<MpcProtocol::kArithmeticGmw>((my_id == 0) ? my_stock : dummy_vec, 0);

    // Party 1's shares
    ShareWrapper p1_cash_share = party->In<MpcProtocol::kArithmeticGmw>((my_id == 1) ? my_cash : dummy_vec, 1);
    ShareWrapper p1_prop_share = party->In<MpcProtocol::kArithmeticGmw>((my_id == 1) ? my_prop : dummy_vec, 1);
    ShareWrapper p1_stock_share = party->In<MpcProtocol::kArithmeticGmw>((my_id == 1) ? my_stock : dummy_vec, 1);

    // 3. Perform additions and comparisons on the packed SIMD shares directly
    // This is much more efficient than using a C++ loop.
    ShareWrapper alice_total = p0_cash_share + p0_prop_share + p0_stock_share;
    ShareWrapper bob_total = p1_cash_share + p1_prop_share + p1_stock_share;

    ShareWrapper p0_is_richer = alice_total > bob_total;   // Boolean SIMD share
    ShareWrapper p1_is_richer = bob_total > alice_total;   // Boolean SIMD share
    
    // 4. To count the wins, convert the boolean results to arithmetic and sum them
    ShareWrapper arithmetic_one = party->In<MpcProtocol::kArithmeticGmw>(std::vector<std::uint8_t>(vector_size, 1), 0);
    
    // These are now arithmetic SIMD shares containing 0s and 1s
    ShareWrapper p0_wins_arith = p0_is_richer * arithmetic_one;
    ShareWrapper p1_wins_arith = p1_is_richer * arithmetic_one;

    // Sum the wins for each party across the vector
    auto p0_wins_elems = p0_wins_arith.Unsimdify();
    ShareWrapper p0_total_wins = p0_wins_elems[0];
    for(size_t i = 1; i < p0_wins_elems.size(); ++i) {
        p0_total_wins += p0_wins_elems[i];
    }
    
    auto p1_wins_elems = p1_wins_arith.Unsimdify();
    ShareWrapper p1_total_wins = p1_wins_elems[0];
    for(size_t i = 1; i < p1_wins_elems.size(); ++i) {
        p1_total_wins += p1_wins_elems[i];
    }

    // The goal is to get the total number of wins for both parties
    ShareWrapper total_wins = p0_total_wins + p1_total_wins;

    auto out = total_wins.Out();
    party->Run();
    auto result = out.As<std::vector<std::uint8_t>>();
    std::cout << "Billionaire total comparisons (P0 wins + P1 wins) = " << static_cast<int>(result[0]) << std::endl;

    party->Finish();
    auto end = std::chrono::high_resolution_clock::now();
    std::cout << "Billionaire execution time: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()
              << " ms" << std::endl;
    return party->GetBackend()->GetRunTimeStatistics().front();
}

int main(int argc, char** argv) {
    try {
        auto config = ParseArguments(argc, argv);

        std::cout << "=== MOTION " << OperationToString(config.operation)
                  << " Benchmark (FIXED VERSION) ===\nMy ID: " << config.my_id << "\nOperation: "
                  << OperationToString(config.operation) << "\nVector size: " << config.vector_size
                  << "\nRepetitions: " << config.repetitions 
                  << "\nDebug mode: " << (config.debug ? "ON" : "OFF") << "\nAll parties:\n";
        for (const auto& [pid, host, port] : config.all_parties) {
            std::string me = (pid == config.my_id) ? " (me)" : "";
            std::cout << "  Party " << pid << ": " << host << ":" << port << me << "\n";
        }
        std::cout << "===============================\n";

        for (size_t rep = 0; rep < config.repetitions; ++rep) {
            std::cout << "\n--- Repetition " << (rep + 1) << "/" << config.repetitions << " ---\n";
            auto comm = SetupCommunication(config);
            auto party = std::make_unique<Party>(std::move(comm));
            party->GetConfiguration()->SetLoggingEnabled(false);
            party->GetConfiguration()->SetOnlineAfterSetup(true);

            RunTimeStatistics stats;
            switch (config.operation) {
                case OperationType::SUM:
                    stats = RunSumBenchmark(party, config.vector_size, config.debug);
                    break;
                case OperationType::COUNT:
                    stats = RunCountBenchmark(party, config.vector_size, config.debug);
                    break;
                case OperationType::RELU:
                    stats = RunReLUBenchmark(party, config.vector_size, config.debug);
                    break;
                case OperationType::BILLIONAIRE:
                    stats = RunBillionaireBenchmark(party, config.vector_size, config.debug);
                    break;
            }

            std::cout << "Repetition " << (rep + 1) << " completed.\n";
            std::cout << "Statistics: " << stats.PrintHumanReadable() << "\n";
        }

        std::cout << "\n=== Benchmark Complete ===\n";
        return EXIT_SUCCESS;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}