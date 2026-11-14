#include "../src/machine/mame_lite.h"
#include <cassert>
#include <cstdint>
#include <cstdio>

int main()
{
	page_allocator<uint32_t> allocator;
	for (size_t count : { size_t(0), size_t(1), size_t(4096), size_t(4097) }) {
		uint32_t *p = allocator.allocate(count);
		assert(p != nullptr);
		assert(reinterpret_cast<uintptr_t>(p) % page_allocator<uint32_t>::PAGE == 0);
		assert(page_allocator<uint32_t>::bytes(count) >= count * sizeof(uint32_t));
		if (count) p[count - 1] = 123;
		allocator.deallocate(p, count);
	}
	bitmap_rgb32 bitmap;
	bitmap.allocate(512, 384);
	assert(reinterpret_cast<uintptr_t>(bitmap.data()) % page_allocator<uint32_t>::PAGE == 0);
	bitmap.pix(383, 511) = 456;
	assert(bitmap.pix(383, 511) == 456);
	std::puts("Page-aligned bitmap allocation passed");
}
