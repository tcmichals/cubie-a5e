#ifndef ETL_PLATFORM_H
#define ETL_PLATFORM_H

#include <stdint.h>
#include <stddef.h>

#define ETL_NO_STL
#ifndef ETL_CPP20_SUPPORTED
#define ETL_CPP20_SUPPORTED 1
#endif

namespace etl {
    using size_t = ::size_t;
    using uintptr_t = ::uintptr_t;
}

#endif // ETL_PLATFORM_H
