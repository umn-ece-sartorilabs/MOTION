#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include "motioncore/base/backend.h"
#include "motioncore/base/configuration.h"
#include "motioncore/base/party.h"
#include "motioncore/communication/tcp_transport.h"
#include "motioncore/protocols/share_wrapper.h"
#include "motioncore/secure_type/secure_unsigned_integer.h"
#include "motioncore/utility/helpers.h"
#include "motioncore/utility/logger.h"

using namespace encrypto::motion;

void BenchmarkReLU(std::size_t party_id, std::size_t num_elements) {
  communication::TcpSetupHelper helper(party_id, communication::TcpSetupHelper::CreateParties(2));
  auto communication_layer =
      std::make_unique<communication::CommunicationLayer>(party_id, helper.SetupConnections());

  auto configuration = std::make_shared<Configuration>(party_id, 2);
  auto logger = std::make_shared<Logger>(
      party_id, boost::log::trivial::severity_level::error);  // Minimal logging

  auto backend = std::make_unique<Backend>(std::move(communication_layer), configuration, logger);

  // Create random input
  std::vector<std::uint32_t> input_data(num_elements, 42);
  std::vector<std::uint32_t> zero_data(num_elements, 0);

  ShareWrapper share_a =
      backend->template ArithmeticGmwInput<std::uint32_t>(party_id == 0 ? input_data : input_data);
  SecureUnsignedInteger a(share_a);

  ShareWrapper share_zero = backend->template ArithmeticGmwInput<std::uint32_t>(zero_data);
  SecureUnsignedInteger zero(share_zero);

  auto start = std::chrono::high_resolution_clock::now();

  auto is_positive = a > zero;

  auto result_share = is_positive.Mux(a.Get(), zero.Get());

  auto output = result_share.Out();

  backend->Run();

  output.template As<std::vector<std::uint32_t>>();

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> diff = end - start;

  if (party_id == 0) {
    std::cout << "Benchmark_ReLU_N" << num_elements << "_Time: " << diff.count() << " s"
              << std::endl;
  }
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "Usage: " << argv[0] << " <party_id> <N>" << std::endl;
    return 1;
  }
  std::size_t party_id = std::atoi(argv[1]);
  std::size_t N = std::atoi(argv[2]);

  try {
    BenchmarkReLU(party_id, N);
  } catch (std::exception& e) {
    std::cerr << "Exception: " << e.what() << std::endl;
    // return 1;
  }

  return 0;
}
