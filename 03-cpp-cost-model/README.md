# C++17/20 as a systems language

The language is a cost model, not a catalogue of features. On a hot path, a construct is allowed if it compiles to the loads, stores, and branches you would have written in C — plus the ones you *wanted*. Everything else belongs in setup, tooling, or the logging thread.

C++17/20 still matter: `std::span`, `string_view`, `constexpr`, `if constexpr`, `std::byte`, structured bindings, and concepts as *constraints*. They do not license `std::function`, `shared_ptr`, or `unordered_map` in the poll loop.

## Cost table (hot path)

| Construct | Typical cost | Use |
| --- | --- | --- |
| Trivial copy of a 16-byte descriptor | registers / one line | yes |
| `virtual` call | indirect branch + I-cache | no |
| `std::function` | alloc or out-of-line call | no |
| `new` / `vector` growth | lock, syscall, jitter | no |
| `std::string` (SSO miss) | heap | no |
| `unordered_map` lookup | hash + miss + branch | almost never |
| Exception unwind | tables, pessimised codegen | compile `-fno-exceptions` |
| `shared_ptr` copy | atomic refcount (true sharing) | no |

Setup may allocate, throw, and use the STL freely. `main` and config parsers are not the book.

## Examples

```bash
./build/03-cpp-cost-model/virtual_dispatch --iters 50000000
./build/03-cpp-cost-model/alloc_vs_pool --iters 5000000 --pool-size 4096
./build/03-cpp-cost-model/hashmap_vs_array --lookups 8000000 --ids 256
```

### 1. Virtual vs direct

Two derived handlers, chosen per item so the compiler cannot devirtualize to one target. Compare a virtual call, a function pointer, and a `switch` on a type tag. The tag + switch is what feed handlers actually look like.

### 2. `new` vs a pool

The pool is `hft::Pool<T>`: a slab allocated once and a free stack of indices. Acquire/release is a few loads. `new`/`delete` is an allocator, a lock, and eventually a syscall. Same object, different jitter.

### 3. Hash map vs dense id

If instrument ids are a small dense integer (or you map them to one at session start), an array indexed by id is one load. `unordered_map` is a hash, a branch, and a likely miss. Production systems spend startup turning symbology into integers for this reason.

## Questions to close this chapter

1. Why is `shared_ptr` on a callback worse than `virtual`? (Atomic refcount = true sharing on every copy.)
2. When is `std::vector` acceptable on a hot path? (Pre-sized, no `push_back` that can grow, trivial `T`.)
3. What does `std::string_view` not do? (It does not own. The backing storage must already be stable — a pool buffer, not a temporary `string`.)
4. Name three C++20 features that belong in this codebase and three that do not.
