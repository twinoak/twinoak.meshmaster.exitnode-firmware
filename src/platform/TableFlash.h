// The board's partition-table sector, for src/platform/PartitionTable.h's three functions.
//
// The implementation is TableFlash.cpp, and it is **the only file of src/ and lib/ that erases or
// writes raw flash**: test/test_flash_writes walks the tree for the ESP-IDF calls that do and
// fails when a second file holds one, as test/test_tls_policy does for the ways of not verifying
// a certificate - which is why this header names none of them. The rail is in README.md's list.

#pragma once

#include "PartitionTable.h"

namespace mm {

// The one sector, at the one address. Every call is bounded to it inside the implementation.
TableFlash& partitionTableFlash();

}  // namespace mm
