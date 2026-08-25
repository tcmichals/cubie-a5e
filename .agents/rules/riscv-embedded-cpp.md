# RISC-V Embedded C++ & ETL Container Rules

This rule strictly governs all C++ development targeting the **XuanTie E907 RISC-V co-processor** (`riscv-firmware/`):

---

## 1. Strict Ban on Heap-Allocating Standard Template Library (STL)

* **FORBIDDEN:** Standard Library heap-allocating containers and types are strictly forbidden in RISC-V firmware:
  * ❌ `std::vector`
  * ❌ `std::string` / `std::stringstream`
  * ❌ `std::queue` / `std::deque` / `std::stack` / `std::list`
  * ❌ `std::map` / `std::unordered_map` / `std::set`
  * ❌ `std::function` (with heap allocation)
  * ❌ `new` / `delete` / `malloc` / `free`
* **Rationale:** The auxiliary core operates with **64 KB DTCM** and **64 KB ITCM**. Heap fragmentation, non-deterministic allocation latency, and out-of-memory panics are unacceptable in hard real-time flight loops.

---

## 2. Mandatory Use of Embedded Template Library (ETL)

* **Repository:** `https://github.com/ETLCPP/etl.git` (`third_party/etl`)
* **Submodule:** `git submodule add https://github.com/ETLCPP/etl.git third_party/etl`
* Whenever container data structures, circular queues, fixed-capacity vectors, string formatters, or embedded algorithms are needed, **ETL (`etl::*`) MUST BE USED**.
* **Allowed ETL Structures:**
  * `etl::vector<T, N>` (Fixed-capacity vector with zero heap allocation)
  * `etl::queue<T, N>` / `etl::circular_buffer<T, N>`
  * `etl::string<N>` / `etl::string_view`
  * `etl::array<T, N>`
  * `etl::span<T>`
  * `etl::flat_map<Key, Value, N>` / `etl::flat_set<T, N>`
  * `etl::pool<T, N>` / `etl::memory_pool<T, N>`

---

## 3. General Embedded C++ Constraints for RISC-V
1. **No C++ Exceptions:** Compile with `-fno-exceptions`. Use `pw::Status`, `std::expected`, or return codes.
2. **No RTTI:** Compile with `-fno-rtti`.
3. **No Thread-Safe Statics:** Compile with `-fno-threadsafe-statics`.
4. **AbstractX Coroutine Allocation:** All coroutines must allocate frames statically inside DTCM (`StaticCoroutinePool`) or elide frames via HALO optimization.
