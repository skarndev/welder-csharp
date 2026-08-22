#pragma once
#include <welder/rods/csharp/document.hpp>

/** @file
    The GENERIC container scaffolding: the `Vector<T>` / `FixedArray<T>`
    classes every sequence instantiation shares, emitted once.

    A `std::vector`/`std::array` instantiation no longer gets a wrapper CLASS
    of its own — it gets an **ops object** (a `VectorOps<T>` /
    `FixedArrayOps<T>` instance in a generated holder class) carrying the
    delegates over its native thunks, and every wrapper in the API is one of
    the two generic classes here dispatching through it. That is what keeps
    the managed surface (and its documentation) at two container types instead
    of one class per element type: the element axis is a real C# generic
    parameter, the instantiation axis is data.

    Element behavior is decided by which ops are present:
    - a SCALAR/ENUM element supplies the span path (`Data`/`Push`/`Fill`) —
      the indexer reads and writes through a zero-copy `Span<T>` over the C++
      buffer, `AsSpan()` is public, `CopyFrom`/the implicit `T[]` conversion
      work;
    - a WELDED-CLASS (or nested-container) element supplies the live-view path
      (`GetAt`/`View`/`HandleOf`/`SetAt`) — the indexer hands out a live view
      pinned to the wrapper, so `v[i].Field = x` writes through, exactly the
      per-instantiation wrappers' contract.

    One `WelderContainerHandle` (destroy via a stored delegate) serves every
    instantiation, so the P/Invoke self parameters need no per-type handle
    classes either. `WelderContainers` is the T→ops registry the public
    `new Vector<T>()` constructor and the implicit `T[]` conversion resolve
    through; generated `[ModuleInitializer]`s fill it before any user code
    runs.
*/

