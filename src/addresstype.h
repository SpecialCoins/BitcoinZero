#ifndef ADDRESSTYPE_H
#define ADDRESSTYPE_H

enum struct AddressType
{
      unknown = 0
    , payToPubKeyHash = 1
    , payToScriptHash = 2
    , privcoinMint = 3
    , privcoinSpend = 4
    , sigmaMint = 5
    , sigmaSpend = 6
    , privcoinRemint = 7
    , lelantusMint = 8
    , lelantusJMint = 9
    , lelantusJSplit = 10
};

namespace privcoin { namespace utils {

inline bool isPrivcoinMint(std::string const & str){
    return str == "Zeromint" || str == "zeromint";
}

inline bool isPrivcoinSpend(std::string const & str){
    return str == "Zerospend";
}

inline bool isPrivcoin(std::string const & str){
    return str == "Privcoin";
}

inline bool isSigmaMint(std::string const & str){
    return str == "Sigmamint";
}

inline bool isSigmaSpend(std::string const & str){
    return str == "Sigmaspend";
}

inline bool isSigma(std::string const & str){
    return str == "Sigma";
}

inline bool isPrivcoinRemint(std::string const & str){
    return str == "Remint";
}

inline bool isLelantus(std::string const & str){
    return str == "Lelantus";
}

inline bool isLelantusMint(std::string const & str){
    return str == "Lelantusmint";
}

inline bool isLelantusJMint(std::string const & str){
    return str == "Lelantusjmint";
}

inline bool isLelantusJSplit(std::string const & str){
    return str == "Lelantusjsplit";
}

}}
#endif /* ADDRESSTYPE_H */

