namespace sigma {

template<class Exponent, class GroupElement>
SigmaPlusProver<Exponent, GroupElement>::SigmaPlusProver(
        const GroupElement& g,
        const std::vector<GroupElement>& h_gens,
        int n,
        int m)
    : g_(g)
    , h_(h_gens)
    , n_(n)
    , m_(m) {
}

template<class Exponent, class GroupElement>
void SigmaPlusProver<Exponent, GroupElement>::proof(
        const std::vector<GroupElement>& commits,
        std::size_t l,
        const Exponent& r,
        SigmaPlusProof<Exponent, GroupElement>& proof_out) {
    std::size_t N = commits.size();
    Exponent rB;
    rB.randomize();

    // Create table sigma of nxm bits.
    std::vector<Exponent> sigma;
    SigmaPrimitives<Exponent, GroupElement>::convert_to_sigma(l, n_, m_, sigma);

    // Values of Ro_k from Figure 5.
    std::vector<Exponent> Pk;
    Pk.resize(m_);
    for (int k = 0; k < m_; ++k) {
        Pk[k].randomize();
    }
    R1ProofGenerator<secp_primitives::Scalar, secp_primitives::GroupElement> r1prover(g_, h_, sigma, rB, n_, m_);
    proof_out.B_ = r1prover.get_B();
    std::vector<Exponent> a;
    r1prover.proof(a, proof_out.r1Proof_, true /*Skip generation of final response*/);

    // Compute coefficients of Polynomials P_I(x), for all I from [0..N].
    std::vector <std::vector<Exponent>> P_i_k;
    P_i_k.resize(N);
    for (std::size_t i = 0; i < N; ++i) {
        std::vector<Exponent>& coefficients = P_i_k[i];
        std::vector<uint64_t> I = SigmaPrimitives<Exponent, GroupElement>::convert_to_nal(i, n_, m_);
        coefficients.push_back(sigma[I[0]]);
        coefficients.push_back(a[I[0]]);
        for (int j = 1; j < m_; ++j) {
            SigmaPrimitives<Exponent, GroupElement>::new_factor(sigma[j * n_ + I[j]], a[j * n_ + I[j]], coefficients);
        }
        std::reverse(coefficients.begin(), coefficients.end());
    }

    //computing G_k`s;
    std::vector <GroupElement> Gk;
    Gk.reserve(m_);
    for (int k = 0; k < m_; ++k) {
        std::vector <Exponent> P_i;
        P_i.reserve(N);
        for (size_t i = 0; i < N; ++i) {
            P_i.emplace_back(P_i_k[i][k]);
        }
        secp_primitives::MultiExponent mult(commits, P_i);
        GroupElement c_k = mult.get_multiple();
        c_k += SigmaPrimitives<Exponent, GroupElement>::commit(g_, Exponent(uint64_t(0)), h_[0], Pk[k]);
        Gk.emplace_back(c_k);
    }
    proof_out.Gk_ = Gk;

    // Compute value of challenge X, then continue R1 proof and sigma final response proof.
    std::vector<GroupElement> group_elements = {
        proof_out.r1Proof_.A_, proof_out.B_, proof_out.r1Proof_.C_, proof_out.r1Proof_.D_};

    group_elements.insert(group_elements.end(), Gk.begin(), Gk.end());
    Exponent x;
    SigmaPrimitives<Exponent, GroupElement>::generate_challenge(group_elements, x);
    r1prover.generate_final_response(a, x, proof_out.r1Proof_);

    //computing z
    Exponent z;
    z = r * x.exponent(uint64_t(m_));
    Exponent sum;
    Exponent x_k(uint64_t(1));
    for (int k = 0; k < m_; ++k) {
        sum += (Pk[k] * x_k);
        x_k *= x;
    }
    z -= sum;
    proof_out.z_ = z;
}

} // namespace sigma
