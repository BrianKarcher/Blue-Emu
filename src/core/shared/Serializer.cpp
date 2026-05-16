#include "Serializer.h"
#include <iostream>
#include <cstdint>
#include <vector>

#define VERSION 1

void Serializer::StartSerialization(std::ostream& os) {
	this->os = &os;
    uint32_t version = VERSION;
    Write(version);
}

void Serializer::StartDeserialization(std::istream& is) {
	this->is = &is;
    uint32_t version;
    Read(version);
    if (version != VERSION) {
        throw std::runtime_error("Unsupported serialization version");
    }
}