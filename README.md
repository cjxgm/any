# A more usable alternative to `std::any` for C++17

Copyright (C) Giumo Clanjor (哆啦比猫/兰威举), 2020.
Licensed under the MIT License.

This `nonstd::any` is different from `std::any`, in which:

- Allow constructing non-copyable non-movable types in-place via `emplace<T>(args...)`.
- Allow constructing non-copyable types. Copying an any containing such type results in `bad_any_copy` exception.
- Allow constructing non-movable types. These types are constructed via an indirect pointer. Moving an any containing such type results in moving the indirection pointer.
- Moving an any will move the contained type by value with best effort, regardless the use of an indirection pointer. If not possible, the indirection pointer will be moved.
- Moving an any is always `noexcept`. Assumes all types are `noexcept` movable, even if they are not.
- Moving an any leaves the original object in empty state (i.e. always clears the moved-out object).

