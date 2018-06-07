#pragma once

#define linb nonstd     // rename linb::any to nonstd::any for consistency reasons
#define ANY_IMPL_ANY_CAST_MOVEABLE

#include "linb-any.inl"

#undef linb
#undef ANY_IMPL_ANY_CAST_MOVEABLE

