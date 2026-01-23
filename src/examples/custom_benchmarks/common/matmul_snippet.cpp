
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
