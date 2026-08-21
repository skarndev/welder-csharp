#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <welder/rods/csharp/text.hpp> // emit_doc_comment (family surface)

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
    /** Whether eligible data members bind through the ~25 SHARED erased
        entry points (typed load/store at a generator-computed offset) instead
        of one thunk + one `[LibraryImport]` per accessor per member — the
        difference between ~110k and ~20k P/Invokes on a record-dominated
        surface, on both compilers (gcc instantiates that many fewer thunks;
        the interop source generator emits that many fewer marshalling
        stubs). Layout safety is a generated `static_assert` per erased
        member re-deriving the offset on the compiling platform. Off = the
        bespoke per-member emission for everything (the pre-erasure
        artifact, byte for byte). */
    bool erased_fields{true};
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

/** One bound member of a welded class, as the family-surface synthesis needs
    it (the `[[=welder::mark::family_surface]]` opt-in): the C# spellings the
    emitters resolved, recorded beside the emission so the render-time pass
    can intersect a family's surfaces without re-deriving anything from
    reflection. Type spellings may carry the render-time reference
    placeholders — they compare exactly (same C++ type ⇒ same placeholder)
    and resolve at render like any other emitted reference. */
struct family_member {
    std::string name{};        /**< The member's C# name. */
    bool method{false};        /**< Method (dispatchable overload) vs property. */
    std::string type_str{};    /**< Property: the public C# type spelling. */
    std::string elem_ref{};    /**< Property over a sequence of WELDED elements:
                                    the element's type placeholder (else empty). */
    bool handle_like{false};   /**< Property typed as a welded class. */
    bool settable{false};      /**< Property: whether a `set` arm was emitted. */
    std::string ret_str{};     /**< Method: the public return type spelling. */
    std::string params_decl{}; /**< Method: the public parameter list. */
    std::string args{};        /**< Method: the forwarding argument names. */
    std::string doc{};         /**< The member's doc text (may be empty). */
};

/** One welded class's identity + member manifest, flushed by the class writer
    for the family-surface synthesis: the render pass groups these by resolved
    first-welded-base and hoists each family's member intersection onto the
    base as dispatch members. */
