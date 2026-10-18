#include "Utilities/File.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>

#ifndef _WIN32

TEST(file, ReadsRandomCharacterDevice)
{
	// On BSD (including PS5), character devices are also candidates for raw optical devices.
	// A failed media-size ioctl must not prevent the syscall RNG from reading /dev/urandom.
	fs::file random{"/dev/urandom"};
	ASSERT_TRUE(random);

	std::array<u8, 256> data{};
	EXPECT_EQ(random.read(data.data(), data.size()), data.size());
}

TEST(file, ReadsZeroCharacterDevice)
{
	fs::file zero{"/dev/zero"};
	ASSERT_TRUE(zero);

	std::array<u8, 256> data;
	data.fill(0xff);
	EXPECT_EQ(zero.read(data.data(), data.size()), data.size());
	EXPECT_TRUE(std::all_of(data.begin(), data.end(), [](u8 value) { return value == 0; }));
}

TEST(file, OpensEmptyCharacterDevice)
{
	fs::file null{"/dev/null"};
	ASSERT_TRUE(null);

	u8 data = 0xff;
	EXPECT_EQ(null.read(&data, 1), 0);
	EXPECT_EQ(data, 0xff);
}

#endif
