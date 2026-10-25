// Isolated ARM expression fixture. The tool substitutes original/reviewed RHS
// expressions from native_vtable_rules.json; declarations retain their actual
// reviewed spellings. Definitions in a separate fixture object supply tables.
extern "C" {
extern char _ZTV10dScEntry_c[];
extern int _ZTV5Whomp[];
extern void *_ZTV5Stage[];
extern void *_ZTV6Cannon;
extern int _ZTV11ChiefChilly;
extern int _ZTV10Scuttlebug[];
extern void *_ZTV6Dorrie[];
extern int data_ov002_0210a83c[];
}

template<class A,class B> struct SameType { enum { value=0 }; };
template<class A> struct SameType<A,A> { enum { value=1 }; };

// @TYPE_ASSERTIONS@

extern "C" unsigned native_vtable_fixture_original(unsigned index)
{
    switch(index) {
    // @ORIGINAL_CASES@
    default:return 0;
    }
}

extern "C" unsigned native_vtable_fixture_adapted(unsigned index)
{
    switch(index) {
    // @ADAPTED_CASES@
    default:return 0;
    }
}
