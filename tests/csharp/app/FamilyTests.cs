// Family-surface round-trip: base-typed data access over welded per-era
// instantiations sharing a welded base MARKED with
// [[=welder::mark::family_surface]] — the surface tests/csharp/cpp/family.hpp
// welds. What the synthesized dispatch members must prove: exact-type hoists
// read AND write, welded members hoist as the family base (live views
// included), welded sequences hoist as a FamilyVector<Base> live view,
// methods forward, era-gated members stay on the concretes, a bare base
// instance throws from the default arm — and an UNMARKED base synthesizes
// nothing, however hoistable its family's intersection is.
using System;
using Xunit;
using family_ns;

public class FamilyTests
{
    [Fact]
    public void ExactTypeMembersHoistReadWrite()
    {
        using var w = new WidgetV2();
        WidgetBase b = w;
        Assert.Equal(2, b.SharedScalar);
        b.SharedScalar = 7;
        Assert.Equal(7, w.SharedScalar);
        b.Label = "azeroth";
        Assert.Equal("azeroth", w.Label);
    }

    [Fact]
    public void SharedSequenceWrapperHoists()
    {
        using var w = new WidgetV1();
        WidgetBase b = w;
        b.Nums.Add(5);
        b.Nums.Add(6);
        Assert.Equal(2, w.Nums.Count);
        Assert.Equal(6, b.Nums[1]);
    }

    [Fact]
    public void WeldedMemberHoistsAsFamilyBase()
    {
        using var w = new WidgetV2();
        WidgetBase b = w;
        Assert.Equal(2, b.Gadget.Power); // GadgetBase-typed, itself dispatched
        b.Gadget.Power = 9;              // the live view writes through
        Assert.Equal(9, w.Gadget.Power);
        using var right = new GadgetV2();
        right.Power = 3;
        b.Gadget = right;                // same era assigns
        Assert.Equal(3, w.Gadget.Power);
        using var wrong = new GadgetV1();
        Assert.Throws<InvalidCastException>(() => b.Gadget = wrong);
    }

    [Fact]
    public void WeldedSequenceHoistsAsFamilyVector()
    {
        using var w = new WidgetV1();
        w.Gadgets.Add(new GadgetV1());
        w.Gadgets.Add(new GadgetV1());
        WidgetBase b = w;
        var view = b.Gadgets;
        Assert.Equal(2, view.Count);
        view[0].Power = 4;               // live element view writes through
        Assert.Equal(4, w.Gadgets[0].Power);
        int n = 0, sum = 0;
        foreach (GadgetBase g in view)
        {
            n++;
            sum += g.Power;
        }
        Assert.Equal(2, n);
        Assert.Equal(5, sum);            // 4 + the default 1
    }

    [Fact]
    public void MethodsDispatch()
    {
        using var w1 = new WidgetV1();
        using var w2 = new WidgetV2();
        WidgetBase b1 = w1, b2 = w2;
        Assert.Equal(1, b1.Era());
        Assert.Equal(2, b2.Era());
        Assert.Equal(6, b2.Scaled(3));
    }

    [Fact]
    public void EraGatedMembersStayOnTheConcretes()
    {
        Assert.Null(typeof(WidgetBase).GetProperty("EraGated"));
        Assert.NotNull(typeof(WidgetV1).GetProperty("EraGated"));
        using var w = new WidgetV1();
        WidgetBase b = w;
        if (b is WidgetV1 v1)            // the pattern-matching story
            v1.EraGated = 42;
        Assert.Equal(42, w.EraGated);
    }

    [Fact]
    public void UnmarkedBaseSynthesizesNothing()
    {
        // The Unmarked family's intersection (SharedScalar, Era) is perfectly
        // hoistable — but its base carries no family_surface mark, so the rod
        // must leave it the bare handle-only base it always was.
        Assert.Null(typeof(UnmarkedBase).GetProperty("SharedScalar"));
        Assert.Null(typeof(UnmarkedBase).GetMethod("Era"));
        Assert.NotNull(typeof(UnmarkedV1).GetProperty("SharedScalar"));
        // The marked bases DID synthesize (the positive control).
        Assert.NotNull(typeof(WidgetBase).GetProperty("SharedScalar"));
    }

    [Fact]
    public void BareBaseThrowsFromTheDefaultArm()
    {
        using var bare = new WidgetBase();
        Assert.Throws<InvalidOperationException>(() => bare.Era());
        Assert.Throws<InvalidOperationException>(() =>
        {
            _ = bare.SharedScalar;
        });
    }
}