struct family_record {
    std::string cs_path{};  /**< The dotted C# path from the root namespace. */
    std::string cs_ns{};    /**< The enclosing namespace's dotted path. */
    std::string cs_name{};  /**< The C# class name (the leaf). */
    std::string base_ref{}; /**< First welded base's placeholder ref, or empty. */
    bool nested{false};     /**< Nested classes neither form nor head a family. */
    bool marked{false};     /**< Carries the `family_surface` mark for this rod's
                                 language — the OPT-IN a base must have for the
                                 synthesis to touch it. */
    std::vector<std::string> surface_names{}; /**< The class's own member names. */
    std::vector<std::string> nested_names{};  /**< Its nested type names. */
    std::vector<family_member> members{};     /**< The member manifest. */
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
    /** Every flushed class's family manifest: the render pass groups them by
        resolved first-welded-base and synthesizes a version-agnostic base
        surface for each family whose base carries the
        `[[=welder::mark::family_surface]]` opt-in. */
    std::vector<family_record> family_records{};
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
    /** Whether any member took the erased path this run — gates rendering the
        shared entry points (their definitions in shim part 0, their
        `[LibraryImport]` declarations in the managed epilogue). Flipped via
        `claim_erased_stubs`, which also registers their symbols. */
    bool erased_used{false};
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
        if (part == 0 && erased_used)
            out += _erased_stub_definitions();
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
        // The synthesized family surfaces (the marked bases): each block is
        // one more `partial class <Base>` declaration, appended after its
        // namespace's welded declarations.
        const family_surface_text fam{_family_surface()};
        const auto push_family = [&](const std::string& ns) {
            for (const auto& [fns, text] : fam.blocks)
                if (fns == ns)
                    items.push_back({item::kind::types, ns, text});
        };
        for (const auto& s : sections)
            if (s.ns.empty())
                slice_at(s.types, s.breaks, [&](std::string t) {
                    items.push_back({item::kind::types, s.ns, std::move(t)});
                });
        push_family("");
        std::string containers_text{containers};
        if (fam.uses_family_vector)
            containers_text += _family_vector_support();
        if (!containers_text.empty())
            items.push_back({item::kind::containers, "", std::move(containers_text)});
        for (const auto& s : sections)
            if (s.ns.empty() && !s.statics.empty())
                items.push_back({item::kind::statics, s.ns, s.statics});
        for (const auto& s : sections) {
            if (s.ns.empty())
                continue;
            slice_at(s.types, s.breaks, [&](std::string t) {
                items.push_back({item::kind::types, s.ns, std::move(t)});
            });
            push_family(s.ns);
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
    /** What @ref _family_surface hands the render: one synthesized
        `partial class <Base>` block per family, keyed by the base's
        namespace, plus whether any block needs the `FamilyVector<T>`
        support type. */
    struct family_surface_text {
        std::vector<std::pair<std::string, std::string>> blocks{};
        bool uses_family_vector{false};
    };

    /** Resolve reference @a s — a `\x01raw\x02` placeholder, or already-plain
        text — against @ref type_names.
        @param s the reference.
        @return the dotted C# path, or empty when the placeholder is unknown. */
    std::string _resolved_ref(const std::string& s) const {
        if (s.size() < 2 || s.front() != '\x01' || s.back() != '\x02')
            return s;
        const std::string raw{s.substr(1, s.size() - 2)};
        for (const auto& [r, cs] : type_names)
            if (r == raw)
                return cs;
        return {};
    }

    /** Synthesize the version-agnostic family surfaces. A FAMILY is two or
        more top-level welded classes sharing one welded base; a family whose
        base carries the `[[=welder::mark::family_surface]]` opt-in (covering
        this rod's language) gains a `partial class` block ON the base holding
        the member INTERSECTION the concretes bind identically, each member a
        dispatch on the concrete class. The mark is strictly required —
        synthesizing members onto a base is too intrusive to infer from
        structure alone, so an unmarked base is never touched:

        - a property whose C# type is the same on every concrete hoists with
          that exact type (settable when every concrete's is);
        - a property typed as a welded class hoists as the member types'
          common welded base — the getter upcasts, the setter downcasts (an
          `InvalidCastException` names a wrong-era assignment);
        - a property over a sequence of welded elements hoists as a read-only
          `FamilyVector<ElementBase>` live view over the concrete's wrapper;
        - a method overload whose parameter list and return type spell the
          same on every concrete hoists as a forwarding dispatch.

        Everything else — era-gated members, shape-changing members — stays
        on the concretes, reached by pattern matching. The synthesis is pure
        managed text over the concretes' own accessors: no thunks, no
        P/Invokes, and the shim never changes.
        @return the per-namespace blocks. */
    family_surface_text _family_surface() const {
        family_surface_text out{};
        if (family_records.empty())
            return out;
        const auto record_at = [&](const std::string& path) -> const family_record* {
            if (path.empty())
                return nullptr;
            for (const auto& r : family_records)
                if (r.cs_path == path)
                    return &r;
            return nullptr;
        };
        // The common welded base of the member types referenced by @a refs
        // (each a type placeholder): every referenced class must BE it or
        // directly derive it. Empty when there is none.
        const auto common_base = [&](const std::vector<std::string>& refs)
            -> std::string {
            std::string candidate{};
            for (const std::string& ref : refs) {
                const std::string t{_resolved_ref(ref)};
                const family_record* rec{record_at(t)};
                if (!rec)
                    return {};
                const std::string b{_resolved_ref(rec->base_ref)};
                if (candidate.empty())
                    candidate = b.empty() ? t : b;
                if (t != candidate && b != candidate)
                    return {};
            }
            return candidate;
        };
        // Group the top-level records by resolved base path, in weld order.
        std::vector<std::pair<std::string, std::vector<const family_record*>>>
            families{};
        for (const auto& r : family_records) {
            if (r.nested || r.base_ref.empty())
                continue;
            const std::string base{_resolved_ref(r.base_ref)};
            if (base.empty())
                continue;
            bool found{false};
            for (auto& [b, v] : families)
                if (b == base) {
                    v.push_back(&r);
                    found = true;
                    break;
                }
            if (!found)
                families.push_back({base, {&r}});
        }
        static constexpr const char* reserved[]{
            "Dispose", "Clone", "ToString", "Equals", "GetHashCode"};
        const std::string default_arm{
            "default: throw new InvalidOperationException(\"no era dispatch "
            "for \" + GetType().Name);"};
        for (const auto& [base_path, children] : families) {
            if (children.size() < 2)
                continue;
            const family_record* base{record_at(base_path)};
            if (!base || base->nested || !base->marked)
                continue;
            std::string body{};
            std::vector<std::string> emitted{};
            for (const family_member& fm0 : children.front()->members) {
                // One hoist per property name / method signature.
                const std::string key{fm0.method
                                          ? fm0.name + "(" + fm0.params_decl + ")"
                                          : fm0.name};
                bool skip{false};
                for (const auto& e : emitted)
                    if (e == key)
                        skip = true;
                for (const char* r : reserved)
                    if (fm0.name == r)
                        skip = true;
                for (const auto* names : {&base->surface_names,
                                          &base->nested_names})
                    for (const auto& n : *names)
                        if (n == fm0.name)
                            skip = true;
                if (skip || fm0.name == base->cs_name)
                    continue;
                emitted.push_back(key);
                // The matching entry on EVERY concrete, or no hoist.
                std::vector<const family_member*> ms{};
                for (const family_record* c : children) {
                    const family_member* hit{nullptr};
                    for (const family_member& m : c->members)
                        if (m.name == fm0.name && m.method == fm0.method &&
                            (!m.method || m.params_decl == fm0.params_decl))
                            hit = &m;
                    if (!hit)
                        break;
                    ms.push_back(hit);
                }
                if (ms.size() != children.size())
                    continue;
                if (fm0.method) {
                    bool same_ret{true};
                    for (const family_member* m : ms)
                        if (m->ret_str != fm0.ret_str)
                            same_ret = false;
                    if (!same_ret)
                        continue;
                    emit_doc_comment(body, "        ",
                                     fm0.doc.empty() ? nullptr : fm0.doc.c_str());
                    const bool is_void{fm0.ret_str == "void"};
                    body += "        public " + fm0.ret_str + " " + fm0.name +
                            "(" + fm0.params_decl + ")\n        {\n"
                            "            switch (this)\n            {\n";
                    for (const family_record* c : children)
                        body += "                case " + c->cs_path + " _c: " +
                                (is_void ? "_c." + fm0.name + "(" + fm0.args +
                                               "); return;"
                                         : "return _c." + fm0.name + "(" +
                                               fm0.args + ");") +
                                "\n";
                    body += "                " + default_arm +
                            "\n            }\n        }\n\n";
                    continue;
                }
                // Properties: exact-type, welded-base, or sequence-of-welded.
                bool same_type{true}, all_handle{true}, all_seq{true},
                    settable{fm0.settable};
                for (const family_member* m : ms) {
                    if (m->type_str != fm0.type_str)
                        same_type = false;
                    if (!m->handle_like)
                        all_handle = false;
                    if (m->elem_ref.empty())
                        all_seq = false;
                    if (!m->settable)
                        settable = false;
                }
                const auto refs_of = [&](bool elem) {
                    std::vector<std::string> refs{};
                    for (const family_member* m : ms)
                        refs.push_back(elem ? m->elem_ref : m->type_str);
                    return refs;
                };
                std::string hoist_type{}, elem_base{};
                if (same_type) {
                    hoist_type = fm0.type_str;
                } else if (all_handle) {
                    hoist_type = common_base(refs_of(false));
                    if (hoist_type.empty())
                        continue;
                } else if (all_seq) {
                    elem_base = common_base(refs_of(true));
                    if (elem_base.empty())
                        continue;
                    hoist_type = "FamilyVector<" + elem_base + ">";
                    out.uses_family_vector = true;
                    settable = false;
                } else {
                    continue;
                }
                emit_doc_comment(body, "        ",
                                 fm0.doc.empty() ? nullptr : fm0.doc.c_str());
                body += "        public " + hoist_type + " " + fm0.name +
                        "\n        {\n            get\n            {\n"
                        "                switch (this)\n                {\n";
                for (std::size_t i{0}; i < children.size(); ++i) {
                    const family_record* c{children[i]};
                    if (elem_base.empty()) {
                        body += "                    case " + c->cs_path +
                                " _c: return _c." + fm0.name + ";\n";
                    } else {
                        body += "                    case " + c->cs_path +
                                " _c:\n                    {\n"
                                "                        var _s = _c." +
                                fm0.name +
                                ";\n                        return new "
                                "FamilyVector<" +
                                elem_base +
                                ">(() => _s.Count, _k => _s[_k]);\n"
                                "                    }\n";
                    }
                }
                body += "                    " + default_arm +
                        "\n                }\n            }\n";
                if (settable) {
                    body += "            set\n            {\n"
                            "                switch (this)\n                {\n";
                    for (std::size_t i{0}; i < children.size(); ++i) {
                        const family_record* c{children[i]};
                        const std::string cast{
                            same_type ? std::string{}
                                      : "(" + ms[i]->type_str + ")"};
                        body += "                    case " + c->cs_path +
                                " _c: _c." + fm0.name + " = " + cast +
                                "value; break;\n";
                    }
                    body += "                    " + default_arm +
                            "\n                }\n            }\n";
                }
                body += "        }\n\n";
            }
            if (body.empty())
                continue;
            std::string block{
                "    // Version-agnostic family surface (welder "
                "family_surface): the members every\n"
                "    // concrete class deriving " +
                base->cs_name +
                " binds identically, dispatched on the\n"
                "    // concrete class. Era-gated members stay on the "
                "concretes - pattern match to\n"
                "    // reach them.\n"
                "    public partial class " +
                base->cs_name + "\n    {\n"};
            block += body;
            block += "    }\n\n";
            out.blocks.push_back({base->cs_ns, std::move(block)});
        }
        return out;
    }

    /** The `FamilyVector<T>` support type — the read-only, base-typed live
        view the synthesized sequence properties return. Rendered once, with
        the generated container wrappers, when any family hoisted a sequence
        member. @return the class text, at namespace depth. */
    static std::string _family_vector_support() {
        return
            "    /// <summary>A read-only live view over a per-era sequence "
            "member, element-typed as the\n"
            "    /// family base: the version-agnostic spelling of a welded "
            "family's vector and\n"
            "    /// fixed-array members. Count and the indexer read through "
            "to the underlying\n"
            "    /// native container; foreach is duck-typed.</summary>\n"
            "    public sealed class FamilyVector<T> where T : class\n"
            "    {\n"
            "        private readonly Func<int> _count;\n"
            "        private readonly Func<int, T> _get;\n"
            "        public FamilyVector(Func<int> count, Func<int, T> get) "
            "{ _count = count; _get = get; }\n"
            "        public int Count => _count();\n"
            "        public T this[int i] => _get(i);\n"
            "        public Enumerator GetEnumerator() => new "
            "Enumerator(this);\n"
            "        /// <summary>Duck-typed foreach support.</summary>\n"
            "        public struct Enumerator\n"
            "        {\n"
            "            private readonly FamilyVector<T> _c;\n"
            "            private int _i;\n"
            "            internal Enumerator(FamilyVector<T> c) { _c = c; "
            "_i = -1; }\n"
            "            public bool MoveNext() => ++_i < _c.Count;\n"
            "            public T Current => _c[_i];\n"
            "        }\n"
            "    }\n\n";
    }

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
        (the allocator hooks the marshalling helpers call), plus — when any
        member took the erased path — the shared erased-field entry points.
        @return the trailing P/Invoke declarations. */
    std::string _cs_pinvoke_epilogue() const {
        std::string out{
            "        [LibraryImport(Lib)] internal static partial void "
            "welder_free(IntPtr p);\n"
            "        [LibraryImport(Lib, StringMarshalling = "
            "StringMarshalling.Utf8)] internal static partial IntPtr "
            "welder_dup_utf8(string s);\n"};
        if (erased_used)
            out += _erased_stub_declarations();
        return out;
    }

    /** The native definitions of the shared erased-field entry points (shim
        part 0, inside `extern "C"`). One typed load/store per scalar width,
        the `bool` pair, the live-view address form, and the `std::string`
        pair. Every managed caller passes the byte offset the GENERATOR
        computed; the per-member `static_assert`s in the shim shards hold the
        layout contract, so the casts here are sound on any platform that
        compiles. The `self` handles arrive pre-adjusted (each wrapper level's
        handle points at ITS subobject), so `self + off` is the member.
        @return the definitions text. */
    static std::string _erased_stub_definitions() {
        static constexpr const char* scalars[][2]{
            {"sbyte", "std::int8_t"},   {"byte", "std::uint8_t"},
            {"short", "std::int16_t"},  {"ushort", "std::uint16_t"},
            {"int", "std::int32_t"},    {"uint", "std::uint32_t"},
            {"long", "std::int64_t"},   {"ulong", "std::uint64_t"},
            {"float", "float"},         {"double", "double"}};
        std::string out{
            "// The ERASED-FIELD entry points: shared by every erased "
            "data-member property.\n"
            "// The offsets arrive from the managed side (generator-computed); "
            "the per-member\n"
            "// static_asserts in the shim shards verify them on this "
            "platform's ABI.\n"
            "static void welder__field_ok(welder_error* e) { e->code = 0; "
            "e->message = nullptr; }\n\n"};
        for (const auto& [cs, cpp] : scalars) {
            out += std::string{cpp} + " welder__field_get_" + cs +
                   "(void* self, std::int32_t off, welder_error* err) { "
                   "welder__field_ok(err); return *reinterpret_cast<const " +
                   cpp +
                   "*>(static_cast<const char*>(self) + off); }\n"
                   "void welder__field_set_" +
                   cs + "(void* self, std::int32_t off, " + cpp +
                   " v, welder_error* err) { welder__field_ok(err); "
                   "*reinterpret_cast<" +
                   cpp + "*>(static_cast<char*>(self) + off) = v; }\n";
        }
        out +=
            "bool welder__field_get_bool(void* self, std::int32_t off, "
            "welder_error* err) { welder__field_ok(err); return "
            "*reinterpret_cast<const bool*>(static_cast<const char*>(self) + "
            "off); }\n"
            "void welder__field_set_bool(void* self, std::int32_t off, bool v, "
            "welder_error* err) { welder__field_ok(err); "
            "*reinterpret_cast<bool*>(static_cast<char*>(self) + off) = v; }\n"
            "void* welder__field_addr(void* self, std::int32_t off, "
            "welder_error* err) { welder__field_ok(err); return "
            "static_cast<char*>(self) + off; }\n"
            "const char* welder__field_get_str(void* self, std::int32_t off, "
            "welder_error* err) { return wcs::shim::caught<const char*>(err, "
            "[&]() -> const char* { return "
            "wcs::shim::dup(reinterpret_cast<const "
            "::std::string*>(static_cast<const char*>(self) + off)->c_str()); "
            "}); }\n"
            "void welder__field_set_str(void* self, std::int32_t off, const "
            "char* v, welder_error* err) { wcs::shim::caught<int>(err, [&]() "
            "-> int { *reinterpret_cast<::std::string*>(static_cast<char*>("
            "self) + off) = v ? v : \"\"; return 0; }); }\n\n";
        return out;
    }

    /** The `[LibraryImport]` declarations of the shared erased-field entry
        points. `self` is the ABSTRACT `SafeHandle` — inbound marshalling only
        needs AddRef/Release, so every per-class handle passes as the base and
        keeps its premature-collection safety.
        @return the declarations text (inside `NativeMethods`). */
    static std::string _erased_stub_declarations() {
        static constexpr const char* scalars[]{"sbyte", "byte",  "short",
                                               "ushort", "int",  "uint",
                                               "long",  "ulong", "float",
                                               "double"};
        std::string out{};
        for (const char* cs : scalars) {
            out += std::string{
                       "        [LibraryImport(Lib)] internal static partial "} +
                   cs + " welder__field_get_" + cs +
                   "(SafeHandle self, int off, out WelderError err);\n"
                   "        [LibraryImport(Lib)] internal static partial void "
                   "welder__field_set_" +
                   cs + "(SafeHandle self, int off, " + cs +
                   " v, out WelderError err);\n";
        }
        out +=
            "        [LibraryImport(Lib)] [return: "
            "MarshalAs(UnmanagedType.U1)] internal static partial bool "
            "welder__field_get_bool(SafeHandle self, int off, out WelderError "
            "err);\n"
            "        [LibraryImport(Lib)] internal static partial void "
            "welder__field_set_bool(SafeHandle self, int off, "
            "[MarshalAs(UnmanagedType.U1)] bool v, out WelderError err);\n"
            "        [LibraryImport(Lib)] internal static partial IntPtr "
            "welder__field_addr(SafeHandle self, int off, out WelderError "
            "err);\n"
            "        [LibraryImport(Lib)] internal static partial IntPtr "
            "welder__field_get_str(SafeHandle self, int off, out WelderError "
            "err);\n"
            "        [LibraryImport(Lib, StringMarshalling = "
            "StringMarshalling.Utf8)] internal static partial void "
            "welder__field_set_str(SafeHandle self, int off, string v, out "
            "WelderError err);\n";
        return out;
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
