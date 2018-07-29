#pragma once

#define ANY_IMPL_ANY_CAST_MOVEABLE
#include "linb-any.inl"

// rename linb::any to nonstd::any for consistency reasons
namespace nonstd
{
    using linb::any;
    using linb::any_cast;
}

#undef ANY_IMPL_ANY_CAST_MOVEABLE

