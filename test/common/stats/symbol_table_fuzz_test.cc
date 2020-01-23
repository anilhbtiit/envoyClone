#include <algorithm>
#include <iostream>

#include "common/common/assert.h"
#include "common/common/base64.h"
#include "common/stats/symbol_table_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"

namespace Envoy {
namespace Stats {
namespace Fuzz {

// Fuzzer for symbol tables.
DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  FuzzedDataProvider provider(buf, len);
  FakeSymbolTableImpl fake_symbol_table;
  SymbolTableImpl symbol_table;
  StatNamePool pool(symbol_table);
  StatNamePool fake_pool(fake_symbol_table);
  StatNameDynamicPool dynamic_pool(symbol_table);
  StatNameDynamicPool fake_dynamic_pool(fake_symbol_table);

  while (provider.remaining_bytes() != 0) {
    std::string next_data = provider.ConsumeRandomLengthString(provider.remaining_bytes());
    StatName stat_name = pool.add(next_data);
    StatName fake_stat_name = fake_pool.add(next_data);
    StatName dynamic_stat_name = dynamic_pool.add(next_data);
    StatName fake_dynamic_stat_name = fake_dynamic_pool.add(next_data);

    // We can add stat-names with trailing dots, but note that they will be
    // trimmed by the Symbol Table implementation, so we must trim the input
    // string before comparing.
    absl::string_view trimmed_fuzz_data = StringUtil::removeTrailingCharacters(next_data, '.');
    FUZZ_ASSERT(trimmed_fuzz_data == symbol_table.toString(stat_name));
    FUZZ_ASSERT(trimmed_fuzz_data == fake_symbol_table.toString(fake_stat_name));
    FUZZ_ASSERT(trimmed_fuzz_data == symbol_table.toString(dynamic_stat_name));
    FUZZ_ASSERT(trimmed_fuzz_data == fake_symbol_table.toString(fake_dynamic_stat_name));

    // Test all combinations of joins within each symbol table.
    if (!trimmed_fuzz_data.empty()) {
      std::string joined = absl::StrCat(trimmed_fuzz_data, ".", trimmed_fuzz_data);
      auto join = [joined](SymbolTable& table, StatName name1, StatName name2) -> bool {
        bool ok = true;
        SymbolTable::StoragePtr storage = table.join({name1, name2});
        StatName stat_name(storage.get());
        std::string name1_name2 = table.toString(stat_name);
        if (joined.size() != name1_name2.size()) {
          std::cerr << "lengths don't match: " << joined.size() << " != " << name1_name2.size()
                    << std::endl;
          ok = false;
        } else {
          for (uint32_t i = 0; i < joined.size(); ++i) {
            if (joined[i] != name1_name2[i]) {
              std::cerr << "char [" << i << "] mismatch: " << joined[i] << "("
                        << static_cast<uint32_t>(joined[i]) << ") != " << name1_name2[i] << "("
                        << static_cast<uint32_t>(name1_name2[i]) << ")" << std::endl;
              ok = false;
            }
          }
        }
        return ok;
      };
      FUZZ_ASSERT(join(symbol_table, stat_name, stat_name));
      FUZZ_ASSERT(join(symbol_table, stat_name, dynamic_stat_name));
      FUZZ_ASSERT(join(symbol_table, dynamic_stat_name, dynamic_stat_name));
      FUZZ_ASSERT(join(symbol_table, dynamic_stat_name, stat_name));
      FUZZ_ASSERT(join(fake_symbol_table, fake_stat_name, fake_stat_name));
      FUZZ_ASSERT(join(fake_symbol_table, fake_stat_name, fake_dynamic_stat_name));
      FUZZ_ASSERT(join(fake_symbol_table, fake_dynamic_stat_name, fake_dynamic_stat_name));
      FUZZ_ASSERT(join(fake_symbol_table, fake_dynamic_stat_name, fake_stat_name));
    }

    // Also encode the string directly, without symbolizing it.
    TestUtil::serializeDeserializeString(next_data);

    // Grab the first few bytes from next_data to synthesize together a random uint64_t.
    if (next_data.size() > 1) {
      uint32_t num_bytes = (next_data[0] % 8) + 1; // random number between 1 and 8 inclusive.
      num_bytes = std::min(static_cast<uint32_t>(next_data.size() - 1),
                           num_bytes); // restrict number up to the size of next_data
      uint64_t number = 0;
      for (uint32_t i = 0; i < num_bytes; ++i) {
        number = 256 * number + next_data[i + 1];
      }
      TestUtil::serializeDeserializeNumber(number);
    }
  }
}

} // namespace Fuzz
} // namespace Stats
} // namespace Envoy
