#include "custom_benchmarks.h"

#include <fstream>
#include <span>
#include "protocols/arithmetic_gmw/arithmetic_gmw_wire.h"
#include "protocols/bmr/bmr_wire.h"
#include "protocols/boolean_gmw/boolean_gmw_wire.h"
#include "protocols/share_wrapper.h"
#include "secure_type/secure_unsigned_integer.h"
#include "statistics/analysis.h"
#include "statistics/run_time_statistics.h"
#include "utility/config.h"

using namespace encrypto::motion;

std::vector<std::uint32_t> GetFileInput(const std::string& path) {
  std::ifstream infile;
  std::vector<std::uint32_t> input;
  std::uint32_t n;
  infile.open(path);
  if (!infile.is_open()) throw std::runtime_error("Could not open input file");
  while (infile >> n) input.push_back(n);
  infile.close();
  return input;
}

SecureUnsignedInteger LoadInput(PartyPointer& party, MpcProtocol protocol, const std::vector<std::uint32_t>& input) {
    // For simplicity, we assume 2 parties. Party 0 provides input, Party 1 provides dummy (0).
    // In a real benchmark, both might provide inputs.
    // Here we strictly follow the MP-SPDZ benchmark style (Sum of Party 0's inputs + Party 1's inputs).
    // To replicate standard behavior: each party inputs their OWN vector (if running as that party).
    // But since this is run as a single executable with ID, we input our share.
    
    // We'll create shares for both parties.
    SecureUnsignedInteger share_p0, share_p1;
    
    switch (protocol) {
        case MpcProtocol::kArithmeticGmw: {
            share_p0 = party->In<MpcProtocol::kArithmeticGmw>(input, 0);
            share_p1 = party->In<MpcProtocol::kArithmeticGmw>(input, 1);
            break;
        }
        case MpcProtocol::kBooleanGmw: {
            share_p0 = party->In<MpcProtocol::kBooleanGmw>(ToInput(input), 0);
            share_p1 = party->In<MpcProtocol::kBooleanGmw>(ToInput(input), 1);
            break;
        }
        default: throw std::invalid_argument("Protocol not supported for this benchmark");
    }
    return share_p0 + share_p1; 
}


RunTimeStatistics EvaluateSum(PartyPointer& party, MpcProtocol protocol,
    std::span<const std::uint32_t> input_command_line, const std::string& input_file_path, bool print_output) {
    
    std::vector<std::uint32_t> input = input_command_line.empty() ? GetFileInput(input_file_path) : std::vector<std::uint32_t>(input_command_line.begin(), input_command_line.end());
    
    SecureUnsignedInteger shared_sum = LoadInput(party, protocol, input);
    
    // Sum reduction
    std::vector<SecureUnsignedInteger> unsimd = shared_sum.Unsimdify();
    SecureUnsignedInteger total = unsimd[0];
    for (size_t i = 1; i < unsimd.size(); ++i) {
        total += unsimd[i];
    }
    
    auto output = total.Out();
    party->Run();
    auto result = output.As<std::uint32_t>();
    if (print_output) std::cout << "Sum Result = " << result << std::endl;
    party->Finish();
    return party->GetBackend()->GetRunTimeStatistics().front();
}

RunTimeStatistics EvaluateCount(PartyPointer& party, MpcProtocol protocol,
    std::span<const std::uint32_t> input_command_line, const std::string& input_file_path, uint32_t threshold, bool print_output) {
    
    std::vector<std::uint32_t> input = input_command_line.empty() ? GetFileInput(input_file_path) : std::vector<std::uint32_t>(input_command_line.begin(), input_command_line.end());
    
    SecureUnsignedInteger shared_vals = LoadInput(party, protocol, input);
    SecureUnsignedInteger limit = party->In<MpcProtocol::kArithmeticGmw>(std::vector<uint32_t>(input.size(), threshold), 0); // Constant threshold shared by P0
    
    // Comparison: shared_vals > threshold
    // operator> returns ShareWrapper (Boolean/Bit)
    ShareWrapper cmp = shared_vals > limit; 
    
    // Convert Boolean comparison result to Arithmetic Integer (0 or 1)
    ShareWrapper cmp_arith_share = cmp.Convert<MpcProtocol::kArithmeticGmw>();
    SecureUnsignedInteger cmp_arith(cmp_arith_share);
    
    // If True (1): Add 1 to count. If False (0): Add 0.
    SecureUnsignedInteger count_vec = cmp_arith;
    
    // Sum reduction
    std::vector<SecureUnsignedInteger> unsimd = count_vec.Unsimdify();
    SecureUnsignedInteger total = unsimd[0];
    for (size_t i = 1; i < unsimd.size(); ++i) {
        total += unsimd[i];
    }
    
    auto output = total.Out();
    party->Run();
    auto result = output.As<std::uint32_t>();
    if (print_output) std::cout << "Count Result = " << result << std::endl;
    party->Finish();
    return party->GetBackend()->GetRunTimeStatistics().front();
}

