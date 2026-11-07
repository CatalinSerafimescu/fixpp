# Contract: C++ API changes in 102 (BREAKING, C++ only; C ABI and Python unchanged)

1. `fixpp::dict::ComponentRef` and `GroupRef` change layout (data-model). Code that names members keeps compiling;
   code that aggregate-initializes them positionally, depends on `sizeof == 12`, or stores them in a binary format
   breaks. `component_fields` / `group_fields` signatures are unchanged and now return each definition's own members.
2. A dictionary with more than 65535 components fails to load.
3. 098's `overlay_limit_error` for "the components/groups side table would stop fitting 16 bits" no longer exists.
4. `fixpp::v50sp2` and `fixpp::vlatest`: every MultiCharValue accessor returns `expected_t<std::string_view>`, every
   such builder `Args` member is `std::optional<std::string_view>`. A caller assigning a `char` writes a one-character
   string literal instead. The precompiled builder libraries' C++ ABI changes accordingly.
