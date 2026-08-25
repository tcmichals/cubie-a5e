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

## 2. Embedded Template Library (ETL) Standard
* **ETL is Strictly 100% Upstream / Immutable:** ETL (`third_party/etl`) is an official Git submodule pointing to `https://github.com/ETLCPP/etl.git`. **NEVER edit ETL internal headers or create custom forks.** Always write client code adhering to official ETL container APIs.
* **AbstractX Submodule:** AbstractX (`third_party/AbstractX`) is our open-source C++20 coroutine repository (`https://github.com/tcmichals/AbstractX.git`). Changes, enhancements, and bug fixes to coroutine primitives are allowed in AbstractX and should be committed/pushed back to upstream `tcmichals/AbstractX`.
* **Prohibit Dynamic Heap Allocation:** All dynamic memory allocation (`malloc`, `free`, global `new`, global `delete`) is strictly prohibited.
* **Mandatory ETL Containers:** Use `etl::vector<T, N>`, `etl::circular_buffer<T, N>`, `etl::array<T, N>`, `etl::span<T>`, `etl::string<N>`, `etl::flat_map<Key, Value, N>` in place of standard heap-allocating STL.

---

## 3. General Embedded C++ Constraints for RISC-V
1. **No C++ Exceptions:** Compile with `-fno-exceptions`. Use `pw::Status`, `std::expected`, or return codes.
2. **No RTTI:** Compile with `-fno-rtti`.
3. **No Thread-Safe Statics:** Compile with `-fno-threadsafe-statics`.
4. **AbstractX Coroutine Allocation:** All coroutines must allocate frames statically inside DTCM (`StaticCoroutinePool`) or elide frames via HALO optimization.
