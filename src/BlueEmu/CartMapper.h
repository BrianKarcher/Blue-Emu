#pragma once
#include "MemoryMapper.h"

class Cartridge;


class CartMapper : public MemoryMapper
{
public:
	CartMapper(Cartridge& cart);
	~CartMapper() = default;


private:
	Cartridge& _cart;
};