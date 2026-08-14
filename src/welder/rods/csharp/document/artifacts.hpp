#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/** @file
    The **two artifacts** the C# rod emits, and the placeholder machinery that
    lets them be written out of order.

    The C# rod is the one backend that emits two coordinated files from a single
    driver pass: a native `extern "C"` **shim** (`shim.cpp`, compiled into a
    shared library with reflection enabled, against the same welded header) and a
    managed **C# wrapper** (`Bindings.cs`, `[LibraryImport]` P/Invoke declarations
    + idiomatic classes). So @ref welder::rods::csharp::document holds several
    buffers rather than luacats' one, and @ref welder::rods::csharp::document::render_shim
    / @ref welder::rods::csharp::document::render_cs assemble the two files.
    Every emission primitive appends the paired native thunk and managed
    declaration together, keyed by the same symbol, so the two sides cannot
    desync; a duplicate symbol is caught at render time.

    **Placeholders.** A type reference is emitted as a sentinel-delimited raw C++
    spelling rather than a final name, and resolved once at render:

    | Flavor | Resolves to |
    |---|---|
    | `\x01raw\x02` | the final C# name (`Outer.Inner`) |
    | `\x03raw\x04` | the same, identifier-sanitized (dots → underscores) — handle fields |
    | `\x05raw\x06` | the C++ spelling the SHIM must use (shim text only) |

    That is what makes declaration order irrelevant, lets a hook without a name
    style still spell styled names, and lets the container generators — which see
    only `std::vector<E>` and never `E`'s welding alias — name their element
    correctly.

    The managed scaffolding rendered once per `Bindings.cs`:
    - `WelderError` — the blittable mirror of the shim's trailing error out-param;
    - `WelderInterop.ThrowIfError` — reads the slot, frees the message, and
      throws: codes 2–5 map to the matching BCL exception types
      (`OutOfMemoryException` / `ArgumentException` /
      `ArgumentOutOfRangeException` / `ArithmeticException`), everything else
      to @c WelderNativeException;
    - one `SafeHandle` subclass per welded class (its `ReleaseHandle` calls the
      class's destroy thunk), giving every wrapper finalizer-safe ownership and
      premature-collection safety on every P/Invoke that passes it.
*/

namespace welder::inline v0::rods::csharp {

/** Knobs the generator supplies once for the whole module. */
struct options {
    std::string cs_namespace{}; /**< The root C# namespace (the module name). */
    std::string library{};      /**< The P/Invoke library (the shared-lib base name). */
    std::string shim_include{}; /**< The header the shim `#include`s to see the types. */
    /** How many translation units the shim is split into (default 1 — the
        single `shim.cpp`). A large welded surface makes one reflection-heavy
        TU a compile-time and memory problem; sharding assigns each top-level
        class (its thunks AND its director subclass, which must share a TU) to
        one of @ref shards files round-robin, so they compile in parallel.
        The managed side is unaffected — P/Invoke binds symbols, not TUs. */
    std::size_t shards{1};
    /** How many files `Bindings.cs` is split into (default 1 — the single
        file). Unlike @ref shards this is not a compile-time measure: Roslyn is
        indifferent to file count (measured — one 11 MB file and 83 small ones
        compile in the same time). It exists for the TOOLING around the
        artifact: editors that will not open a multi-megabyte source, reviewable
        diffs, and per-namespace goldens. A C# assembly has no translation-unit
        boundary, so the split is unconditionally safe — names resolve
        assembly-wide, `NativeMethods` is `partial`, and a namespace may be
        reopened by any file. Parts land in `<stem>.<i>.cs` siblings. */
    std::size_t cs_files{1};
};

/** One C# namespace's slice of the module: a nested C++ namespace maps to a
    REAL nested C# namespace (`geo::util` → `namespace geo.Util`), holding its
    welded types plus one `Global` static class for its free functions and
    variables (C# has no namespace-scope functions). The root namespace is the
    section with an empty @ref ns. */
struct ns_section {
    std::string ns{};      /**< Dotted path below the root (`""` = the root). */
    std::string types{};   /**< Wrapper class + enum declarations. */
    std::string statics{}; /**< The namespace's `Global` static-class body. */
    /** Offsets into @ref types where one declaration ends and the next begins,
        recorded by the class/enum writers as they flush. Splitting
        (@ref options::cs_files) may only cut here — a boundary the emitters
        KNOW, so no part can ever end mid-declaration. Monotonic: every writer
        appends. */
    std::vector<std::size_t> breaks{};
};

/** The growing pair of documents shared by every writer handle: the native shim and
    the managed wrapper. Class/enum text lands in a per-namespace @ref ns_section::types;
    free functions and namespace variables land in that section's @ref ns_section::statics;
    every P/Invoke declaration lands in @ref pinvoke (one `NativeMethods` class);
    every native thunk lands in @ref shim. */
struct document {
    options opts{};
    std::string module_doc{}; /**< The root namespace's doc (a file-header comment). */
    /** `extern "C"` thunk bodies, one buffer per shim TU (see
        @ref options::shards; a single part unless sharding is on). */
    std::vector<std::string> shim_parts{std::vector<std::string>(1)};
    /** Director subclass definitions (before `extern "C"`), per shim TU —
        a class's director must live beside its thunks (its `construct_as` /
        `dir_init` thunks name the subclass type). */
    std::vector<std::string> director_parts{std::vector<std::string>(1)};
    std::size_t shard_cursor{0}; /**< The shard the NEXT emission lands in. */

    /** Size the shim to @a n TUs (from @ref options::shards; call once,
        before the driver runs — parts never shrink).
        @param n the TU count (clamped to ≥ 1). */
    void set_shard_count(std::size_t n) {
        shim_parts.resize(n == 0 ? 1 : n);
        director_parts.resize(shim_parts.size());
    }
    /** The shim TU count. @return the number of parts. */
    std::size_t shard_count() const { return shim_parts.size(); }
    /** The shim buffer current emissions append to. @return the active part. */
    std::string& current_shim() { return shim_parts[shard_cursor]; }
    /** The active part's director buffer. @return the active part. */
    std::string& current_directors() { return director_parts[shard_cursor]; }
    /** Rotate to the next shard (round-robin) — the driver calls this per
        TOP-LEVEL class / free-function group, the units whose emissions are
        self-contained; everything they trigger (nested types, container
        wrappers, directors) stays in the class's shard. A no-op at count 1. */
    void advance_shard() { shard_cursor = (shard_cursor + 1) % shim_parts.size(); }

    std::string pinvoke{}; /**< `[LibraryImport]` declarations (inside NativeMethods). */
    /** Offsets into @ref pinvoke at symbol boundaries (recorded when a
        @ref bound_symbol finishes), so a split part never cuts a declaration in
        half. `NativeMethods` is `partial`, so each part reopens it. */
    std::vector<std::size_t> pinvoke_breaks{};
    std::vector<ns_section> sections{}; /**< Per-namespace types + Global bodies. */
    std::string containers{};   /**< Generated container-wrapper classes (root ns). */
    std::vector<std::string> container_keys{}; /**< Dedup (one wrapper per type). */

    /** First-emission check for a generated container wrapper.
        @param key the container's rename key (its display string).
        @return true the first time @a key is seen, false afterwards. */
    bool claim_container(std::string key) {
        for (const auto& k : container_keys)
            if (k == key)
                return false;
        container_keys.push_back(std::move(key));
        return true;
    }
    std::vector<std::string> symbols{};  /**< Every emitted C symbol (collision check). */
    /** raw `::qualified` C++ name → final C# name, filled by make_class/make_enum.
        Type REFERENCES are emitted as `\x01raw\x02` placeholders and reconciled
        at render (the luacats record_type_name idiom) — declaration order never
        matters, and hooks without a Style (add_operator) still spell styled
        names correctly. */
    std::vector<std::pair<std::string, std::string>> type_names{};

    /** Register (or update — nested factories refine the flat name to the
        dotted path) the final C# name for raw C++ type spelling @a raw.
        @param raw    the placeholder key.
        @param styled the final C# name. */
    void record_type_name(std::string raw, std::string styled) {
        for (auto& [r, cs] : type_names)
            if (r == raw) {
                cs = std::move(styled);
                return;
            }
        type_names.emplace_back(std::move(raw), std::move(styled));
    }

    /** raw type key → the `::qualified` C++ spelling the SHIM must use to name
        that type, filled by make_class/make_enum from the welding declaration.

        The C#-name registry above cannot serve this — the shim needs C++, not
        C#. Nor can `qualified_cpp_name(T)`, for a class-template
        SPECIALIZATION: that walk collects identifiers, a specialization has
        none, so it silently yields the enclosing NAMESPACE and the shim splices
        `^^ns::detail` (not a type). The spellable name such a type does have is
        its namespace-scope welding ALIAS — precisely what make_class receives as
        `Decl`. Recording it here is what lets the container generators, which
        see only the container type and never the alias, name their element.
        @see welder::rods::csharp::anchor_ref */
    std::vector<std::pair<std::string, std::string>> type_anchors{};

    /** Register the shim's C++ spelling for raw type key @a raw.
        @param raw the placeholder key.
        @param cpp the `::`-qualified C++ spelling. */
    void record_type_anchor(std::string raw, std::string cpp) {
        for (auto& [r, a] : type_anchors)
            if (r == raw) {
                a = std::move(cpp);
                return;
            }
        type_anchors.emplace_back(std::move(raw), std::move(cpp));
    }

    /** Resolve every `\x05raw\x06` anchor placeholder in @a text against
        @ref type_anchors. Applied to the SHIM only: these are C++ spellings,
        which have no business in the emitted C#. An unmatched placeholder keeps
        its raw spelling, so the shim build fails loudly rather than silently
        splicing the wrong entity.
        @param text the shim text.
        @return @a text with every anchor resolved. */
    std::string apply_type_anchors(std::string text) const {
        std::size_t b1{0};
        while ((b1 = text.find('\x05', b1)) != std::string::npos) {
            const std::size_t b2{text.find('\x06', b1 + 1)};
            if (b2 == std::string::npos)
                break;
            const std::string raw{text.substr(b1 + 1, b2 - b1 - 1)};
            std::string sub{raw};
            for (const auto& [r, a] : type_anchors)
                if (r == raw) {
                    sub = a;
                    break;
                }
            text.replace(b1, b2 - b1 + 1, sub);
            b1 += sub.size(); // anchors never nest
        }
        return text;
    }

    /** Resolve every placeholder in @a text against @ref type_names — the
        `\x01raw\x02` flavor substitutes the registered C# name verbatim
        (`Outer.Inner` for a nested type), the `\x03raw\x04` flavor its
        IDENTIFIER-SAFE form (dots → underscores — handle-field names). An
        unmatched one keeps the raw name — visibly wrong C#, which cannot
        happen for gate-admitted references.
        @param text the managed text.
        @return @a text with every reference resolved. */
    std::string apply_type_renames(std::string text) const {
        for (const char open : {'\x01', '\x03'}) {
            const char close{open == '\x01' ? '\x02' : '\x04'};
            std::size_t b1{0};
            while ((b1 = text.find(open, b1)) != std::string::npos) {
                const std::size_t b2{text.find(close, b1 + 1)};
                if (b2 == std::string::npos)
                    break;
                const std::string raw{text.substr(b1 + 1, b2 - b1 - 1)};
                std::string sub{raw};
                for (const auto& [r, cs] : type_names)
                    if (r == raw) {
                        sub = cs;
                        break;
                    }
                if (open == '\x03')
                    for (char& c : sub)
                        if (c == '.')
                            c = '_';
                text.replace(b1, b2 - b1 + 1, sub);
                // Do NOT skip past the substitution: a container's final name
                // itself contains its element's placeholder (the rescan
                // resolves it; keys never contain themselves, so this
                // terminates).
            }
        }
        return text;
    }

    /** Record an emitted C symbol; a duplicate aborts the generator with a
        designed message (two members whose underscore paths collide — e.g.
        `a_b::c` vs `a::b_c` — would silently link to one thunk otherwise).
        @param sym the C symbol. */
    void record_symbol(std::string sym) {
        for (const auto& s : symbols)
            if (s == sym) {
                current_shim() += "#error welder: duplicate C symbol '" + sym +
                        "' (colliding underscore paths); rename one entity with "
                        "weld_as\n";
                return;
            }
        symbols.push_back(std::move(sym));
    }

    /** The section of C# namespace @a ns (dotted path below the root; `""` =
        the root namespace), created on first use.
        @param ns the dotted namespace path.
        @return the (stable) section. */
    ns_section& section(std::string_view ns) {
        for (auto& s : sections)
            if (s.ns == ns)
                return s;
        sections.push_back({std::string{ns}, {}, {}});
        return sections.back();
    }

    /** The finished text of shim TU @a part (part 0 when the shim is a
        single `shim.cpp`). Every part carries the same prologue (includes +
        the `wcs` alias) and its own directors + thunks; the two hand-written
        exports (`welder_free` / `welder_dup_utf8`) are emitted exactly once,
        in part 0.
        @param part the shard index (`< shard_count()`).
        @return the native artifact, with every anchor placeholder resolved. */
    std::string render_shim(std::size_t part = 0) const {
        std::string out{
            "// <auto-generated> welder C#/.NET native shim. Do not edit -\n"
            "// regenerate via the welder_csharp_generate_bindings() target.\n"
            "// Compiled with reflection against the same welded header the\n"
            "// generator saw: thunk bodies SPLICE the exact member reflection\n"
            "// (re-derived by the shared lookup layer), so no C++ type is\n"
            "// respelled here - only the C-ABI wire types are text.\n"
            "#include <cstdint>\n"
            "#include <welder/vocabulary.hpp>\n"};
        if (!opts.shim_include.empty())
            out += "#include \"" + opts.shim_include + "\"\n";
        out += "#include <welder/rods/csharp/shim_support.hpp>\n"
               "\n"
               "namespace wcs = ::welder::rods::csharp;\n"
               "\n";
        out += director_parts[part];
        out += "extern \"C\" {\n\n";
        out += shim_parts[part];
        if (part == 0)
            out += "void welder_free(void* p) { std::free(p); }\n\n"
                   "const char* welder_dup_utf8(const char* s) { return "
                   "wcs::shim::dup(s ? s : \"\"); }\n\n";
        out += "} // extern \"C\"\n";
        return apply_type_anchors(std::move(out));
    }

    /** The finished `Bindings.cs` text.
        @return the managed artifact, with every reference placeholder resolved. */
    std::string render_cs() const { return render_cs_parts(1).front(); }

    /** The managed artifact as @a n files (@ref options::cs_files).

        Every part is a complete compilation unit: the same header, `using`
        directives and root `namespace` block, then its slice of the surface.
        The slicing is size-balanced but only ever cuts at a boundary the
        emitters recorded (@ref ns_section::breaks, @ref pinvoke_breaks), and
        each part reopens whatever scope its slice needs — a nested namespace,
        or the `partial` `NativeMethods`. Part 0 additionally carries the
        one-per-assembly scaffolding (the error contract, the wire structs,
        `WelderInterop`, and `NativeMethods`' `Lib` constant plus the two
        always-present thunk declarations).

        `n == 1` reproduces @ref render_cs byte for byte — the single-file form
        is the same code path, not a parallel one.
        @param n the file count (clamped to ≥ 1).
        @return the parts, in order. */
    std::vector<std::string> render_cs_parts(std::size_t n) const {
        if (n == 0)
            n = 1;
        // The items a part can be built from, in emission order. Anything
        // pinned to part 0 (the scaffolding) is not an item — it is preamble.
        struct item {
            enum class kind { types, statics, containers, pinvoke } what;
            std::string ns;   /**< The namespace to reopen (empty = root). */
            std::string text; /**< The slice's text. */
        };
        std::vector<item> items{};
        const auto slice_at = [](const std::string& text,
                                 const std::vector<std::size_t>& breaks,
                                 auto emit) {
            std::size_t prev{0};
            for (const std::size_t b : breaks) {
                if (b > prev && b <= text.size())
                    emit(text.substr(prev, b - prev));
                prev = b;
            }
            if (prev < text.size())
                emit(text.substr(prev));
        };
        // Emission order mirrors the single-file layout exactly: the P/Invoke
        // class first (its prologue/epilogue ride the stream as chunks, so the
        // block reads identically), then the root's types, the shared container
        // wrappers, the root `Global`, and finally each nested namespace.
        items.push_back({item::kind::pinvoke, "", _cs_pinvoke_prologue()});
        slice_at(pinvoke, pinvoke_breaks, [&](std::string t) {
            items.push_back({item::kind::pinvoke, "", std::move(t)});
        });
        items.push_back({item::kind::pinvoke, "", _cs_pinvoke_epilogue()});
        for (const auto& s : sections)
            if (s.ns.empty())
                slice_at(s.types, s.breaks, [&](std::string t) {
                    items.push_back({item::kind::types, s.ns, std::move(t)});
                });
        if (!containers.empty())
            items.push_back({item::kind::containers, "", containers});
        for (const auto& s : sections)
            if (s.ns.empty() && !s.statics.empty())
                items.push_back({item::kind::statics, s.ns, s.statics});
        for (const auto& s : sections) {
            if (s.ns.empty())
                continue;
            slice_at(s.types, s.breaks, [&](std::string t) {
                items.push_back({item::kind::types, s.ns, std::move(t)});
            });
            if (!s.statics.empty())
                items.push_back({item::kind::statics, s.ns, s.statics});
        }

        // Size-balanced assignment: walk the items once, starting a new part
        // whenever the current one passes its share. Order is preserved, so a
        // namespace's declarations stay contiguous and parts stay diffable.
        std::size_t total{0};
        for (const auto& it : items)
            total += it.text.size();
        const std::size_t target{total / n + 1};
        std::vector<std::vector<const item*>> bins(n);
        std::size_t bin{0}, filled{0};
        for (const auto& it : items) {
            if (bin + 1 < n && filled > target) {
                ++bin;
                filled = 0;
            }
            bins[bin].push_back(&it);
            filled += it.text.size();
        }

        std::vector<std::string> parts{};
        for (std::size_t p{0}; p < n; ++p) {
            std::string out{_cs_header()};
            out += "namespace " + opts.cs_namespace + "\n{\n";
            if (p == 0)
                out += _cs_scaffolding();
            // Walk this bin, opening/closing a nested namespace or the partial
            // NativeMethods as the run of items requires.
            std::string open_ns{};
            bool in_native{false};
            const auto close_scope = [&] {
                if (in_native) {
                    out += "    }\n\n";
                    in_native = false;
                }
                if (!open_ns.empty()) {
                    out += "    }\n\n";
                    open_ns.clear();
                }
            };
            for (const item* it : bins[p]) {
                if (it->what == item::kind::pinvoke) {
                    if (!in_native) {
                        close_scope();
                        out += "    internal static partial class NativeMethods\n"
                               "    {\n";
                        in_native = true;
                    }
                    out += it->text;
                    continue;
                }
                if (in_native || open_ns != it->ns) {
                    close_scope();
                    if (!it->ns.empty()) {
                        out += "    namespace " + it->ns + "\n    {\n";
                        open_ns = it->ns;
                    }
                }
                if (it->what == item::kind::statics) {
                    out += "    public static class Global\n    {\n";
                    out += it->text;
                    out += "    }\n\n";
                } else {
                    out += it->text;
                }
            }
            close_scope();
            out += "}\n";
            parts.push_back(apply_type_renames(std::move(out)));
        }
        return parts;
    }

  private:
    /** The file header every part opens with (comment banner, `#nullable`,
        `using`s). @return the shared preamble text. */
    std::string _cs_header() const {
        std::string out{
            "// <auto-generated> welder C#/.NET bindings. Do not edit -\n"
            "// regenerate via the welder_csharp_generate_bindings() target.\n"};
        if (!module_doc.empty()) {
            out += "//\n";
            for (std::size_t b{0}; b < module_doc.size();) {
                std::size_t e{module_doc.find('\n', b)};
                if (e == std::string::npos)
                    e = module_doc.size();
                out += "// " + module_doc.substr(b, e - b) + "\n";
                b = e + 1;
            }
        }
        out += "#nullable enable\n"
               "using System;\n"
               "using System.Runtime.CompilerServices;\n"
               "using System.Runtime.InteropServices;\n\n";
        return out;
    }

    /** The one-per-assembly scaffolding part 0 carries: the error contract, the
        by-value wires, `WelderInterop`, and `NativeMethods`' constant plus the
        two always-present declarations.
        @return the scaffolding text, at namespace depth. */
    std::string _cs_scaffolding() const {
        // The error contract: the blittable slot + the check-and-throw helper.
        std::string out{
            "    /// <summary>The native error slot every welder thunk fills "
            "(code 0 = success).</summary>\n"
            "    [StructLayout(LayoutKind.Sequential)]\n"
            "    internal struct WelderError\n    {\n"
            "        public int Code;\n"
            "        public IntPtr Message;\n"
            "    }\n\n"
            "    /// <summary>A C++ exception that crossed the native "
            "boundary.</summary>\n"
            "    public class WelderNativeException : Exception\n    {\n"
            "        public int NativeCode { get; }\n"
            "        public WelderNativeException(int code, string message) : "
            "base(message)\n"
            "        {\n            NativeCode = code;\n        }\n"
            "    }\n\n"
            "    /// <summary>The by-value wire of an optional with a leaf "
            "payload.</summary>\n"
            "    [StructLayout(LayoutKind.Sequential)]\n"
            "    internal struct WelderOptWire\n    {\n"
            "        public byte Has;\n"
            "        public long I;\n"
            "        public double F;\n"
            "        public IntPtr S;\n"
            "        public IntPtr P;\n"
            "    }\n\n"
            "    /// <summary>The by-value wire of a scalar/enum sequence "
            "(buffer + length).</summary>\n"
            "    [StructLayout(LayoutKind.Sequential)]\n"
            "    internal struct WelderSeqWire\n    {\n"
            "        public IntPtr Data;\n"
            "        public long Len;\n"
            "    }\n\n"
            "    /// <summary>The by-value wire of a shared_ptr return: the "
            "object plus the boxed\n"
            "    /// shared_ptr copy pinning it.</summary>\n"
            "    [StructLayout(LayoutKind.Sequential)]\n"
            "    internal struct WelderSpWire\n    {\n"
            "        public IntPtr Obj;\n"
            "        public IntPtr Box;\n"
            "    }\n\n"
            "    internal static class WelderInterop\n    {\n"
            "        internal static void ThrowIfError(in WelderError err)\n"
            "        {\n"
            "            if (err.Code == 0) return;\n"
            "            string msg = \"\";\n"
            "            if (err.Message != IntPtr.Zero)\n"
            "            {\n"
            "                msg = Marshal.PtrToStringUTF8(err.Message) ?? "
            "\"\";\n"
            "                NativeMethods.welder_free(err.Message);\n"
            "            }\n"
            "            throw err.Code switch\n"
            "            {\n"
            "                2 => new OutOfMemoryException(msg),\n"
            "                3 => new ArgumentException(msg),\n"
            "                4 => new ArgumentOutOfRangeException(null, msg),\n"
            "                5 => new ArithmeticException(msg),\n"
            "                _ => (Exception)new WelderNativeException(err.Code, "
            "msg),\n"
            "            };\n"
            "        }\n\n"
            // The string-sequence wire: unlike a scalar array there is no
            // blittable buffer to pin, so each element crosses as its own UTF-8
            // buffer inside a pointer array. Outbound the native side owns both
            // (welder_free); inbound these three helpers own the staging.
            "        /// <summary>Stage a string[] as an unmanaged array of "
            "UTF-8 buffers.</summary>\n"
            "        internal static WelderSeqWire ToUtf8Seq(string[] a)\n"
            "        {\n"
            "            var _w = new WelderSeqWire { Data = IntPtr.Zero, Len = "
            "a is null ? 0 : a.Length };\n"
            "            if (a is null || a.Length == 0) return _w;\n"
            "            _w.Data = Marshal.AllocHGlobal(IntPtr.Size * "
            "a.Length);\n"
            "            for (int i = 0; i < a.Length; i++)\n"
            "                Marshal.WriteIntPtr(_w.Data, i * IntPtr.Size,\n"
            "                    NativeMethods.welder_dup_utf8(a[i] ?? \"\"));\n"
            "            return _w;\n"
            "        }\n\n"
            "        /// <summary>Release what ToUtf8Seq staged.</summary>\n"
            "        internal static void FreeUtf8Seq(WelderSeqWire w)\n"
            "        {\n"
            "            if (w.Data == IntPtr.Zero) return;\n"
            "            for (long i = 0; i < w.Len; i++)\n"
            "                NativeMethods.welder_free("
            "Marshal.ReadIntPtr(w.Data, (int)(i * IntPtr.Size)));\n"
            "            Marshal.FreeHGlobal(w.Data);\n"
            "        }\n\n"
            "        /// <summary>Read a returned string sequence, freeing every "
            "element buffer and the array.</summary>\n"
            "        internal static string[] FromUtf8Seq(WelderSeqWire w)\n"
            "        {\n"
            "            var _out = new string[w.Len];\n"
            "            for (long i = 0; i < w.Len; i++)\n"
            "            {\n"
            "                IntPtr _p = Marshal.ReadIntPtr(w.Data, (int)(i * "
            "IntPtr.Size));\n"
            "                _out[i] = Marshal.PtrToStringUTF8(_p) ?? \"\";\n"
            "                NativeMethods.welder_free(_p);\n"
            "            }\n"
            "            if (w.Data != IntPtr.Zero) "
            "NativeMethods.welder_free(w.Data);\n"
            "            return _out;\n"
            "        }\n"
            "    }\n\n"};
        return out;
    }

    /** `NativeMethods`' leading constant — the first P/Invoke chunk, so the
        declarations that follow it read exactly as the single-file form did.
        @return the `Lib` constant line. */
    std::string _cs_pinvoke_prologue() const {
        return "        internal const string Lib = \"" + opts.library + "\";\n\n";
    }

    /** The two declarations every assembly needs regardless of what is bound
        (the allocator hooks the marshalling helpers call).
        @return the trailing P/Invoke declarations. */
    std::string _cs_pinvoke_epilogue() const {
        return "        [LibraryImport(Lib)] internal static partial void "
               "welder_free(IntPtr p);\n"
               "        [LibraryImport(Lib, StringMarshalling = "
               "StringMarshalling.Utf8)] internal static partial IntPtr "
               "welder_dup_utf8(string s);\n";
    }
};

/** A module handle: the shared document plus this namespace's dotted C#
    namespace path below the root (`""` = the root namespace) — the section
    its types and `Global` members land in. Copyable (the driver's
    `add_submodule` returns one by value). */
struct module_writer {
    document* doc{nullptr};
    std::string cs_ns{}; /**< Dotted C# namespace path (`""` = root). */
};

} // namespace welder::inline v0::rods::csharp