namespace welder::inline v0::rods::csharp {

/** Emit the shared generic-container scaffolding into @a doc's containers
    buffer, once (claimed like any container key). Every sequence generator
    calls this before emitting its ops holder.
    @param doc the growing document. */
inline void ensure_container_scaffolding(document& doc) {
    if (!doc.claim_container("welder:generic-containers"))
        return;
    doc.containers +=
        R"cs(    /// <summary>The one SafeHandle every generated container wrapper holds: release
    /// goes through the instantiation's stored destroy function (null = a
    /// non-owning view).</summary>
    public sealed class WelderContainerHandle : SafeHandle
    {
        private readonly Action<IntPtr>? _destroy;
        internal WelderContainerHandle(IntPtr handle, Action<IntPtr>? destroy) : base(IntPtr.Zero, destroy != null)
        {
            SetHandle(handle);
            _destroy = destroy;
        }
        public override bool IsInvalid => handle == IntPtr.Zero;
        protected override bool ReleaseHandle()
        {
            _destroy!(handle);
            return true;
        }
    }

    /// <summary>The per-instantiation native operations a Vector&lt;T&gt; dispatches
    /// through. Scalar/enum elements fill the span trio (Data/Push/Fill); welded
    /// and nested-container elements fill the live-view quartet
    /// (GetAt/View/HandleOf/SetAt + Add).</summary>
    public sealed class VectorOps<T>
    {
        internal Func<IntPtr>? New;
        internal Action<IntPtr>? Destroy;
        internal Func<WelderContainerHandle, long>? Size;
        internal Func<WelderContainerHandle, IntPtr>? Data;
        internal Action<WelderContainerHandle, T>? Push;
        internal Action<WelderContainerHandle, IntPtr, long>? Fill;
        internal Func<WelderContainerHandle, long, IntPtr>? GetAt;
        internal Func<IntPtr, object, T>? View;
        internal Func<T, SafeHandle>? HandleOf;
        internal Action<WelderContainerHandle, long, SafeHandle>? SetAt;
        internal Action<WelderContainerHandle, SafeHandle>? Add;
        internal Action<WelderContainerHandle>? Clear;
        internal int ElemSize; // native sizeof(element) when T has a blittable Data mirror, else 0
    }

    /// <summary>The per-instantiation native operations a FixedArray&lt;T&gt;
    /// dispatches through; the extent is data here, not part of a type
    /// name.</summary>
    public sealed class FixedArrayOps<T>
    {
        internal int Count;
        internal Func<IntPtr>? New;
        internal Action<IntPtr>? Destroy;
        internal Func<WelderContainerHandle, IntPtr>? Data;
        internal Action<WelderContainerHandle, IntPtr, long>? Fill;
        internal Func<WelderContainerHandle, long, IntPtr>? GetAt;
        internal Func<IntPtr, object, T>? View;
        internal Func<T, SafeHandle>? HandleOf;
        internal Action<WelderContainerHandle, long, SafeHandle>? SetAt;
        internal int ElemSize; // native sizeof(element) when T has a blittable Data mirror, else 0
    }

    /// <summary>The element-type → ops registry behind <c>new Vector&lt;T&gt;()</c> and
    /// the implicit <c>T[]</c> conversions. Generated module initializers register
    /// every bound instantiation before any user code runs; fixed arrays register
    /// per (element, extent) and resolve by the SOURCE ARRAY's length.</summary>
    public static class WelderContainers
    {
        private static readonly System.Collections.Generic.Dictionary<Type, object> _vector = new();
        private static readonly System.Collections.Generic.Dictionary<Type, System.Collections.Generic.List<object>> _fixed = new();
        internal static void RegisterVector<T>(VectorOps<T> ops) => _vector[typeof(T)] = ops;
        internal static VectorOps<T> VectorOf<T>() =>
            _vector.TryGetValue(typeof(T), out var _o)
                ? (VectorOps<T>)_o
                : throw new InvalidOperationException(
                      "no bound std::vector instantiation has element type " + typeof(T));
        internal static void RegisterFixedArray<T>(FixedArrayOps<T> ops)
        {
            if (!_fixed.TryGetValue(typeof(T), out var _l))
                _fixed[typeof(T)] = _l = new System.Collections.Generic.List<object>();
            _l.Add(ops);
        }
        internal static FixedArrayOps<T> FixedArrayOf<T>(int length)
        {
            if (_fixed.TryGetValue(typeof(T), out var _l))
                foreach (var _o in _l)
                    if (((FixedArrayOps<T>)_o).Count == length)
                        return (FixedArrayOps<T>)_o;
            throw new ArgumentException(
                "no bound std::array instantiation has element type " + typeof(T) +
                " and length " + length);
        }
    }

    /// <summary>A reference-semantic C++ std::vector, generic over the element. A
    /// scalar/enum element reads and writes through a zero-copy span over the C++
    /// buffer (AsSpan() is the buffer-protocol view, valid until a size-changing
    /// operation or Dispose); a welded-class element hands out LIVE views, so
    /// <c>v[i].Field = x</c> writes through.</summary>
    public sealed class Vector<T> : IDisposable
    {
        internal readonly VectorOps<T> _ops;
        internal WelderContainerHandle _h;
        internal object? _owner;
        internal Vector(IntPtr handle, bool owns, VectorOps<T> ops)
        {
            _ops = ops;
            _h = new WelderContainerHandle(handle, owns ? ops.Destroy : null);
        }
        /// <summary>A fresh, empty vector (any element type the module binds).</summary>
        public Vector()
        {
            _ops = WelderContainers.VectorOf<T>();
            IntPtr _r = _ops.New!();
            _h = new WelderContainerHandle(_r, _ops.Destroy);
        }
        public int Count => checked((int)_ops.Size!(_h));
        /// <summary>A zero-copy span over the C++ buffer (scalar/enum elements
        /// only); valid until a size-changing operation or Dispose.</summary>
        public unsafe Span<T> AsSpan()
        {
            if (_ops.Data == null)
                throw new InvalidOperationException("AsSpan() requires a scalar or enum element type");
            IntPtr _d = _ops.Data(_h);
            var _s = new Span<T>((void*)_d, Count);
            GC.KeepAlive(this);
            return _s;
        }
        /// <summary>Reinterpret the contiguous native storage as ONE span of blittable
        /// record values — a single interop crossing for the whole buffer, where the
        /// live-view indexer pays per element. TData is the element's nested Data
        /// mirror (size-checked at runtime; the layout itself is asserted in the
        /// native build). Writes go straight to native memory. Valid until a
        /// size-changing operation or Dispose.</summary>
        public unsafe Span<TData> AsSpan<TData>() where TData : unmanaged
        {
            if (_ops.ElemSize == 0)
                throw new InvalidOperationException(
                    "the element type has no blittable Data mirror");
            if (sizeof(TData) != _ops.ElemSize)
                throw new ArgumentException(
                    "sizeof(" + typeof(TData) + ") != native element size " + _ops.ElemSize);
            var _n = Count;
            if (_n == 0)
                return default;
            IntPtr _p = _ops.GetAt!(_h, 0);
            var _s = new Span<TData>((void*)_p, _n);
            GC.KeepAlive(this);
            return _s;
        }
        public T this[int i]
        {
            get
            {
                if (_ops.Data != null)
                    return AsSpan()[i];
                IntPtr _p = _ops.GetAt!(_h, i);
                return _ops.View!(_p, this);
            }
            set
            {
                if (_ops.Data != null)
                {
                    AsSpan()[i] = value;
                    return;
                }
                _ops.SetAt!(_h, i, _ops.HandleOf!(value));
            }
        }
        public void Add(T item)
        {
            if (_ops.Push != null)
            {
                _ops.Push(_h, item);
                return;
            }
            _ops.Add!(_h, _ops.HandleOf!(item));
        }
        public void Clear() => _ops.Clear!(_h);
        public T[] ToArray()
        {
            if (_ops.Data != null)
                return AsSpan().ToArray();
            var _n = Count;
            var _a = new T[_n];
            for (int _i = 0; _i < _n; _i++)
                _a[_i] = this[_i];
            return _a;
        }
        /// <summary>Replace the whole contents (scalar/enum elements only).</summary>
        public void CopyFrom(ReadOnlySpan<T> src)
        {
            if (_ops.Fill == null)
                throw new InvalidOperationException("CopyFrom requires a scalar or enum element type");
            var _tmp = src.ToArray();
            var _gh = GCHandle.Alloc(_tmp, GCHandleType.Pinned);
            try
            {
                _ops.Fill(_h, _gh.AddrOfPinnedObject(), _tmp.Length);
            }
            finally
            {
                _gh.Free();
            }
        }
        public static implicit operator Vector<T>(T[] a)
        {
            var _v = new Vector<T>();
            _v.CopyFrom(a);
            return _v;
        }
        public Enumerator GetEnumerator() => new Enumerator(this);
        /// <summary>Duck-typed foreach support (allocation-free).</summary>
        public struct Enumerator
        {
            private readonly Vector<T> _c;
            private int _i;
            internal Enumerator(Vector<T> c) { _c = c; _i = -1; }
            public bool MoveNext() => ++_i < _c.Count;
            public T Current => _c[_i];
        }
        public void Dispose() => _h.Dispose();
    }

    /// <summary>The fixed-size sibling of Vector&lt;T&gt; — a C++ std::array behind the
    /// same element protocols, minus every size-changing operation. The extent is
    /// the instantiation's, read via Count.</summary>
    public sealed class FixedArray<T> : IDisposable
    {
        internal readonly FixedArrayOps<T> _ops;
        internal WelderContainerHandle _h;
        internal object? _owner;
        internal FixedArray(IntPtr handle, bool owns, FixedArrayOps<T> ops)
        {
            _ops = ops;
            _h = new WelderContainerHandle(handle, owns ? ops.Destroy : null);
        }
        public int Count => _ops.Count;
        /// <summary>A zero-copy span over the C++ buffer (scalar/enum elements
        /// only); valid until Dispose.</summary>
        public unsafe Span<T> AsSpan()
        {
            if (_ops.Data == null)
                throw new InvalidOperationException("AsSpan() requires a scalar or enum element type");
            IntPtr _d = _ops.Data(_h);
            var _s = new Span<T>((void*)_d, Count);
            GC.KeepAlive(this);
            return _s;
        }
        /// <summary>Reinterpret the contiguous native storage as ONE span of blittable
        /// record values — a single interop crossing for the whole buffer, where the
        /// live-view indexer pays per element. TData is the element's nested Data
        /// mirror (size-checked at runtime; the layout itself is asserted in the
        /// native build). Writes go straight to native memory. Valid until a
        /// size-changing operation or Dispose.</summary>
        public unsafe Span<TData> AsSpan<TData>() where TData : unmanaged
        {
            if (_ops.ElemSize == 0)
                throw new InvalidOperationException(
                    "the element type has no blittable Data mirror");
            if (sizeof(TData) != _ops.ElemSize)
                throw new ArgumentException(
                    "sizeof(" + typeof(TData) + ") != native element size " + _ops.ElemSize);
            var _n = Count;
            if (_n == 0)
                return default;
            IntPtr _p = _ops.GetAt!(_h, 0);
            var _s = new Span<TData>((void*)_p, _n);
            GC.KeepAlive(this);
            return _s;
        }
        public T this[int i]
        {
            get
            {
                if (_ops.Data != null)
                    return AsSpan()[i];
                IntPtr _p = _ops.GetAt!(_h, i);
                return _ops.View!(_p, this);
            }
            set
            {
                if (_ops.Data != null)
                {
                    AsSpan()[i] = value;
                    return;
                }
                _ops.SetAt!(_h, i, _ops.HandleOf!(value));
            }
        }
        public T[] ToArray()
        {
            if (_ops.Data != null)
                return AsSpan().ToArray();
            var _n = Count;
            var _a = new T[_n];
            for (int _i = 0; _i < _n; _i++)
                _a[_i] = this[_i];
            return _a;
        }
        /// <summary>Overwrite the elements (scalar/enum elements only; src length
        /// must match Count).</summary>
        public void CopyFrom(ReadOnlySpan<T> src)
        {
            if (_ops.Fill == null)
                throw new InvalidOperationException("CopyFrom requires a scalar or enum element type");
            var _tmp = src.ToArray();
            var _gh = GCHandle.Alloc(_tmp, GCHandleType.Pinned);
            try
            {
                _ops.Fill(_h, _gh.AddrOfPinnedObject(), _tmp.Length);
            }
            finally
            {
                _gh.Free();
            }
        }
        public static implicit operator FixedArray<T>(T[] a)
        {
            // The extent picks the instantiation: resolved by the source
            // array's length (throws ArgumentException when no bound
            // std::array matches — a wrong-length source was an error in the
            // fill thunk before, and stays one here).
            var _ops = WelderContainers.FixedArrayOf<T>(a.Length);
            var _v = new FixedArray<T>(_ops.New!(), true, _ops);
            _v.CopyFrom(a);
            return _v;
        }
        public Enumerator GetEnumerator() => new Enumerator(this);
        /// <summary>Duck-typed foreach support (allocation-free).</summary>
        public struct Enumerator
        {
            private readonly FixedArray<T> _c;
            private int _i;
            internal Enumerator(FixedArray<T> c) { _c = c; _i = -1; }
            public bool MoveNext() => ++_i < _c.Count;
            public T Current => _c[_i];
        }
        public void Dispose() => _h.Dispose();
    }

)cs";
}

} // namespace welder::inline v0::rods::csharp
