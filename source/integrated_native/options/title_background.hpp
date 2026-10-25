#pragma once
namespace title_background {
// The central DS image stays unchanged. Forty LCD pixels on either side
// correspond to 32 DS pixels. Reflect inward at the adjoining edge rather
// than wrapping to the opposite side of the castle.
constexpr unsigned extension_source(unsigned strip_column,bool reflected) {
    return reflected?(strip_column<32?31-strip_column:287-strip_column):
        (strip_column<32?224+strip_column:strip_column-32);
}
}