RunTimeStatistics EvaluateReLU(PartyPointer& party, MpcProtocol protocol,
    std::span<const std::uint32_t> input_command_line, const std::string& input_file_path, bool print_output) {
    
    std::vector<std::uint32_t> input = input_command_line.empty() ? GetFileInput(input_file_path) : std::vector<std::uint32_t>(input_command_line.begin(), input_command_line.end());
    
    SecureUnsignedInteger shared_vals = LoadInput(party, protocol, input);
    
    // ReLU: max(0, x)
    // But input is UnsignedInteger (so always > 0). Usually ReLU is for Signed integers. 
    // Assuming we might have interpreted these as signed (but loading as unsigned uint32_t). 
    // For standard ReLU benchmark in MPC papers, we often assume signed inputs or just benchmark the comparison+mux.
    // If input is truly unsigned, ReLU(x) = x. 
    // Let's assume we want to benchmark the COST of ReLU (Comparison + Mux). 
    // So we compare against a threshold (middle of range) or assume interpreted as signed.
    
    // Let's interpret uint32 as signed for comparison: > 2^31 ?
    // Check if MSB is 0 (positive) or 1 (negative).
    // If input is conceptually signed int32, then:
    // x > 0 check is equivalent to checking MSB (if MSB=1 -> negative).
    
    // Or we strictly implement: (x > 0) ? x : 0.
    // Note: SecureUnsignedInteger operator> is unsigned comparison. 
    
    SecureUnsignedInteger zero_vec = party->In<MpcProtocol::kArithmeticGmw>(std::vector<uint32_t>(input.size(), 0), 0);
    ShareWrapper is_positive = shared_vals > zero_vec; 
    
    // Convert to Arithmetic (0 or 1)
    ShareWrapper is_pos_arith_share = is_positive.Convert<MpcProtocol::kArithmeticGmw>();
    SecureUnsignedInteger is_pos_arith(is_pos_arith_share);

    // relu = is_pos * val
    SecureUnsignedInteger relu_result = is_pos_arith * shared_vals;
    
    // Output ALL results (or just one check to avoid massive I/O)
    // We'll output the sum of ReLUs just for verification
    std::vector<SecureUnsignedInteger> unsimd = relu_result.Unsimdify();
    SecureUnsignedInteger total = unsimd[0];
    for (size_t i = 1; i < unsimd.size(); ++i) {
        total += unsimd[i];
    }
    
    auto output = total.Out();
    party->Run();
    auto result = output.As<std::uint32_t>();
    
    if (print_output) std::cout << "ReLU Sum Result = " << result << std::endl;
    party->Finish();
    return party->GetBackend()->GetRunTimeStatistics().front();
}

RunTimeStatistics EvaluateMatMul(PartyPointer& party, MpcProtocol protocol,
    std::span<const std::uint32_t> input_command_line, const std::string& input_file_path, size_t dim, bool print_output) {
    
    // MatMul N x N
    // N is roughly dim^0.5 if we passed a flat vector, but typically we just generate Random data for benchmarks.
    // Let's assume input_command_line provides just N (dimension).
    
    size_t matrix_element_count = dim * dim;
    
    // Generate Random Matrices
    auto my_id = party->GetConfiguration()->GetMyId();
    std::vector<uint32_t> mat_a = GetFileInput(input_file_path); // Or random
    if (mat_a.empty()) mat_a = std::vector<uint32_t>(matrix_element_count, 1);
    
    // Share A (from P0) and B (from P1)
    // Actually standard MatMul benchmark: P0 provides A, P1 provides B.
    
    SecureUnsignedInteger a = party->In<MpcProtocol::kArithmeticGmw>(mat_a, 0); // P0 input
    SecureUnsignedInteger b = party->In<MpcProtocol::kArithmeticGmw>(mat_a, 1); // P1 input (reuse same data for simplicity)

    // Naive O(N^3) MatMul
    // A [N x N] * B [N x N] = C [N x N]
    // C_ij = Sum_k (A_ik * B_kj)

    // Unsimdify to get individual elements A_ij and B_ij
    auto a_elems = a.Unsimdify();
    auto b_elems = b.Unsimdify();

    std::vector<ShareWrapper> c_elems;
    c_elems.reserve(matrix_element_count);

    for (size_t i = 0; i < dim; ++i) {
        for (size_t j = 0; j < dim; ++j) {
            ShareWrapper sum_k;
            // First k=0
            sum_k = a_elems[i * dim + 0] * b_elems[0 * dim + j];
            for (size_t k = 1; k < dim; ++k) {
                sum_k = sum_k + (a_elems[i * dim + k] * b_elems[k * dim + j]);
            }
            c_elems.push_back(sum_k);
        }
    }
    
    ShareWrapper c_flat = ShareWrapper::Simdify(c_elems);
    auto out = c_flat.Out();
    party->Run();
    auto result = out.As<std::vector<std::uint32_t>>();
    
    if (print_output) std::cout << "MatMul[0] = " << result[0] << std::endl;
    party->Finish();
    return party->GetBackend()->GetRunTimeStatistics().front();
}
