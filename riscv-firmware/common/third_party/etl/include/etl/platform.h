#ifndef ETL_PLATFORM_H
#define ETL_PLATFORM_H

#include <stdint.h>
#include <stddef.h>

#define ETL_NO_STL
#define ETL_NO_EXCEPTIONS
#define ETL_CPP20_SUPPORTED 1

namespace etl {
    using size_t = ::size_t;
    using uintptr_t = ::uintptr_t;
}

#endif // ETL_PLATFORM_H
