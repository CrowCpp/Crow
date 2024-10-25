#include <cstdint>
#include <fuzzer/FuzzedDataProvider.h>

#include "crow.h"

class FuzzException : public std::exception
{
    virtual const char* what() const throw()
    {
        return "Base64 decoding error!";
    }
};

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, const std::size_t size)
{
    FuzzedDataProvider fdp{data, size};

    std::string plaintext = fdp.ConsumeRandomLengthString();
    std::string encoded = crow::utility::base64encode(plaintext, plaintext.size());
    std::string decoded = crow::utility::base64decode(encoded, encoded.size());

    if (plaintext != decoded)
    {
        throw FuzzException();
    }
    return 0;
}